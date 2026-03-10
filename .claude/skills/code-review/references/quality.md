# Code Review Rules — General Code Quality

> **Scope:** These rules apply to **all languages**. Only rules that affect
> correctness, safety, reliability, or performance are included. Pure
> documentation, naming preference, and minor refactoring rules are excluded
> per `code-review.sh` directives. See SKILL.md "Do NOT comment on" section.

---

## Correctness

- `QUAL-CORRECT-001`: **Off-by-one errors.** Check loop bounds, array indices,
  substring ranges, and fence-post conditions.
- `QUAL-CORRECT-002`: **Null/nil dereference.** Verify pointers, references,
  and optional values are checked before use.
- `QUAL-CORRECT-003`: **Integer overflow/underflow.** Arithmetic on sizes,
  counts, offsets, and timestamps must account for overflow. Use safe-math
  helpers or widen types before computing.
- `QUAL-CORRECT-004`: **Uninitialized variables.** All variables must be
  initialized before first read, especially on branches where assignment may
  be skipped.
- `QUAL-CORRECT-005`: **Unreachable / dead code.** Flag code after
  unconditional return, break, `ereport(ERROR)`, `exit()`, `abort()`, or
  `throw`. Exception: intentional fall-through with comment.
- `QUAL-CORRECT-006`: **Type confusion.** Verify casts between types are safe.
  In C, check sign mismatches (`signed` vs `unsigned`), pointer casts, and
  truncation. In Java/Python, check downcasts.
- `QUAL-CORRECT-007`: **Boolean logic errors.** Watch for inverted conditions
  (`!` vs missing `!`), De Morgan's law mistakes, short-circuit side effects,
  and always-true/always-false conditions.
- `QUAL-CORRECT-008`: **Copy-paste errors.** Detect duplicated blocks with
  mismatched variable names, swapped parameters, or incomplete adaptation.

## Error Handling

- `QUAL-ERR-001`: **Missing error checks.** Every fallible operation (syscall,
  allocation, I/O, network, parsing) must have its return value or exception
  checked. Flag silently-ignored errors.
- `QUAL-ERR-002`: **Swallowed exceptions.** Empty catch/except/rescue blocks
  that silently discard errors. At minimum, log with context.
- `QUAL-ERR-003`: **Error message quality.** Error messages must include enough
  context to diagnose the problem: what failed, what was expected, and relevant
  identifiers (table name, file path, OID, etc.). Flag only when the message
  is misleading or would cause incorrect diagnosis — not for missing docs.
- `QUAL-ERR-004`: **Error propagation.** Errors must not be lost when crossing
  boundaries (function returns, RPC, IPC). Verify error codes and messages
  survive serialization/deserialization.
- `QUAL-ERR-005`: **Cleanup on error paths.** Resources (files, sockets, locks,
  memory, temp tables) must be released on both success and error paths. Check
  early-return and exception paths.

## Resource Management

- `QUAL-RES-001`: **Resource leaks.** Opened files, sockets, database
  connections, cursors, and allocated buffers must be closed/freed on all
  exit paths, including error/exception paths.
- `QUAL-RES-002`: **Double free / double close.** Flag patterns where a
  resource could be released more than once (e.g., `free` in both normal path
  and error handler without nulling the pointer).
- `QUAL-RES-003`: **Use after free / use after close.** Accessing memory,
  file descriptors, or handles after they have been released.
- `QUAL-RES-004`: **Unbounded growth.** Caches, buffers, queues, and
  in-memory collections must have size limits or eviction policies. Flag
  unbounded `append`/`push` in loops without caps.
- `QUAL-RES-005`: **Temporary file/table cleanup.** Temporary resources
  created during operations must be cleaned up, especially on error paths.
  Use RAII, `defer`, `try-finally`, or `ON COMMIT DROP`.

## Concurrency

- `QUAL-CONC-001`: **Race conditions.** Shared mutable state accessed from
  multiple threads/processes without synchronization. Check for
  read-modify-write patterns without locks or atomics.
