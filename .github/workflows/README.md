# pgraft GitHub Actions Workflows

## Build Packages Workflow

The `build-packages.yml` workflow builds RPM and DEB packages for pgraft PostgreSQL extension across multiple Linux distributions.

### Manual Trigger

This workflow uses `workflow_dispatch` for manual triggering with the following inputs:

#### Inputs

| Parameter | Description | Required | Default | Options |
|-----------|-------------|----------|---------|---------|
| `version` | Package version (e.g., 1.0.0) | Yes | 1.0.0 | Any semver |
| `pg_version` | PostgreSQL major version | Yes | 17 | 17, 16, 15, 14 |
| `release` | Release number | Yes | 1 | Any integer |

### How to Trigger

#### Via GitHub UI

1. Navigate to your repository on GitHub
2. Click on **Actions** tab
3. Select **Build RPM and DEB Packages** workflow
4. Click **Run workflow** button
5. Fill in the inputs:
   - Version: `1.0.0`
   - PostgreSQL Version: `17`
   - Release: `1`
6. Click **Run workflow**

#### Via GitHub CLI

```bash
gh workflow run build-packages.yml \
  -f version=1.0.0 \
  -f pg_version=17 \
  -f release=1
```

#### Via GitHub API

```bash
curl -X POST \
  -H "Accept: application/vnd.github.v3+json" \
  -H "Authorization: token YOUR_GITHUB_TOKEN" \
  https://api.github.com/repos/YOUR_ORG/pgraft/actions/workflows/build-packages.yml/dispatches \
  -d '{"ref":"main","inputs":{"version":"1.0.0","pg_version":"17","release":"1"}}'
```

### Supported Distributions

#### RPM-based

- **CentOS 9** - `centos:9`
- **Rocky Linux 9** - `rockylinux:9`
- **AlmaLinux 9** - `almalinux:9`
- **Fedora Latest** - `fedora:latest`

#### DEB-based

- **Ubuntu 22.04** - `ubuntu:22.04`
- **Ubuntu 24.04** - `ubuntu:24.04`
- **Debian 11** - `debian:11`
- **Debian 12** - `debian:12`

### Jobs

#### 1. `build-rpm`

Builds RPM packages for Red Hat-based distributions.

**Matrix:**
- CentOS 9
- Rocky Linux 9
- AlmaLinux 9
- Fedora Latest

**Outputs:**
- Binary RPM: `pgraft-{version}-{release}.{dist}.{arch}.rpm`
- Source RPM: `pgraft-{version}-{release}.{dist}.src.rpm`

**Artifacts:** Uploaded as `rpm-{os}-pg{version}`

#### 2. `build-deb`

Builds DEB packages for Debian-based distributions.

**Matrix:**
- Ubuntu 22.04
- Ubuntu 24.04
- Debian 11
- Debian 12

**Outputs:**
- Binary DEB: `postgresql-{pg_version}-pgraft_{version}-{release}_{arch}.deb`

**Artifacts:** Uploaded as `deb-{os}-pg{version}`

#### 3. `create-release`

Creates a GitHub release with all built packages.

**Triggers:** After both `build-rpm` and `build-deb` complete

**Actions:**
1. Downloads all package artifacts
2. Creates a new GitHub release
3. Uploads all RPM and DEB packages as release assets

**Release Tag:** `v{version}-pg{pg_version}`
**Example:** `v1.0.0-pg17`

#### 4. `test-installation`

Tests package installation on sample distributions.

**Matrix:**
- CentOS 9 (RPM test)
- Ubuntu 22.04 (DEB test)

**Steps:**
1. Downloads built packages
2. Installs PostgreSQL
3. Installs pgraft package
4. Verifies files are in place
5. Creates pgraft extension in PostgreSQL

### Artifacts

All packages are uploaded as GitHub Actions artifacts with 30-day retention.

**Naming Convention:**
- RPM artifacts: `rpm-{distribution}-pg{version}`
- DEB artifacts: `deb-{distribution}-pg{version}`

### Package Contents

#### RPM Package

