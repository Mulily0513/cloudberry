# src/makefiles/ — Build System Helpers

14 platform-specific Makefile fragments and the PGXS extension build system.

## Key Files

- **pgxs.mk** — PostgreSQL Extension Building Infrastructure (PGXS). Used by `contrib/` and `gpcontrib/` extensions.
- **Makefile.{platform}** — Platform-specific rules for Linux, Darwin, AIX, Windows, etc.

Extensions include PGXS via `include $(PGXS)` to get standard build targets (install, installcheck, clean).