- `QUAL-CONC-002`: **Deadlocks.** Lock acquisition order must be consistent.
  Flag nested locks acquired in different orders across code paths.
- `QUAL-CONC-003`: **TOCTOU (Time-of-check to time-of-use).** Check-then-act
  patterns on shared state or filesystem without atomicity guarantees.
- `QUAL-CONC-004`: **Signal safety.** Signal handlers must only call
  async-signal-safe functions. Flag `malloc`, `printf`, `elog` in signal
  handlers.
- `QUAL-CONC-005`: **Atomic correctness.** Verify memory ordering on atomic
  operations. Relaxed ordering is only safe when there are no dependent reads.

## Build & Import Hygiene

- `QUAL-BUILD-001`: **Dead imports/includes.** Unused `#include`, `import`,
  `use`, `require` statements. These can cause unnecessary build dependencies,
  longer compile times, or link errors. Flag only when the import is clearly
  unused in the changed code.

## API & Interface Design

- `QUAL-API-001`: **Breaking changes.** Modifications to public function
  signatures, struct layouts, wire formats, SQL function signatures, or CLI
  arguments must be backward-compatible or explicitly versioned.
- `QUAL-API-002`: **Consistent return conventions.** Functions in the same
  module should follow the same pattern for returning errors (return codes,
  exceptions, result types). Mixed styles within a module are a warning.
- `QUAL-API-003`: **Parameter validation.** Public/exported functions must
  validate inputs at the boundary. Internal functions may trust their callers
  but should assert invariants in debug builds.

## Testing

- `QUAL-TEST-001`: **Missing test coverage.** New features, bug fixes, and
  behavioral changes should be accompanied by tests. Flag PRs that modify
  logic without adding or updating tests.
- `QUAL-TEST-002`: **Test determinism.** Tests must not depend on execution
  order, timing, random values, or environment-specific state. Use `ORDER BY`
  in SQL tests, seed random generators, mock time-dependent functions.
- `QUAL-TEST-003`: **Test isolation.** Tests must not leak state (temp tables,
  files, environment variables, GUC settings) that affects other tests. Clean
  up in teardown.
- `QUAL-TEST-004`: **Assertion quality.** Tests should assert specific expected
  values, not just "no error". Flag empty test bodies, tests with no
  assertions, and overly broad assertions.
- `QUAL-TEST-005`: **Edge cases.** Tests should cover boundary conditions:
  empty input, NULL, zero, max values, single element, duplicate elements,
  Unicode, special characters.

## Performance

- `QUAL-PERF-001`: **N+1 queries.** Loop that issues a query per iteration
  when a single batch query would suffice.
- `QUAL-PERF-002`: **Unnecessary copies.** Large data structures copied when
  a reference/pointer would suffice. In C, unnecessary `memcpy` of large
  buffers. In Python/Java, unnecessary list/string copies in hot paths.
- `QUAL-PERF-003`: **Missing index usage.** SQL queries filtering or joining
  on columns without indexes, especially in system catalog queries.
- `QUAL-PERF-004`: **Quadratic or worse algorithms.** Nested loops over the
  same collection, repeated linear search when a hash/tree lookup is possible.
- `QUAL-PERF-005`: **Excessive logging in hot paths.** Debug logging inside
  tight loops or per-tuple processing that can overwhelm I/O. Ensure log
  level guards are checked first.
- `QUAL-PERF-006`: **Blocking I/O in critical sections.** Synchronous I/O,
  DNS resolution, or network calls while holding locks or in time-sensitive
  code paths.

## Logging & Observability

- `QUAL-LOG-001`: **Sensitive data in logs.** Never log passwords, tokens,
  PII, or full query text that may contain sensitive data. Redact or truncate.
- `QUAL-LOG-002`: **Appropriate log levels.** Routine operations at DEBUG/LOG,
  warnings for degraded-but-functional states, ERROR only for actual failures.
  Flag `elog(ERROR)` for non-error conditions or `elog(DEBUG)` for errors.