```
/usr/pgsql-{version}/lib/pgraft.so
/usr/pgsql-{version}/lib/pgraft_go.so
/usr/pgsql-{version}/share/extension/pgraft.control
/usr/pgsql-{version}/share/extension/pgraft--1.0.sql
```

#### DEB Package

```
/usr/lib/postgresql/{version}/lib/pgraft.so
/usr/lib/postgresql/{version}/lib/pgraft_go.so
/usr/share/postgresql/{version}/extension/pgraft.control
/usr/share/postgresql/{version}/extension/pgraft--1.0.sql
```

### Dependencies

#### Build Dependencies

- PostgreSQL development packages
- Go >= 1.21
- json-c development libraries
- gcc, make
- RPM build tools (RPM) or debhelper (DEB)

#### Runtime Dependencies

- PostgreSQL server (matching version)
- json-c library

### Example Usage

After the workflow completes:

1. **Download packages from Artifacts:**
   ```bash
   # Via GitHub UI or gh CLI
   gh run download {run-id}
   ```

2. **Or download from Release:**
   ```bash
   # RPM
   wget https://github.com/YOUR_ORG/pgraft/releases/download/v1.0.0-pg17/pgraft-1.0.0-1.el9.x86_64.rpm
   
   # DEB
   wget https://github.com/YOUR_ORG/pgraft/releases/download/v1.0.0-pg17/postgresql-17-pgraft_1.0.0-1_amd64.deb
   ```

3. **Install:**
   ```bash
   # RPM
   sudo rpm -ivh pgraft-1.0.0-1.el9.x86_64.rpm
   
   # DEB
   sudo dpkg -i postgresql-17-pgraft_1.0.0-1_amd64.deb
   sudo apt-get install -f  # Fix dependencies if needed
   ```

4. **Use in PostgreSQL:**
   ```sql
   CREATE EXTENSION pgraft;
   
   SELECT pgraft.cluster_health();
   SELECT * FROM pgraft.member_list();
   ```

### Customization

To add more distributions:

1. **For RPM:** Add to the matrix in `build-rpm` job
   ```yaml
   matrix:
     os:
       - centos:9
       - your-new-distro:version
   ```

2. **For DEB:** Add to the matrix in `build-deb` job
   ```yaml
   matrix:
     os:
       - ubuntu:22.04
       - your-new-distro:version
   ```

### Troubleshooting

#### Build Failures

Check the workflow logs for each job:
1. Go to Actions tab
2. Click on the failed workflow run
3. Click on the failed job
4. Expand the failed step

Common issues:
- Missing dependencies
- PostgreSQL version not available
- Go version incompatibility

#### Package Installation Failures

- Verify PostgreSQL is installed with matching version
- Check dependencies: `ldd /path/to/pgraft.so`
- Ensure json-c library is installed

### Local Testing

Test the spec/rules files locally before pushing:

#### RPM (CentOS/Rocky/AlmaLinux)
```bash
docker run -it --rm -v $(pwd):/pgraft rockylinux:9 bash
cd /pgraft
# Follow RPM build steps from workflow
```

#### DEB (Ubuntu/Debian)
```bash
docker run -it --rm -v $(pwd):/pgraft ubuntu:22.04 bash
cd /pgraft
# Follow DEB build steps from workflow
```

### Security

The workflow uses:
- `actions/checkout@v4` - Official GitHub action
- `actions/upload-artifact@v4` - Official GitHub action
- `actions/download-artifact@v4` - Official GitHub action
- `actions/create-release@v1` - Official GitHub action
- `actions/github-script@v7` - Official GitHub action

No third-party actions are used for security.

### Maintenance

To update:
1. PostgreSQL versions: Modify `pg_version` input options
2. Distributions: Modify matrix in respective jobs
3. Dependencies: Update package lists in install steps
4. Package structure: Modify spec/rules files

### Related Documentation

- [pgraft README](../../README.md)
- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [RPM Packaging Guide](https://rpm-packaging-guide.github.io/)
- [Debian Packaging Guide](https://www.debian.org/doc/manuals/maint-guide/)

---

**Maintained by:** pgElephant Team  
**Last Updated:** October 2025

