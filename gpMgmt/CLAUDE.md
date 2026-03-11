# gpMgmt/ — Cluster Management Tools

Python and Bash tools for administering Cloudberry/Greenplum clusters.

## Structure

```
bin/     User-facing management scripts (52+ tools)
sbin/    System-level scripts (segment start/stop/recovery)
doc/     Tool documentation
test/    Behave BDD tests
demo/    Demo cluster setup (gpdemo)
```

## Key Tools

| Tool | Purpose |
|------|---------|
| `gpinitsystem` | Initialize a new Cloudberry cluster |
| `gpstart` / `gpstop` | Start / stop the cluster |
| `gpinitstandby` | Initialize standby coordinator |
| `gpactivatestandby` | Activate standby as primary |
| `gpaddmirrors` | Add segment mirrors |
| `gpexpand` | Expand cluster (add segments) |
| `gpdeletesystem` | Delete a Cloudberry cluster |
| `gpconfig` | Modify GUC configuration parameters |
| `gpcheckcat` | Check catalog consistency |
| `gpcheckperf` | Hardware performance benchmarks |
| `gpload` / `gpload.py` | Bulk data loading |
| `analyzedb` | Incremental ANALYZE |
| `gplogfilter` | Filter and analyze log files |

## Build & Install

```bash
make -C gpMgmt        # Installs to $GPHOME
make -C gpMgmt check  # Runs tests
```

Scripts install to `$GPHOME/bin/` and `$GPHOME/sbin/`. Python libraries go to `$GPHOME/lib/python/`.

## Testing

```bash
cd gpMgmt/test
make behave tags=tagname            # Run specific BDD tests
make behave flags="[behave flags]"  # Run with custom flags
```

Uses the Behave BDD framework. Test configuration in `Makefile.behave`.
Dev dependencies: `requirements-dev.txt` (behave, coverage, etc.).

## Conventions

- All management scripts are prefixed with `gp`.
- Bash wrappers in `bin/` delegate to Python implementations (e.g., `gpload` calls `gpload.py`).
- Scripts depend on `$GPHOME` environment variable being set.
