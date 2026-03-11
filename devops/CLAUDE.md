# devops/ — Build & Deployment Automation

Docker-based infrastructure for building, testing, and deploying Cloudberry.

## Structure

```
build/     Docker images for building (Rocky8/9, Ubuntu22.04)
deploy/    Deployment scripts
sandbox/   Local development environment (docker-compose)
release/   Release packaging and automation
```

## Usage

- Build containers provide reproducible compilation environments.
- Sandbox uses docker-compose for local multi-node cluster setup.
- Release scripts handle DEB/RPM packaging.

See `devops/README.md` for detailed instructions.
