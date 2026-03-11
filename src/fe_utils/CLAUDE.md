# src/fe_utils/ — Frontend Utilities

15 files of client-side support code used by tools in `src/bin/`.

## Contents

- Connection utilities and SQL query helpers.
- Parallel slot management (for parallel pg_dump, etc.).
- Archive operations.
- Conditional/psql script parsing.
- Output formatting.
- Recovery file generation.

Linked by frontend tools alongside `src/common/` and `libpq`.
