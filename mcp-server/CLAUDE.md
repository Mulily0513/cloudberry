# mcp-server/ — MCP Server for AI Integration

Model Context Protocol server exposing Cloudberry database metadata and tools to AI agents (Claude, Cursor, etc.).

## Setup & Run

```bash
cd mcp-server
uv pip install -e ".[dev]"          # Install with dev dependencies
cloudberry-mcp-server --mode stdio  # Stdio transport (recommended for Claude)
cloudberry-mcp-server --mode http   # HTTP transport (default port 8000)
```

Configuration via environment variables (see `dotenv.example`):
`DB_HOST`, `DB_PORT`, `DB_NAME`, `DB_USER`, `DB_PASSWORD`, `MCP_HOST`, `MCP_PORT`, `MCP_DEBUG`.

## Package Structure

```
src/cbmcp/
  server.py      MCP server implementation
  database.py    Database connection and operations
  config.py      Configuration management
  client.py      MCP client
  security.py    SQL query validation
  prompt.py      AI prompt templates
```

## Exposed Capabilities

- **Resources** (3): schemas list, database info, database summary.
- **Tools** (40+): schema introspection, query execution (read-only), EXPLAIN plans, performance monitoring, user/permission management, vacuum info, bloat analysis.
- **Prompts** (3): query performance analysis, index suggestions, health checks.

## Security

- Read-only by default: only SELECT, WITH, SHOW, EXPLAIN allowed.
- Blocks: INSERT, UPDATE, DELETE, DROP, CREATE, ALTER, TRUNCATE, GRANT, REVOKE.
- Sensitive table protection (pg_user, pg_shadow, pg_authid).
- Parameterized queries with sanitization.
- Connection pooling via asyncpg (min=1, max=10).

## Testing

```bash
cd mcp-server
./run_tests.sh                      # Run all tests
uv run pytest tests/ -v             # Verbose test output
uv run pytest --cov=cbmcp           # With coverage
```

Tests in `tests/`: `test_cbmcp.py` (main), `test_database_tools.py` (database tools).

## Dependencies

Core: `fastmcp>=2.10.6`, `asyncpg>=0.29.0`, `psycopg2-binary`, `pydantic>=2.0.0`, `python-dotenv`.
