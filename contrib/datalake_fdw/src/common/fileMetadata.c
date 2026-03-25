#include "fileMetadata.h"
#include "lib/stringinfo.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "libpq/libpq-fe.h"
#include "libpq/libpq-int.h"
#include "access/xact.h"
#include "cdb/cdbdisp.h"
#include "cdb/cdbvars.h"
#include "cdb/cdbdispatchresult.h"
#include "utils/json.h"


/*
 * Compatibility shim for pqAddTuple which may not be exported
 * by older postgres binaries.
 */
static bool
pqAddTuple_compat(PGresult *res, PGresAttValue *tup, const char **errmsgp)
{
	if (res->ntups >= res->tupArrSize)
	{
		int newSize = (res->tupArrSize > 0) ? res->tupArrSize * 2 : 128;
		PGresAttValue **newTuples;

		newTuples = (PGresAttValue **)
			realloc(res->tuples, newSize * sizeof(PGresAttValue *));
		if (!newTuples)
		{
			*errmsgp = "out of memory for query result";
			return false;
		}
		res->tuples = newTuples;
		res->tupArrSize = newSize;
	}
	res->tuples[res->ntups] = tup;
	res->ntups++;
	return true;
}

List *FDW_ResultMetaList = NIL;

typedef struct CdbDispatchCmdAsync
{
	struct CdbDispatchResult **dispatchResultPtrArray;
	int			dispatchCount;
	volatile	DispatchWaitMode waitMode;
	const char *ackMessage;
	char	   *query_text;
	int			query_text_len;
}			CdbDispatchCmdAsync;

static bool
receiveDataMeta_internal(PGconn *conn, PGresult *result,
						 const char **errmsg)
{
	char	   *msg;
	int			msglen;

	result->numAttributes = 1;

	pqGetInt(&msglen, 4, conn);
	Assert(msglen > 0);
	msg = (char *) palloc(msglen);

	if (pqGetnchar(msg, msglen, conn))
	{
		*errmsg = libpq_gettext("insufficient data in \"m\" message");
		return false;
	}

	{
		PGdataValue *columns;
		PGresAttValue *tup;

		tup = (PGresAttValue *)
			pqResultAlloc(result, result->numAttributes * sizeof(PGresAttValue), true);
		if (!tup)
			return false;

		columns = (PGdataValue *)
			pqResultAlloc(result, result->numAttributes * sizeof(PGdataValue), true);
		if (!columns)
			return false;

		columns[0].value = msg;
		columns[0].len = msglen;
		for (int i = 0; i < result->numAttributes; i++)
		{
			char	   *val;

			tup[i].len = columns[i].len;
			val = (char *) pqResultAlloc(result, tup[i].len + 1, false);
			if (val == NULL)
				return false;
			memcpy(val, columns[i].value, tup[i].len);
			val[tup[i].len] = '\0';
			tup[i].value = val;
		}

		/* And add the tuple to the PGresult's tuple array */
		if (!pqAddTuple_compat(result, tup, errmsg))
			return false;

		/* Success! */
		if (!conn->result)
			conn->result = result;

		return true;
	}
}

int
RecvMetaMethod(PGconn *conn, int msgLength)
{
	PGresult   *result;
	const char *errmsg = NULL;

	result = conn->result;
	if (!result)
	{
		result = PQmakeEmptyPGresult(conn, PGRES_TUPLES_OK);
		if (!result)
		{
			/* errmsg == NULL means "out of memory", see below */
			goto advance_and_error;
		}
	}
	result->resultStatus = PGRES_TUPLES_OK;

	if (!receiveDataMeta_internal(conn, result, &errmsg))
		goto advance_and_error;

	return 0;
advance_and_error:
	/* Discard unsaved result, if any */
	if (result && result != conn->result)
		PQclear(result);

	/*
	 * Replace partially constructed result with an error result. First
	 * discard the old result to try to win back some memory.
	 */
	pqClearAsyncResult(conn);

	/*
	 * If preceding code didn't provide an error message, assume "out of
	 * memory" was meant.  The advantage of having this special case is that
	 * freeing the old result first greatly improves the odds that gettext()
	 * will succeed in providing a translation.
	 */
	if (!errmsg)
		errmsg = libpq_gettext("out of memory for query result");

	appendPQExpBuffer(&conn->errorMessage, "%s\n", errmsg);
	pqSaveErrorResult(conn);

	/*
	 * Show the message as fully consumed, else pqParseInput3 will overwrite
	 * our error with a complaint about that.
	 */
	conn->inCursor = conn->inStart + 5 + msgLength;

	return 0;
}

int	(*FDWRecvProtocol) (PGconn *conn, int msgLength) = RecvMetaMethod;

void
FDW_SendMeta(bytea *msg)
{
	StringInfoData buf;

	if (msg)
	{
		pq_beginmessage(&buf, 'f');
		pq_sendint32(&buf, VARSIZE_ANY(msg));
		pq_sendbytes(&buf, (const char *) msg, VARSIZE_ANY(msg));
		pq_endmessage(&buf);
		pq_flush();
	}
}


void FDW_RecvMeta(CdbDispatcherState * ds)
{
	if (Gp_role != GP_ROLE_DISPATCH)
		goto chain;

	if (IsAbortInProgress())
		goto chain;

	CdbDispatchCmdAsync *pParams = (CdbDispatchCmdAsync *) ds->dispatchParams;

	if (!pParams)
		goto chain;

	/* fetch qe results */
	for (int i = 0; i < pParams->dispatchCount; i++)
	{
		CdbDispatchResult *dispatchResult = pParams->dispatchResultPtrArray[i];

		for (int j = 0; j < cdbdisp_numPGresult(dispatchResult); j++)
		{
			struct pg_result *res = cdbdisp_getPGresult(dispatchResult, j);

			if (PQresultStatus(res) == PGRES_TUPLES_OK)
			{
				List *meta_list = FDW_deserializeMeta(res);
				FDW_ResultMetaList = list_concat(FDW_ResultMetaList, meta_list);
			}
		}
	}

chain:
	if (datalake_prev_ProcessDispatchResult)
		datalake_prev_ProcessDispatchResult(ds);
}

