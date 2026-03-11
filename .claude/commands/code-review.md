Review code changes against PostgreSQL/Greenplum/Cloudberry Database coding standards.

## Input

`$ARGUMENTS` can be one of:
- A commit range: `commit1..commit2` or `commit1...commit2`
- A branch name: review all commits from where it diverged from main/master
- `staged`: review currently staged (git add) changes
- Empty: review current branch's commits against origin/main (or origin/master)

## Steps

### 1. Determine what to review

Run one of the following based on the input:

```bash
# If $ARGUMENTS is "staged"
git diff --cached

# If $ARGUMENTS is a commit range (contains "..")
git log --oneline $ARGUMENTS
git diff $ARGUMENTS

# If $ARGUMENTS is a branch name
git log --oneline origin/main..$ARGUMENTS
git diff origin/main...$ARGUMENTS

# If $ARGUMENTS is empty — review current branch vs origin/main
git log --oneline origin/main..HEAD
git diff origin/main...HEAD
```

If origin/main does not exist, try origin/master.

### 2. Gather commit context

For non-staged reviews, get commit messages to understand intent:

```bash
git log --format="commit %h%nAuthor: %an%nDate: %ad%n%n    %s%n    %b" <range>
```

### 3. Read the diff and relevant source files

- Read the full diff output.
- For each significantly changed file, use the Read tool to read the full file for additional context (not just the diff). This is critical for understanding surrounding code, data structures, and API contracts.
- For C code, also check the corresponding header files (.h) when function signatures change.

### 4. Review against PostgreSQL/Greenplum/Cloudberry standards

Evaluate every changed file systematically. Apply these domain-specific review criteria:

#### Memory Management (Critical)
- All palloc'd memory must be in an appropriate MemoryContext
- Check for missing pfree() on long-lived allocations
- Verify MemoryContextSwitchTo() is used correctly (switch back in PG_CATCH)
- Watch for CurrentMemoryContext assumptions in utility functions
- Ensure TopMemoryContext is not polluted with per-query allocations

#### Error Handling (Critical)
- ereport/elog usage: correct error level (ERROR vs WARNING vs NOTICE)
- PG_TRY/PG_CATCH blocks must restore state (CurrentMemoryContext, CurrentResourceOwner)
- No resource leaks in error paths (open files, SPI connections, locks)
- Error messages should follow PostgreSQL style: primary message lowercase, no trailing period, detail/hint in separate fields
- Check that errcode() is appropriate for the error type

#### Concurrency & Locking (Critical for MPP)
- Lock ordering: follow established lock hierarchy to prevent deadlocks
- Check for missing LWLock/SpinLock acquisition before shared memory access
- Verify LockRelease matches LockAcquire
- For Greenplum/Cloudberry MPP code: check QD-QE coordination, motion node handling
- Gang management: proper cleanup on error
- Interconnect: verify flow control, buffer management

#### Catalog & System Cache
- Proper use of SearchSysCache / ReleaseSysCache (every search must have a release)
- Check for cache invalidation handling (RegisterSysCacheCallback)
- Catalog changes must update pg_depend, handle dependencies correctly
- DDL operations need proper event trigger support

#### SQL & Planner
- Verify cost estimation changes are reasonable
- Check for plan correctness: join order, predicate pushdown
- Ensure parameterized paths handle NULLs correctly
- Statistics usage: check for missing/stale statistics assumptions
- For partitioned tables: verify partition pruning, constraint exclusion

#### Executor & Data
- Check for tuple descriptor leaks (missing ReleaseTupleDesc)
- Verify proper slot management (ExecClearTuple, ExecStoreVirtualTuple)
- Scan direction handling in custom scan providers
- Aggregate state transitions: verify transvalue handling, NULL checks
- For Cloudberry: check for motion hazards, slice table correctness

#### WAL & Recovery
- WAL record format changes need XLogRecGetData compatibility
- Check redo routine handles partial/torn records
- Verify WAL consistency (XLogInsert matches redo logic)
- For standby: check conflict resolution, recovery snapshots

#### Extension & Hook API
- Backward compatibility: do not break existing extension ABIs
- Proper use of _PG_init / _PG_fini
- Check hook chain: save and call previous hook
- Shared library versioning (PG_MODULE_MAGIC)

#### Testing
- New features should have regression tests
- Check that test expected output is correct and stable
- Isolation tests for concurrency-sensitive code
- For Cloudberry: verify tests run on multi-segment configurations

#### Code Style (PostgreSQL conventions)
- Tabs for indentation (not spaces) in C code
- Function declarations: return type on separate line
- Variable declarations at top of block (C89 style for older PG versions)
- Comments: use /* */ not // in C code (for PG < 17 compatibility)
- Naming: lowercase_with_underscores, consistent with existing code

### 5. Present the review

Output a structured review:

**Summary**: What the changes do in 1-3 sentences.

**Verdict**: One of:
- **APPROVE** — No significant issues found
- **REQUEST_CHANGES** — Critical or major issues must be addressed
- **COMMENT** — Minor issues or suggestions, acceptable to merge as-is

**Findings**: Group by severity:

1. **Critical** — Bugs, crashes, data corruption, security issues
2. **Major** — Memory leaks, concurrency issues, incorrect behavior in edge cases
3. **Minor** — Style inconsistencies, missing optimizations, documentation
4. **Nit** — Trivial suggestions (only include sparingly)

Each finding should include:
- File path and line number
- Description of the issue
- Suggested fix (code snippet if applicable)
- Reference to PostgreSQL/Greenplum convention or precedent if relevant

### 6. Offer follow-up

After presenting the review, ask the user if they want to:
- Fix any of the issues automatically
- Get more detail on a specific finding
- Review additional context for any file

## Important

- Do NOT modify any files unless the user explicitly asks.
- Read actual source files for context, not just diffs.
- Be pragmatic: if the code follows an existing pattern in the codebase (even if not ideal), don't flag it as an issue.
- For Cloudberry-specific code (src/backend/cdb/*, src/backend/gpopt/*, gpcontrib/*), apply Greenplum/Cloudberry MPP-specific review criteria.
- For standard PostgreSQL code paths, apply upstream PostgreSQL coding standards.
