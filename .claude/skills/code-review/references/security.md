# Code Review Rules — Security

These rules apply to **all languages**. They cover the OWASP Top 10 and
database-specific security concerns for Apache Cloudberry.

---

## Injection (OWASP A03)

- `SEC-INJ-001`: **SQL injection.** All SQL constructed from external input
  must use parameterized queries, prepared statements, or
  `quote_literal()`/`quote_identifier()`. Flag string concatenation or
  `sprintf`/`format` with unsanitized input into SQL.
- `SEC-INJ-002`: **Command injection.** External input passed to shell
  commands (`system()`, `exec()`, `popen()`, `subprocess` with `shell=True`,
  backticks). Use argument arrays and avoid shell interpretation.
- `SEC-INJ-003`: **LDAP injection.** User input in LDAP filter strings must
  be escaped with proper LDAP encoding functions.
- `SEC-INJ-004`: **Log injection.** Newlines and control characters in user
  input must be stripped or escaped before writing to logs, to prevent log
  forging and log-based attacks.
- `SEC-INJ-005`: **XML/XXE injection.** XML parsers must disable external
  entity resolution and DTD processing. Flag `DocumentBuilderFactory` or
  `SAXParser` without `setFeature(XMLConstants.FEATURE_SECURE_PROCESSING)`.
- `SEC-INJ-006`: **Expression injection.** User input passed to `eval()`,
  `exec()`, `Function()`, template engines, or regex constructors without
  sanitization.

## Authentication & Authorization (OWASP A01, A07)

- `SEC-AUTH-001`: **Missing permission checks.** Database operations that
  access or modify objects must verify the user has appropriate privileges
  (`ACL_SELECT`, `ACL_INSERT`, etc.) via `pg_*_aclcheck()` functions.
- `SEC-AUTH-002`: **Privilege escalation.** Functions declared `SECURITY
  DEFINER` (runs as owner) must validate caller permissions internally. Flag
  new `SECURITY DEFINER` functions without explicit authorization checks.
- `SEC-AUTH-003`: **Superuser-only operations.** Operations that bypass
  access control must check `superuser()` and be clearly documented as
  superuser-only.
- `SEC-AUTH-004`: **Role membership checks.** Use `has_privs_of_role()` or
  `is_member_of_role()` for role-based checks. Never compare role OIDs
  directly without considering role inheritance.

## Cryptography (OWASP A02)

- `SEC-CRYPTO-001`: **Hardcoded secrets.** No passwords, private keys, API
  tokens, connection strings with credentials, or encryption keys in source
  code. Use environment variables, vaults, or configuration files with
  restricted permissions.
- `SEC-CRYPTO-002`: **Weak algorithms.** Flag MD5, SHA-1, DES, RC4, and
  ECB mode for security-sensitive use. Use SHA-256+, AES-GCM, or the
  project's standard crypto functions.
- `SEC-CRYPTO-003`: **Insufficient randomness.** Use cryptographically secure
  PRNGs (`pg_strong_random`, `/dev/urandom`, `SecureRandom`,
  `secrets` module) for security tokens, nonces, and key generation.
  Flag `rand()`, `random()`, `Math.random()` in security contexts.
- `SEC-CRYPTO-004`: **Plaintext sensitive data.** Passwords must be hashed
  (scram-sha-256 in PostgreSQL context), not stored or transmitted in
  plaintext. Connection strings should use SSL/TLS.

## Sensitive Data Exposure (OWASP A02)

- `SEC-DATA-001`: **Information leakage in errors.** Error messages returned
  to clients must not expose internal paths, stack traces, SQL queries,
  table structures, or system configuration details.
- `SEC-DATA-002`: **Debug code in production paths.** Flag `printf` debugging,
  `console.log`, `System.out.println`, or `elog(LOG, "DEBUG: ...")` in
  non-debug code paths. Ensure debug output is gated behind appropriate log
  levels.
- `SEC-DATA-003`: **Sensitive fields in logs.** Authentication tokens,
  session IDs, passwords, PII, and full query text with literal values must
  be redacted before logging.
- `SEC-DATA-004`: **Core dumps and memory exposure.** Sensitive data in memory
  (passwords, keys) should be zeroed after use (`explicit_bzero`,
  `SecureString`). Prevent sensitive data from appearing in core dumps.

## Input Validation (OWASP A03)

- `SEC-INPUT-001`: **Path traversal.** File paths from external input must be
  validated against allowed directories. Reject `..`, absolute paths, and
  symlink resolution outside the allowed root. In PostgreSQL context, check
  `is_absolute_path()` and validate against `DataDir`.
- `SEC-INPUT-002`: **Buffer overflow.** Fixed-size buffers receiving
  variable-length input must bounds-check. In C, use `strlcpy`/`snprintf`
  instead of `strcpy`/`sprintf`. Check `NAMEDATALEN` limits for identifiers.
- `SEC-INPUT-003`: **Integer overflow in size calculations.** Size/length
  values from external input used in allocation or array indexing must be
  validated against reasonable bounds before use. Check for multiplication
  overflow in `palloc(count * size)` patterns.
