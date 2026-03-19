/*-------------------------------------------------------------------------
 *
 * check_expiration.c
 *	  UDF to request the postmaster to check database expiration date.
 *
 * Portions Copyright (c) 2023-2025, HashData Technology Limited.
 *
 * IDENTIFICATION
 *	  contrib/check_expiration/check_expiration.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "funcapi.h"
#include "miscadmin.h"
#include "storage/pmsignal.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(check_expiration_date);

/*
 * check_expiration_date
 *
 * Send a signal to the postmaster requesting it to perform an
 * expiration date check.  Requires superuser privileges.
 *
 * Returns true if the signal was sent successfully.
 */
Datum
check_expiration_date(PG_FUNCTION_ARGS)
{
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to request expiration check")));

#ifdef USE_EXPIRATION_DATE
	SendPostmasterSignal(PMSIGNAL_CHECK_EXPIRATION);

	ereport(NOTICE,
			(errmsg("expiration date check requested")));

	PG_RETURN_BOOL(true);
#else
	ereport(NOTICE,
			(errmsg("database was not compiled with expiration date support")));

	PG_RETURN_BOOL(false);
#endif
}