/*
 * Wire format (fixed-size fields first, variable-length string last):
 *   [int64_t fileSize][int64_t recordCount][FileFormat format][FileContent content][size_t slen][char filePath[slen]]
 */
bytea *FDW_serializeMeta(void *msg)
{
	FileFragment *meta = (FileFragment *) msg;
	bytea	   *result;

	size_t		slen = strlen(meta->filePath);
	size_t		size = 2 * sizeof(int64_t) + sizeof(FileFormat) + sizeof(FileContent) + sizeof(size_t) + slen;

	result = (bytea*)palloc0(size + VARHDRSZ);
	SET_VARSIZE(result, size + VARHDRSZ);

	char	   *ptr = VARDATA(result);
	memcpy(ptr, &(meta->fileSize), sizeof(int64_t));
	ptr += sizeof(int64_t);
	memcpy(ptr, &(meta->recordCount), sizeof(int64_t));
	ptr += sizeof(int64_t);
	memcpy(ptr, &(meta->format), sizeof(FileFormat));
	ptr += sizeof(FileFormat);
	memcpy(ptr, &(meta->content), sizeof(FileContent));
	ptr += sizeof(FileContent);
	memcpy(ptr, &slen, sizeof(size_t));
	ptr += sizeof(size_t);
	memcpy(ptr, meta->filePath, slen);
	return result;
}

List *FDW_deserializeMeta(struct pg_result *res)
{
	if (PQntuples(res) == 0 || PQnfields(res) == 0)
	{
		return NULL;
	}

	List *meta_list = NIL;
	int ntups = PQntuples(res);

	for (int i = 0; i < ntups; i++)
	{
		bytea *msg = (bytea *) PQgetvalue(res, i, 0);
		FileFragment *meta = (FileFragment*)palloc(sizeof(FileFragment));
		char	   *ptr = VARDATA(msg);
		size_t		slen;

		memcpy(&(meta->fileSize), ptr, sizeof(int64_t));
		ptr += sizeof(int64_t);
		memcpy(&(meta->recordCount), ptr, sizeof(int64_t));
		ptr += sizeof(int64_t);
		memcpy(&(meta->format), ptr, sizeof(FileFormat));
		ptr += sizeof(FileFormat);
		memcpy(&(meta->content), ptr, sizeof(FileContent));
		ptr += sizeof(FileContent);
		memcpy(&slen, ptr, sizeof(size_t));
		ptr += sizeof(size_t);
		meta->filePath = pnstrdup(ptr, slen);
		meta_list = lappend(meta_list, meta);
	}
	return meta_list;
}

/*
 * FDW_serialize_file_list_to_json
 *     Serialize a list of FileFragment objects to JSON format.
 *     This is used for sending file lists in POST request bodies instead of headers.
 *
 * Parameters:
 *     file_list - List of FileFragment pointers
 *     output - StringInfoData buffer to receive the JSON output
 *
 * The output format is:
 * {
 *   "files": [
 *     {
 *       "filePath": "...",
 *       "format": "PARQUET",
 *       "content": "DATA_FILE",
 *       "fileSize": 12345,
 *       "recordCount": 100
 *     },
 *     ...
 *   ]
 * }
 */
void
FDW_serialize_file_list_to_json(List *file_list, StringInfoData *output)
{
    ListCell   *lc;
    int         file_count = 0;
    
    if (file_list == NIL)
    {
        initStringInfo(output);
        appendStringInfoString(output, "{\"files\":[]}");
        return;
    }
    
    initStringInfo(output);
    appendStringInfoChar(output, '{');
    appendStringInfoString(output, "\"files\":[");
    
    foreach_with_count(lc, file_list, file_count)
    {
        FileFragment *file = (FileFragment *) lfirst(lc);
        const char  *format_str;
        const char  *content_str;
        
        if (file_count > 0)
            appendStringInfoChar(output, ',');
        
        appendStringInfoChar(output, '{');
        
        /* filePath - use escape_json to handle special characters */
        appendStringInfoString(output, "\"filePath\":");
        escape_json(output, file->filePath);
        
        /* format */
        switch (file->format)
        {
            case ORC:  format_str = "ORC"; break;
            case PARQUET: format_str = "PARQUET"; break;
            case AVRO: format_str = "AVRO"; break;
            default: format_str = "UNKNOWN"; break;
        }
        appendStringInfo(output, ",\"format\":\"%s\"", format_str);
        
        /* content */
        switch (file->content)
        {
            case DATA: content_str = "DATA_FILE"; break;
            case POSITION_DELETES: content_str = "POSITION_DELETE"; break;
            case EQUALITY_DELETES: content_str = "EQUALITY_DELETE"; break;
            case DELTA_LOG: content_str = "DELTA_LOG"; break;
            default: content_str = "UNKNOWN"; break;
        }
        appendStringInfo(output, ",\"content\":\"%s\"", content_str);
        
        /* fileSize */
        appendStringInfo(output, ",\"fileSize\":%ld", (long)file->fileSize);
        
        /* recordCount */
        appendStringInfo(output, ",\"recordCount\":%ld", (long)file->recordCount);
        
        appendStringInfoChar(output, '}');
    }
    
    appendStringInfoString(output, "]}");
}