- `SEC-INPUT-004`: **Denial of service via input.** Validate size limits on
  user-provided data: query length, number of columns, IN-list size, nesting
  depth, regex complexity. Prevent stack overflow from deeply nested input.
- `SEC-INPUT-005`: **Deserialization safety.** Untrusted data deserialization
  (pickle, Java serialization, custom binary formats) must validate structure
  and sizes before processing. Prefer safe formats (JSON, protobuf) over
  arbitrary serialization.
- `SEC-INPUT-006`: **Unicode and encoding safety.** Validate and normalize
  character encoding at input boundaries. Flag potential encoding mismatch
  between client and server. In PostgreSQL, respect `client_encoding` and
  use `pg_verify_mbstr` for validation.

## Network & Communication Security

- `SEC-NET-001`: **Unencrypted connections.** Network connections carrying
  sensitive data must use TLS/SSL. Flag plaintext HTTP endpoints for
  authenticated APIs, unencrypted interconnect traffic, or FDW connections
  without `sslmode=require` or higher.
- `SEC-NET-002`: **Certificate validation.** TLS connections must validate
  server certificates. Flag `sslmode=disable`, `verify-ca` without proper CA
  configuration, or disabled certificate checks (`CURLOPT_SSL_VERIFYPEER=0`,
  `InsecureSkipVerify`).
- `SEC-NET-003`: **SSRF (Server-Side Request Forgery).** Server-side code
  that fetches URLs from user input must validate the target host against
  an allowlist. Block private/internal IP ranges (10.x, 172.16.x, 169.254.x,
  127.x, ::1) and metadata endpoints (169.254.169.254).
- `SEC-NET-004`: **DNS rebinding.** Resolve hostnames once and use the IP for
  both validation and connection. Don't validate hostname then connect
  separately — the DNS result may change between checks.

## Access Control — Database-Specific

- `SEC-DB-001`: **Catalog modification safety.** Direct manipulation of system
  catalogs (`pg_class`, `pg_proc`, `pg_authid`, etc.) must be done through
  sanctioned APIs, not raw DML. Flag direct `UPDATE pg_class` patterns.
- `SEC-DB-002`: **Search path attacks.** Functions and triggers must be
  resistant to `search_path` manipulation. Use schema-qualified names for
  all function/operator calls in security-sensitive code, or `SET search_path`
  at function entry.
- `SEC-DB-003`: **Row-Level Security bypass.** Ensure RLS policies are not
  inadvertently bypassed by new code. Superuser and table owner bypass RLS
  by default — verify this is intentional.
- `SEC-DB-004`: **Extension security.** Extension SQL scripts must not execute
  with elevated privileges beyond what's needed. Check for unnecessary
  `SECURITY DEFINER`, creation of superuser-owned objects, or granting of
  excessive permissions.
- `SEC-DB-005`: **Temp table hijacking.** Code that uses unqualified temp
  table names in security-sensitive contexts is vulnerable to temp table
  pre-creation attacks. Use schema-qualified names or verify table ownership.

## File System & Environment

- `SEC-FS-001`: **File permission.** Files created at runtime (config, PID
  files, sockets, temp files) must have restrictive permissions (0600/0700).
  Flag `chmod 777`, world-readable credential files, or `umask(0)`.
- `SEC-FS-002`: **Symlink attacks.** Operations on files in shared directories
  (e.g., `/tmp`) must use `O_NOFOLLOW` or verify the file is not a symlink
  before writing. Use `mkstemp`/`mktemp` for temporary files.
- `SEC-FS-003`: **Environment variable trust.** Don't trust environment
  variables for security decisions in setuid/privileged contexts. Sanitize
  `PATH`, `LD_LIBRARY_PATH`, and other environment variables at startup.

## Supply Chain & Dependencies

- `SEC-DEP-001`: **Pinned dependencies.** Build and runtime dependencies must
  be version-pinned. Flag unpinned `pip install`, `npm install` without
  lockfile, or `go get` without version. In Maven, avoid `LATEST`/`RELEASE`
  versions.
- `SEC-DEP-002`: **Known vulnerabilities.** New or upgraded dependencies
  should be checked against known CVE databases. Flag dependencies with
  known critical vulnerabilities.
- `SEC-DEP-003`: **Minimal dependency principle.** New external dependencies
  must be justified. Prefer standard library functionality over third-party
  packages for simple tasks. Each new dependency increases the attack surface.

## Cloudberry / Distributed-Database Specific

- `SEC-DIST-001`: **Interconnect security.** Data flowing between QD and QE
  segments via interconnect should be integrity-checked. Changes to motion
  node or interconnect code must not bypass existing security checks.
- `SEC-DIST-002`: **Segment-level privilege.** Operations dispatched to
  segments must preserve the original user's privilege context. Flag code that
  elevates privileges during dispatch or uses a different auth identity on
  segments.
- `SEC-DIST-003`: **FDW credential handling.** Foreign data wrapper connection
  options containing credentials must use `user mapping` with restricted
  visibility. Credentials must not be logged or included in `EXPLAIN` output.
- `SEC-DIST-004`: **Shared storage access.** Code accessing shared storage
  (S3, HDFS, cloud storage) must validate bucket/path permissions and not
  expose credentials in URIs, logs, or error messages.
