# Automation Scripts

Scripts for automating datalake_fdw testing and data preparation.

## Scripts

### install_beeline.sh

Install Apache Hive and beeline client.

**Usage:**
```bash
./install_beeline.sh
```

**Environment Variables:**
- `HIVE_VERSION` - Hive version to install (default: 3.1.3)

**Example:**
```bash
HIVE_VERSION=3.1.3 ./install_beeline.sh
```

**Post-installation:**
```bash
# Load environment variables
source /etc/profile.d/hive.sh

# Verify installation
hive --version
beeline --version
```

### run_hive_smoke.sh

Execute Hive smoke tests to prepare test data.

**Usage:**
```bash
./run_hive_smoke.sh
```

**Environment Variables:**
- `HIVE_HOST` - Hive server hostname (default: localhost)
- `HIVE_PORT` - Hive server port (default: 10000)
- `HIVE_USER` - Hive username (default: hive)
- `HIVE_DATABASE` - Target database (default: default)

**Example:**
```bash
HIVE_HOST=hadoop-master HIVE_PORT=10000 ./run_hive_smoke.sh
```

## Requirements

- curl (for install_beeline.sh)
- Network access to Apache archive and Hive server
