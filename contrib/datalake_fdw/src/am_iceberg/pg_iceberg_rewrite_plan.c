/*-------------------------------------------------------------------------
 *
 * pg_iceberg_rewrite_plan.c
 *    Implementation of data contracts for Iceberg VACUUM rewrite.
 *
 * This file contains JSON contract helpers for QE rewrite results,
 * including both parsing and payload construction.
 *
 * IDENTIFICATION
 *	  contrib/datalake_fdw/src/am_iceberg/pg_iceberg_rewrite_plan.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "common/jsonapi.h"
#include "mb/pg_wchar.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/json.h"
#include "utils/jsonfuncs.h"

#include "include/pg_iceberg_rewrite_plan.h"

/* Internal parse state for JSON parser */
typedef enum
{
	RW_HELPER_PARSE_NONE,
	RW_HELPER_PARSE_ADDED_FRAGMENTS,
	RW_HELPER_PARSE_REWRITTEN_FRAGMENTS
} RewriteHelperParseField;

typedef struct
{
	JsonLexContext *lex;
	int depth;
	RewriteHelperParseField current_field;
	const char *added_fragments_start;
	const char *added_fragments_end;
	const char *rewritten_fragments_start;
	const char *rewritten_fragments_end;
} RewriteHelperParseState;

/* --- Parser callbacks --- */

static void
rw_helper_object_start(void *state)
{
	RewriteHelperParseState *s = (RewriteHelperParseState *) state;
	s->depth++;
}

static void
rw_helper_object_end(void *state)
{
	RewriteHelperParseState *s = (RewriteHelperParseState *) state;
	s->depth--;
}

static void
rw_helper_array_start(void *state)
{
	RewriteHelperParseState *s = (RewriteHelperParseState *) state;

	if (s->current_field == RW_HELPER_PARSE_ADDED_FRAGMENTS &&
		s->added_fragments_start == NULL)
		s->added_fragments_start = s->lex->token_start;
	else if (s->current_field == RW_HELPER_PARSE_REWRITTEN_FRAGMENTS &&
			 s->rewritten_fragments_start == NULL)
		s->rewritten_fragments_start = s->lex->token_start;

	s->depth++;
}

static void
rw_helper_array_end(void *state)
{
	RewriteHelperParseState *s = (RewriteHelperParseState *) state;

	if (s->current_field == RW_HELPER_PARSE_ADDED_FRAGMENTS)
	{
		s->added_fragments_end = s->lex->prev_token_terminator;
		s->current_field = RW_HELPER_PARSE_NONE;
	}
	else if (s->current_field == RW_HELPER_PARSE_REWRITTEN_FRAGMENTS)
	{
		s->rewritten_fragments_end = s->lex->prev_token_terminator;
		s->current_field = RW_HELPER_PARSE_NONE;
	}

	s->depth--;
}

static void
rw_helper_object_field_start(void *state, char *fname, bool isnull)
{
	RewriteHelperParseState *s = (RewriteHelperParseState *) state;

	if (s->depth == 1)
	{
		if (pg_strcasecmp(fname, "fragments") == 0 ||
			pg_strcasecmp(fname, "addedFragments") == 0)
			s->current_field = RW_HELPER_PARSE_ADDED_FRAGMENTS;
		else if (pg_strcasecmp(fname, "rewrittenFragments") == 0)
			s->current_field = RW_HELPER_PARSE_REWRITTEN_FRAGMENTS;
		else
			s->current_field = RW_HELPER_PARSE_NONE;
	}
}

static void
rw_helper_scalar(void *state, char *token, JsonTokenType tokentype)
{
	/* No scalars needed for now */
}

static char *
rw_helper_extract_array_inner(const char *array_start,
							  const char *array_end)
{
	/*
	 * array_start points to '[' and array_end points to the character after ']'.
	 * Return the array inner payload without surrounding brackets.
	 */
	if (array_start != NULL &&
		array_end != NULL &&
		array_end > array_start + 1)
	{
		return pnstrdup(array_start + 1,
						array_end - array_start - 2);
	}

	return pstrdup("");
}

static const char *
rw_helper_file_format_name(FileFormat format)
{
	switch (format)
	{
		case ORC:
			return "orc";
		case PARQUET:
			return "parquet";
		case AVRO:
			return "avro";
		default:
			return "unknown";
	}
}

