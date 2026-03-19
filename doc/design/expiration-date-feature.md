# Database Expiration Date Feature — Requirements and Design Document

## 1. Requirements Overview

Provide a compile-time configure option `--with-expiration-date=YYYY-MM-DD` that embeds
an expiration date into the database binary. At runtime the database periodically checks
whether the current date has passed this expiration date. If it has, the database logs an
error and initiates a Fast Shutdown automatically.

### 1.1 Goals

- Provide automatic service termination on expiry for trial / time-limited license scenarios.
- Expiration behavior is deterministic and cannot be bypassed (baked in at compile time, independent of external config files or GUC parameters).
- Zero performance impact during normal operation.

### 1.2 Non-Goals

- No online license verification or dynamic extension mechanism.
- No encryption / obfuscation of the expiration date (it is a compile-time constant readable via reverse engineering).
- No changes to client tools (psql, etc.).

---

## 2. User Interaction

### 2.1 Build Time

```bash
# Specify the expiration date (format: YYYY-MM-DD)
./configure --with-expiration-date=2026-12-31 ...

# Omit the option for no expiration limit (default behavior, identical to current)
./configure ...
```

The configure stage validates the date format and exits with an error if it is invalid.

### 2.2 Runtime

| Scenario | Behavior |
|----------|----------|
| No expiration date configured | No additional checks; behavior identical to upstream |
| Current date <= expiration date | Normal operation; expiration date logged at startup (LOG level) |
| Within 7 days of expiration | WARNING-level alert logged on each periodic check |
| Current date > expiration date | LOG-level error message logged, Fast Shutdown initiated |

### 2.3 Log Examples

```
# At startup
LOG:  database expiration date is 2026-12-31
LOG:  remaining days until expiration: 42

# Warning within 7 days of expiration
WARNING:  database will expire in 3 days (expiration date: 2026-12-31)

# After expiration
LOG:  database has expired (expiration date: 2026-12-31), initiating shutdown
LOG:  received fast shutdown request
```

---

## 3. Technical Design

### 3.1 Overall Architecture

```
┌─────────────┐    compile time   ┌──────────────────┐
│ configure   │ ──AC_DEFINE────▶  │ pg_config.h      │
│ --with-     │                   │ USE_EXPIRATION    │
│ expiration- │                   │ EXPIRATION_YEAR   │
│ date=...    │                   │ EXPIRATION_MONTH  │
└─────────────┘                   │ EXPIRATION_DAY    │
                                  └────────┬─────────┘
                                           │
                              compiled into postmaster binary
                                           │
                                           ▼
                                ┌─────────────────────┐
                                │ postmaster.c         │
                                │ ServerLoop()         │
                                │                      │
                                │  every 12 hours ────▶ CheckExpirationDate()
                                │                      │   ├─ not expired → continue
                                │                      │   ├─ ≤7 days    → WARNING
                                │                      │   └─ expired    → Fast Shutdown
                                └─────────────────────┘
```

### 3.2 Design Choice: Postmaster ServerLoop vs. Background Worker

| Dimension | ServerLoop Inline Check | Background Worker |
|-----------|------------------------|-------------------|
| Reliability | High: postmaster main process, cannot be killed | Medium: may be killed by user or OOM |
| Implementation complexity | Low: ~50 lines of code | Medium: requires worker registration, lifecycle management |
| Performance impact | Near zero: one time() call every 12 hours | Slightly higher: extra process overhead |
| Bypassability | Low: cannot be disabled via SQL or GUC | Medium: can be disabled by modifying shared_preload_libraries |
| Shutdown capability | Directly sets the Shutdown variable | Must signal the postmaster via signals |

**Conclusion: Use the ServerLoop inline check approach.**

### 3.3 Files Modified

| File | Changes |
|------|---------|
| `configure.ac` | Add `--with-expiration-date` option, date format validation, AC_DEFINE |
| `src/include/pg_config.h.in` | Add `#undef USE_EXPIRATION_DATE`, `EXPIRATION_YEAR/MONTH/DAY` |
| `src/backend/postmaster/postmaster.c` | Add `CheckExpirationDate()` function, call it from ServerLoop and startup |
| `src/backend/utils/misc/guc_gp.c` | Add read-only GUC `cb_license_expiration` for `SHOW` queries |

### 3.4 Detailed Design

#### 3.4.1 configure.ac Changes

Add a new option in configure.ac (following the existing `AC_ARG_WITH` pattern):

