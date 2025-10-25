# pgraft Package Building

Automated RPM and DEB package building via GitHub Actions.

## Quick Start

Packages are built automatically on:
- Push to `main` or `develop`
- Git tags (e.g., `v1.0.0`)
- Manual trigger via GitHub Actions UI

## Download Packages

1. Go to [Actions tab](../../actions)
2. Select latest successful build
3. Download artifacts:
   - `rpm-pg14`, `rpm-pg15`, `rpm-pg16`, `rpm-pg17`
   - `deb-pg14`, `deb-pg15`, `deb-pg16`, `deb-pg17`

## Package Details

### RPM (RHEL/CentOS/Fedora)
- **Name**: `pgraft_17-1.0.0-1.el9.x86_64.rpm`
- **Requires**: `postgresql17-server`, `json-c`
- **Install**: `sudo dnf install pgraft_17-*.rpm`
- **Path**: `/usr/pgsql-17/lib/pgraft*.so`

### DEB (Ubuntu/Debian)
- **Name**: `postgresql-17-pgraft_1.0.0-1_amd64.deb`
- **Depends**: `postgresql-17`, `libjson-c5`
- **Install**: `sudo dpkg -i postgresql-17-pgraft_*.deb`
- **Path**: `/usr/lib/postgresql/17/lib/pgraft*.so`

## Structure

```
packaging/
├── rpm/
│   └── pgraft.spec              # RPM specification
├── deb/
│   ├── control.template         # DEB metadata
│   ├── rules.template           # Build rules
│   ├── changelog.template       # Changelog
│   └── compat                   # Debhelper v10
├── docker/
│   └── Dockerfile.centos9       # RPM builder (for testing)
└── scripts/
    └── verify-package.sh        # Package verification
```

## Local Testing

```bash
# Verify package
./scripts/verify-package.sh path/to/package.rpm rpm
./scripts/verify-package.sh path/to/package.deb deb
```

## Supported PostgreSQL Versions

| Version | RPM | DEB | Status |
|---------|-----|-----|--------|
| PG 14   | ✅  | ✅  | Tested |
| PG 15   | ✅  | ✅  | Tested |
| PG 16   | ✅  | ✅  | Tested |
| PG 17   | ✅  | ✅  | Tested |

## Trigger Manual Build

```bash
gh workflow run build-packages.yml
```

Or use GitHub UI: Actions → Build Packages → Run workflow