static const char *
rw_helper_position_on_delete(FileContent content)
{
	switch (content)
	{
		case DATA:
			return "DATA_FILE";
		case POSITION_DELETES:
			return "POSITION_DELETE";
		case EQUALITY_DELETES:
			return "EQUALITY_DELETE";
		case DELTA_LOG:
			return "DELTA_LOG";
		default:
			return "unknown";
	}
}

void
pg_iceberg_rewrite_append_fragment_json(StringInfo buf,
										FileFragment *fragment,
										int64 fallback_file_size)
{
	const char *path;
	const char *format_name;
	const char *position_on_delete;
	int64 file_size;

	if (fragment == NULL)
		return;

	path = fragment->filePath ? fragment->filePath : "";
	format_name = rw_helper_file_format_name(fragment->format);
	position_on_delete = rw_helper_position_on_delete(fragment->content);
	file_size = (fragment->fileSize > 0) ? fragment->fileSize : fallback_file_size;
	if (file_size < 0)
		file_size = 0;

	appendStringInfoChar(buf, '{');
	appendStringInfoString(buf, "\"path\":");
	escape_json(buf, path);
	appendStringInfoString(buf, ",\"format\":");
	escape_json(buf, format_name);
	appendStringInfo(buf,
					 ",\"record_count\":" INT64_FORMAT ","
					 "\"file_size_in_bytes\":" INT64_FORMAT ","
					 "\"position_on_delete\":",
					 fragment->recordCount,
					 file_size);
	escape_json(buf, position_on_delete);
	appendStringInfoChar(buf, '}');
}

char *
pg_iceberg_rewrite_build_qe_result_json(const char *added_result_json,
										const char *rewritten_fragments_json)
{
	char *added_fragments_json = NULL;
	char *result_json;
	bool has_added = false;
	bool has_rewritten = false;
	const char *rewritten_json =
		(rewritten_fragments_json != NULL) ? rewritten_fragments_json : "";

	if (added_result_json != NULL && added_result_json[0] != '\0')
		pg_iceberg_extract_rewrite_fragments_json(added_result_json,
												  &added_fragments_json);
	else
		added_fragments_json = pstrdup("");

	has_added = (added_fragments_json != NULL && added_fragments_json[0] != '\0');
	has_rewritten = (rewritten_json[0] != '\0');

	if (!has_added && !has_rewritten)
	{
		if (added_fragments_json != NULL)
			pfree(added_fragments_json);
		return NULL;
	}

	result_json = psprintf("{\"fragments\":[%s],\"rewrittenFragments\":[%s]}",
						   added_fragments_json ? added_fragments_json : "",
						   rewritten_json);

	if (added_fragments_json != NULL)
		pfree(added_fragments_json);

	return result_json;
}

/*
 * pg_iceberg_extract_rewrite_fragments_json
 *    Extract the fragments array from a rewrite result JSON.
 *    Expects JSON shape: {"fragments":[...]}.
 */
void
pg_iceberg_extract_rewrite_fragments_json(const char *json,
										  char **fragments_json)
{
	pg_iceberg_extract_rewrite_result_arrays(json, fragments_json, NULL);
}

void
pg_iceberg_extract_rewrite_result_arrays(const char *json,
										 char **added_fragments_json,
										 char **rewritten_fragments_json)
{
	JsonLexContext *lex;
	JsonSemAction sem;
	RewriteHelperParseState ps;

	memset(&ps, 0, sizeof(ps));

	if (json == NULL)
		return;

	lex = makeJsonLexContextCstringLen((char *) json,
								   strlen(json),
								   GetDatabaseEncoding(),
								   true);
	ps.lex = lex;

	memset(&sem, 0, sizeof(sem));
	sem.semstate = &ps;
	sem.object_start = rw_helper_object_start;
	sem.object_end = rw_helper_object_end;
	sem.array_start = rw_helper_array_start;
	sem.array_end = rw_helper_array_end;
	sem.object_field_start = rw_helper_object_field_start;
	sem.scalar = rw_helper_scalar;

	pg_parse_json_or_ereport(lex, &sem);

	if (added_fragments_json != NULL)
		*added_fragments_json = rw_helper_extract_array_inner(
			ps.added_fragments_start,
			ps.added_fragments_end);

	if (rewritten_fragments_json != NULL)
		*rewritten_fragments_json = rw_helper_extract_array_inner(
			ps.rewritten_fragments_start,
			ps.rewritten_fragments_end);

	pfree(lex);
}
