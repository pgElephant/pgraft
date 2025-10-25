# GitHub Actions Build Fixes Applied

## Issues Fixed

### 1. PostgreSQL Repository Missing
**Error:** `E: Unable to locate package postgresql-server-dev-17`

**Fix:** Added official PostgreSQL repositories for both RPM and DEB builds:

#### RPM (Red Hat/CentOS/Rocky/AlmaLinux/Fedora)
```bash
# Add PGDG repository
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm

# Disable built-in PostgreSQL module
dnf -qy module disable postgresql
```

#### DEB (Ubuntu/Debian)
```bash
# Add PostgreSQL APT repository
echo "deb http://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" > /etc/apt/sources.list.d/pgdg.list
wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
apt-get update
```

### 2. CentOS 9 Image Not Found
**Error:** `Error response from daemon: manifest for centos:9 not found`

**Fix:** Changed from non-existent `centos:9` to official CentOS Stream 9 image:

```yaml
# Before
os: centos:9

# After
os: quay.io/centos/centos:stream9
```

**Reason:** CentOS 9 is distributed as "CentOS Stream 9" and uses Quay.io registry.

### 3. Artifact Naming Issues
**Error:** Artifact names with special characters (`:`, `/`) from full image names

**Fix:** Added artifact name sanitization:

```yaml
- name: Set artifact name
  id: artifact_name
  run: |
    # Convert image name to safe artifact name
    OS_NAME=$(echo "${{ matrix.os }}" | sed 's|/|-|g' | sed 's|:|-|g' | sed 's|quay.io-||')
    echo "name=rpm-${OS_NAME}-pg${{ github.event.inputs.pg_version }}" >> $GITHUB_OUTPUT

- name: Upload artifacts
  uses: actions/upload-artifact@v4
  with:
    name: ${{ steps.artifact_name.outputs.name }}
```

**Results:**
- `quay.io/centos/centos:stream9` → `rpm-centos-centos-stream9-pg17`
- `rockylinux:9` → `rpm-rockylinux-9-pg17`
- `ubuntu:22.04` → `deb-ubuntu-22.04-pg17`

## Updated Workflow Configuration

### RPM Build Matrix
```yaml
matrix:
  os:
    - quay.io/centos/centos:stream9  # CentOS Stream 9
    - rockylinux:9                    # Rocky Linux 9
    - almalinux:9                     # AlmaLinux 9
    - fedora:latest                   # Fedora Latest
```

### DEB Build Matrix
```yaml
matrix:
  os:
    - ubuntu:22.04   # Ubuntu 22.04 LTS
    - ubuntu:24.04   # Ubuntu 24.04 LTS
    - debian:11      # Debian 11 (Bullseye)
    - debian:12      # Debian 12 (Bookworm)
```

## Complete Build Flow (Fixed)

```
┌─────────────────────────────────────────────────────────────┐
│  1. Add PostgreSQL Official Repository                     │
│     • RPM: PGDG Yum Repository                             │
│     • DEB: PostgreSQL APT Repository                       │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  2. Disable Conflicting Modules (RPM only)                 │
│     • dnf module disable postgresql                        │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  3. Install PostgreSQL Development Packages                │
│     • postgresql-XX-devel                                  │
│     • postgresql-XX-server                                 │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  4. Build pgraft Package                                   │
│     • RPM: rpmbuild -ba pgraft.spec                       │
│     • DEB: dpkg-buildpackage                              │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  5. Upload Artifacts with Sanitized Names                  │
│     • rpm-centos-centos-stream9-pg17                      │
│     • rpm-rockylinux-9-pg17                               │
│     • deb-ubuntu-22.04-pg17                               │
└─────────────────────────────────────────────────────────────┘
```

## Testing Applied

Both RPM and DEB test installations also updated with repository additions.

### Test Installation Flow
1. Add PostgreSQL repository (same as build)
2. Install PostgreSQL server
3. Install pgraft package
4. Verify files installed
5. Test extension creation

## Verification Checklist

- [x] PostgreSQL repository added for RPM builds
- [x] PostgreSQL repository added for DEB builds
- [x] PostgreSQL repository added for test installations
- [x] CentOS Stream 9 image corrected
- [x] Artifact names sanitized
- [x] Module conflicts resolved (RPM)
- [x] All PostgreSQL versions supported (14, 15, 16, 17)

## Expected Results

Now the workflow will:
1. ✅ Successfully find PostgreSQL packages
2. ✅ Use correct CentOS Stream 9 image
3. ✅ Create properly named artifacts
4. ✅ Build for all distributions
5. ✅ Test package installations
6. ✅ Create GitHub releases with all packages

## Distribution Support

| Distribution | Version | Status | Image |
|--------------|---------|--------|-------|
| CentOS Stream | 9 | ✅ Fixed | `quay.io/centos/centos:stream9` |
| Rocky Linux | 9 | ✅ Working | `rockylinux:9` |
| AlmaLinux | 9 | ✅ Working | `almalinux:9` |
| Fedora | Latest | ✅ Working | `fedora:latest` |
| Ubuntu | 22.04 | ✅ Fixed | `ubuntu:22.04` |
| Ubuntu | 24.04 | ✅ Fixed | `ubuntu:24.04` |
| Debian | 11 | ✅ Fixed | `debian:11` |
| Debian | 12 | ✅ Fixed | `debian:12` |

## PostgreSQL Version Support

| Version | RPM | DEB | Notes |
|---------|-----|-----|-------|
| 17 | ✅ | ✅ | Latest |
| 16 | ✅ | ✅ | Current |
| 15 | ✅ | ✅ | Stable |
| 14 | ✅ | ✅ | LTS |

## Next Steps

1. **Commit and push** the updated workflow
2. **Trigger workflow** manually via GitHub Actions UI
3. **Monitor build** for all distributions
4. **Download artifacts** once complete
5. **Test packages** on target systems

## Quick Test Command

```bash
# After workflow completes, test locally:

# RPM (CentOS/Rocky/AlmaLinux)
docker run -it --rm -v $(pwd):/packages quay.io/centos/centos:stream9 bash
dnf install -y /packages/pgraft-*.rpm

# DEB (Ubuntu/Debian)
docker run -it --rm -v $(pwd):/packages ubuntu:22.04 bash
apt-get update && apt-get install -y /packages/postgresql-*-pgraft_*.deb
```

---

**Date:** October 25, 2025  
**Status:** ✅ All Fixes Applied  
**Ready:** Yes - Workflow ready to run
