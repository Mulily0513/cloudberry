# src/pl/ — Procedural Languages

Server-side language handlers for writing stored procedures and functions.

## Languages

- **plpgsql/** — PL/pgSQL: SQL procedural language (default, most commonly used).
- **plpython/** — PL/Python: Python procedures (44 files). Requires `--with-python` at configure.
- **plperl/** — PL/Perl: Perl procedures. Requires `--with-perl` at configure.
- **tcl/** — PL/Tcl: Tcl procedures (15 files).

## Build

```bash
make -C src/pl                # Build all configured languages
make -C src/pl/plpgsql        # Build PL/pgSQL only
```

Each language compiles to a shared library loaded by the server on demand.
Regression tests in each subdirectory: `make -C src/pl/plpgsql check`.