```m4
#
# --with-expiration-date=YYYY-MM-DD
#
AC_ARG_WITH([expiration-date],
  [AS_HELP_STRING([--with-expiration-date=DATE],
    [set database expiration date in YYYY-MM-DD format])],
  [],
  [with_expiration_date=no])

if test "$with_expiration_date" != "no" -a "$with_expiration_date" != ""; then
  # Validate date format YYYY-MM-DD
  expiration_date="$with_expiration_date"
  if ! echo "$expiration_date" | grep -qE '^[0-9]{4}-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])$'; then
    AC_MSG_ERROR([invalid expiration date format: "$expiration_date" (expected YYYY-MM-DD)])
  fi

  # Split into year/month/day
  exp_year=$(echo "$expiration_date" | cut -d- -f1)
  exp_month=$(echo "$expiration_date" | cut -d- -f2 | sed 's/^0//')
  exp_day=$(echo "$expiration_date" | cut -d- -f3 | sed 's/^0//')

  AC_DEFINE([USE_EXPIRATION_DATE], 1,
    [Define to 1 if a database expiration date is configured.])
  AC_DEFINE_UNQUOTED([EXPIRATION_YEAR], [$exp_year],
    [Expiration year.])
  AC_DEFINE_UNQUOTED([EXPIRATION_MONTH], [$exp_month],
    [Expiration month (1-12).])
  AC_DEFINE_UNQUOTED([EXPIRATION_DAY], [$exp_day],
    [Expiration day (1-31).])
  AC_DEFINE_UNQUOTED([EXPIRATION_DATE_STR], ["$expiration_date"],
    [Expiration date string for display.])

  AC_MSG_NOTICE([database expiration date set to $expiration_date])
fi
```

#### 3.4.2 pg_config.h.in Additions

```c
/* Define to 1 if a database expiration date is configured. */
#undef USE_EXPIRATION_DATE

/* Expiration date components (only meaningful if USE_EXPIRATION_DATE is 1) */
#undef EXPIRATION_YEAR
#undef EXPIRATION_MONTH
#undef EXPIRATION_DAY
#undef EXPIRATION_DATE_STR
```

#### 3.4.3 postmaster.c Core Logic

```c
#ifdef USE_EXPIRATION_DATE
#include <time.h>

/*
 * CheckExpirationDate
 *
 * Compare current date against the compile-time expiration date.
 * Called from ServerLoop() twice a day (every 12 hours).
 *
 * Returns the number of remaining days (negative if expired).
 */
static int
CheckExpirationDate(void)
{
    time_t      now;
    struct tm   tm_now;
    struct tm   tm_exp;
    double      diff_seconds;
    int         remaining_days;

    now = time(NULL);
    localtime_r(&now, &tm_now);

    /* Build expiration date struct (end of day) */
    memset(&tm_exp, 0, sizeof(tm_exp));
    tm_exp.tm_year = EXPIRATION_YEAR - 1900;
    tm_exp.tm_mon  = EXPIRATION_MONTH - 1;
    tm_exp.tm_mday = EXPIRATION_DAY;
    tm_exp.tm_hour = 23;
    tm_exp.tm_min  = 59;
    tm_exp.tm_sec  = 59;
    tm_exp.tm_isdst = -1;  /* let mktime determine DST */

    diff_seconds = difftime(mktime(&tm_exp), now);
    remaining_days = (int)(diff_seconds / 86400);

    if (remaining_days < 0)
    {
        ereport(LOG,
            (errmsg("database has expired (expiration date: %s), "
                    "initiating fast shutdown", EXPIRATION_DATE_STR)));

        /* Trigger Fast Shutdown — same as SIGINT to postmaster */
        Shutdown = FastShutdown;
        if (pmState == PM_RUN ||
            pmState == PM_RECOVERY ||
            pmState == PM_HOT_STANDBY ||
            pmState == PM_STARTUP)
        {
            /* Signal all children to terminate */
            ereport(LOG,
                (errmsg("aborting any active transactions")));
            pmState = PM_STOP_BACKENDS;
        }
        PostmasterStateMachine();
    }
    else if (remaining_days <= 7)
    {
        ereport(WARNING,
            (errmsg("database will expire in %d day(s) (expiration date: %s)",
                    remaining_days, EXPIRATION_DATE_STR)));
    }

    return remaining_days;
}
#endif /* USE_EXPIRATION_DATE */
```

#### 3.4.4 Call Sites

**At startup (in PostmasterMain, after the cluster is ready):**

```c
#ifdef USE_EXPIRATION_DATE
    {
        int remaining = CheckExpirationDate();
        if (remaining >= 0)
            ereport(LOG,
                (errmsg("database expiration date is %s, "
                        "remaining days: %d",
                        EXPIRATION_DATE_STR, remaining)));
    }
#endif
```

**In ServerLoop (after select returns, periodic check):**

```c
    /* Existing periodic checks ... */

#ifdef USE_EXPIRATION_DATE
    /* Check expiration date twice a day (every 12 hours) */
    {
        static time_t last_expiration_check_time = 0;

        if (now - last_expiration_check_time >= 12 * SECS_PER_HOUR)
        {
            CheckExpirationDate();
            last_expiration_check_time = now;
        }
    }
#endif
```

