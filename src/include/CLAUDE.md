# src/include/ — Header Files

35+ subdirectories of header files for the entire database system.

## Key Header Categories

- **nodes/** — Node type definitions: `nodes.h` (NodeTag enum), `parsenodes.h`, `pathnodes.h`, `plannodes.h`, `execnodes.h`.
- **access/** — Table/index access method APIs (`heapam.h`, `genam.h`, `tableam.h`).
- **catalog/** — System catalog definitions and generated headers.
- **executor/** — Executor API (`executor.h`, node implementations).
- **optimizer/** — Planner/optimizer interfaces.
- **parser/** — Parser API and keyword lists.
- **storage/** — Buffer manager, lock manager, storage manager APIs.
- **utils/** — Utility interfaces (palloc, GUC, caching, ADT).
- **cdb/** — Cloudberry distributed system headers (dispatch, motion, gang, interconnect).
- **libpq/** — Client-server protocol definitions.
- **replication/** — WAL replication APIs.
- **commands/** — DDL/DML command handler declarations.

## Conventions

- Headers are included as `#include "subdir/header.h"` (relative to `src/include/`).
- Catalog headers in `catalog/` are partially auto-generated during build.
- Core types defined in `postgres.h` (Datum, Oid, etc.) and `c.h` (basic C types).
