# src/port/ — Platform Portability Layer

70+ files providing OS abstraction for cross-platform compatibility.

## Contents

- **Network**: getaddrinfo, inet_aton, socket wrappers.
- **Threading**: pthread abstractions.
- **File I/O**: pread/pwrite, fsync, file descriptor management.
- **Signal handling**: Platform-specific signal implementations.
- **CRC32**: SSE4.2 and ARMv8 optimized variants.
- **String functions**: strlcpy, strlcat, snprintf replacements for platforms lacking them.
- **Random**: Cryptographic and pseudo-random number generation.
- **Missing POSIX**: Implementations of functions absent on some platforms.

Compiled for both frontend and backend (like `src/common/`). Platform selection is driven by `configure` and `src/template/` files.
