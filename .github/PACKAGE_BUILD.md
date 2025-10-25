# Package Build Instructions

## Automated Builds (GitHub Actions)

### Quick Start

1. **Go to GitHub Actions**:
   - Navigate to https://github.com/YOUR_ORG/pgraft/actions
   - Click on "Build RPM and DEB Packages" workflow
   - Click "Run workflow"

2. **Fill in parameters**:
   - Version: `1.0.0` (your package version)
   - PostgreSQL Version: Select from dropdown (17, 16, 15, 14)
   - Release: `1` (package release number)

3. **Wait for completion** (~15-20 minutes)

4. **Download packages**:
   - From Artifacts (bottom of workflow run page)
   - Or from Releases page (auto-created)

### Build Matrix

| Distribution | PostgreSQL Versions | Package Type |
|--------------|---------------------|--------------|
| CentOS 9 | 17, 16, 15, 14 | RPM |
| Rocky Linux 9 | 17, 16, 15, 14 | RPM |
| AlmaLinux 9 | 17, 16, 15, 14 | RPM |
| Fedora Latest | 17, 16, 15, 14 | RPM |
| Ubuntu 22.04 | 17, 16, 15, 14 | DEB |
| Ubuntu 24.04 | 17, 16, 15, 14 | DEB |
| Debian 11 | 17, 16, 15, 14 | DEB |
| Debian 12 | 17, 16, 15, 14 | DEB |

## Manual Builds

### RPM Package (Red Hat, CentOS, Rocky, AlmaLinux, Fedora)

```bash
# Install build dependencies
sudo dnf install -y rpm-build rpmdevtools gcc make \
  postgresql17-devel golang json-c-devel

# Setup RPM build tree
rpmdev-setuptree

# Copy spec file (create from template in .github/workflows/build-packages.yml)
cp pgraft.spec ~/rpmbuild/SPECS/

# Create source tarball
tar czf ~/rpmbuild/SOURCES/pgraft-1.0.0.tar.gz .

# Build RPM
rpmbuild -ba ~/rpmbuild/SPECS/pgraft.spec

# Find built packages
ls ~/rpmbuild/RPMS/*/pgraft-*.rpm
ls ~/rpmbuild/SRPMS/pgraft-*.src.rpm
```

### DEB Package (Ubuntu, Debian)

```bash
# Install build dependencies
sudo apt-get update
sudo apt-get install -y build-essential debhelper devscripts \
  postgresql-server-dev-17 golang-go libjson-c-dev

# Copy debian directory (create from template in .github/workflows/build-packages.yml)
mkdir -p debian

# Build DEB package
dpkg-buildpackage -us -uc -b

# Find built package
ls ../postgresql-17-pgraft_*.deb
```

## Installation

### RPM-based Systems

```bash
# Install package
sudo rpm -ivh pgraft-1.0.0-1.el9.x86_64.rpm

# Or with yum/dnf
sudo dnf install ./pgraft-1.0.0-1.el9.x86_64.rpm
```

### DEB-based Systems

```bash
# Install package
sudo dpkg -i postgresql-17-pgraft_1.0.0-1_amd64.deb

# Fix dependencies if needed
sudo apt-get install -f
```

## Verification

After installation:

```bash
# Check files
rpm -ql pgraft  # RPM
dpkg -L postgresql-17-pgraft  # DEB

# Start PostgreSQL
sudo systemctl start postgresql-17  # RPM
sudo systemctl start postgresql  # DEB

# Create extension
sudo -u postgres psql -c "CREATE EXTENSION pgraft;"

# Verify
sudo -u postgres psql -c "SELECT * FROM pg_available_extensions WHERE name='pgraft';"
```

## Troubleshooting

### Missing Dependencies

**RPM:**
```bash
sudo dnf install epel-release
sudo dnf install json-c
```

**DEB:**
```bash
sudo apt-get update
sudo apt-get install libjson-c5
```

### PostgreSQL Not Found

Make sure PostgreSQL is installed:

**RPM:**
```bash
sudo dnf install postgresql17-server
```

**DEB:**
```bash
sudo apt-get install postgresql-17
```

### Extension Creation Fails

1. Check pgraft is in extension directory:
   ```bash
   ls /usr/pgsql-17/share/extension/pgraft*  # RPM
   ls /usr/share/postgresql/17/extension/pgraft*  # DEB
   ```

2. Check shared library:
   ```bash
   ls /usr/pgsql-17/lib/pgraft.so  # RPM
   ls /usr/lib/postgresql/17/lib/pgraft.so  # DEB
   ```

3. Check shared_preload_libraries:
   ```sql
   SHOW shared_preload_libraries;
   ```

   If pgraft is not loaded, add to postgresql.conf:
   ```
   shared_preload_libraries = 'pgraft'
   ```

   Then restart PostgreSQL.

## GitHub CLI Commands

```bash
# List workflow runs
gh run list --workflow=build-packages.yml

# Watch a workflow run
gh run watch

# Download artifacts
gh run download <run-id>

# Trigger workflow
gh workflow run build-packages.yml \
  -f version=1.0.0 \
  -f pg_version=17 \
  -f release=1

# View workflow logs
gh run view --log
```

## CI/CD Integration

### Automatic Builds on Tag

To automatically build when creating a version tag:

```yaml
on:
  push:
    tags:
      - 'v*'
```

Add this to `.github/workflows/build-packages.yml` to trigger on git tags.

### Build on Release

To automatically build when creating a GitHub release:

```yaml
on:
  release:
    types: [created]
```

## Advanced Options

### Custom PostgreSQL Path

For non-standard PostgreSQL installations:

**RPM:**
```bash
rpmbuild -ba ~/rpmbuild/SPECS/pgraft.spec \
  --define "pg_config /path/to/pg_config"
```

**DEB:**
```bash
export PG_CONFIG=/path/to/pg_config
dpkg-buildpackage -us -uc -b
```

### Debug Build

For debug symbols:

**RPM:**
```bash
rpmbuild -ba ~/rpmbuild/SPECS/pgraft.spec \
  --define "debug_package %{nil}"
```

**DEB:**
```bash
DEB_BUILD_OPTIONS="nostrip" dpkg-buildpackage -us -uc -b
```

---

For more details, see [workflows/README.md](.github/workflows/README.md)
