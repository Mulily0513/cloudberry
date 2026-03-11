# config/ — Autoconf Macros

28 files of m4 macros and helper scripts for the `configure` system.

## Contents

- **Compiler detection**: C/C++, GCC-specific flags.
- **Library checks**: libpq, threading (ax_pthread.m4), Python, Perl, Tcl.
- **Platform features**: version comparison, feature detection.
- **Build helpers**: config.guess, config.sub, install-sh.

These files are consumed by `configure.ac` to generate the `configure` script via autoconf.
