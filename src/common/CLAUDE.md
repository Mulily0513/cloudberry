# src/common/ — Shared Core Utilities

60+ files of low-level code shared between frontend (client tools) and backend (server).

## Contents

- **Cryptography**: MD5, SHA1/SHA2, HMAC, SCRAM authentication, cipher support.
- **Compression**: LZ4, Zstd wrappers.
- **Parsing**: JSON parser, keyword lookup tables.
- **Encoding**: Character set conversion, base64.
- **I/O**: File operations, path manipulation, logging.
- **Numeric**: Ryu algorithm for float-to-string conversion.
- **String**: String utilities, wildcard matching.

## Build

Compiled twice: once for backend (with `-DFRONTEND` unset) and once for frontend tools (with `-DFRONTEND`). This allows the same source to behave differently in client vs server context.