#### 3.4.5 Read-Only GUC `cb_license_expiration`

A `PGC_INTERNAL` read-only GUC is registered in `guc_gp.c` so that users can query
the license expiration date via SQL:

```sql
-- When an expiration date is configured:
SHOW cb_license_expiration;
-- Returns: '2026-12-31'

-- When no expiration date is configured, the GUC does not exist:
SHOW cb_license_expiration;
-- ERROR:  unrecognized configuration parameter "cb_license_expiration"
```

The entire GUC definition is guarded by `#ifdef USE_EXPIRATION_DATE` and only exists
when `--with-expiration-date` is specified at compile time. In builds without an
expiration date, this parameter is completely invisible.

**Variable declaration:**

```c
#ifdef USE_EXPIRATION_DATE
static char *cb_license_expiration_string;
#endif
```

**GUC registration (in the `ConfigureNamesString_gp[]` array):**

```c
#ifdef USE_EXPIRATION_DATE
{
    /* Can't be set in postgresql.conf */
    {"cb_license_expiration", PGC_INTERNAL, PRESET_OPTIONS,
        gettext_noop("Shows the license expiration date."),
        NULL,
        GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
    },
    &cb_license_expiration_string,
    EXPIRATION_DATE_STR,
    NULL, NULL, NULL
},
#endif
```

**Design notes:**

- Uses the `PGC_INTERNAL` context so users cannot modify the value via `SET` or configuration files.
- Both the variable declaration and GUC registration are guarded by `#ifdef USE_EXPIRATION_DATE`.
- In builds without an expiration date, the GUC does not exist at all, revealing no information about the expiration mechanism.
- Placed in `guc_gp.c` (not `guc.c`) because this is a Cloudberry-specific feature.

---

## 4. Security Considerations

| Risk | Mitigation |
|------|------------|
| User rolls back the system clock | Only local time is used; clock rollback cannot be fully prevented. This should be documented. For stronger protection, a monotonic counter or NTP verification could be added (out of scope for this phase). |
| Reverse engineering reveals the expiration date | This feature does not rely on obscurity for security; the expiration date is a plaintext compile-time constant. |
| Loss of in-flight transactions at expiry | Fast Shutdown is used (equivalent to `gpstop -M fast`), which rolls back active transactions without causing data corruption. |
| Time desynchronization across segment nodes | The check runs only in the Postmaster (QD coordinator). When the QD stops, all QE segments stop as well. For standalone segment nodes, each segment's postmaster performs its own independent check. |

---

## 5. Test Plan

### 5.1 Unit Tests

| Test Case | Description |
|-----------|-------------|
| configure format validation | Pass invalid formats (e.g., `2026/12/31`, `abc`, empty value) and verify configure reports an error |
| configure valid value | Pass `2026-12-31` and verify `pg_config.h` contains the correct macro definitions |
| No argument | Verify `USE_EXPIRATION_DATE` is not defined and behavior is identical to upstream |

### 5.2 Integration Tests

| Test Case | Description |
|-----------|-------------|
| Expiration date in the future | Build with a future date, start the cluster, verify LOG shows remaining days, service runs normally |
| Expiration date in the past | Build with a past date, start the cluster, verify it triggers Fast Shutdown immediately after startup |
| Expiration date within 7 days | Set a date within 7 days, verify WARNING log output |
| Expiration date is today | Set today's date, verify normal operation before 23:59:59 and shutdown after |
| No expiration date configured | Default build, verify no expiration-related logs, service runs normally |
| GUC query | `SHOW cb_license_expiration;` returns the correct value |

### 5.3 MPP Cluster Tests

| Test Case | Description |
|-----------|-------------|
| Coordinator expires | Verify that after QD shutdown, all QE segments stop as well |
| Cluster restart | After expiration, `gpstart -a` should fail to start (expiration detected immediately on startup) |

---

## 6. Implementation Steps

| Phase | Task | Estimated Changes |
|-------|------|-------------------|
| 1 | `configure.ac` + `pg_config.h.in`: add option and macro definitions | ~30 lines |
| 2 | `postmaster.c`: add `CheckExpirationDate()`, call from startup and ServerLoop | ~60 lines |
| 3 | `guc_gp.c`: register read-only GUC `cb_license_expiration` | ~20 lines |
| 4 | Regression test cases | ~50 lines SQL/Shell |
| **Total** | | **~160 lines** |

---

## 7. Backward Compatibility

- **Default behavior unchanged**: When `--with-expiration-date` is not specified, `USE_EXPIRATION_DATE` is not defined. All new code is guarded by `#ifdef`, making the compiled result identical to the current version.
- **No catalog changes**: No system table modifications; no impact on `pg_upgrade`.
- **No protocol changes**: No impact on client connection protocols.
