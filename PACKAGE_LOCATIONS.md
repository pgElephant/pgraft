# Where Are My pgraft Packages?

## 🎯 Package Location Guide

Your GitHub Actions workflow built the packages successfully! Here's where to find them:

### Option 1: GitHub Actions Artifacts (Recommended)

#### Step-by-Step:

1. **Go to your repository on GitHub**
   ```
   https://github.com/YOUR_USERNAME/pgraft
   ```

2. **Click on "Actions" tab** (top navigation)

3. **Find your workflow run**
   - Look for: "Build RPM and DEB Packages" or "Build All Packages"
   - Status should be: ✅ (green checkmark)
   - Click on the workflow run

4. **Scroll to the bottom of the page**
   - Section: **"Artifacts"**
   - You'll see something like:

   ```
   📦 Artifacts
   
   rpm-centos-centos-stream9-pg17     2.5 MB    Expires in 30 days
   rpm-rockylinux-9-pg17              2.4 MB    Expires in 30 days
   rpm-almalinux-9-pg17               2.4 MB    Expires in 30 days
   deb-ubuntu-22.04-pg17              1.8 MB    Expires in 30 days
   deb-ubuntu-24.04-pg17              1.8 MB    Expires in 30 days
   deb-debian-11-pg17                 1.8 MB    Expires in 30 days
   deb-debian-12-pg17                 1.8 MB    Expires in 30 days
   ```

5. **Click on an artifact name to download**
   - Downloads as a ZIP file
   - Extract to get your .rpm or .deb files

#### Example:
```
After downloading rpm-rockylinux-9-pg17.zip:

$ unzip rpm-rockylinux-9-pg17.zip
Archive:  rpm-rockylinux-9-pg17.zip
  inflating: pgraft-1.0.0-1.el9.x86_64.rpm
  inflating: pgraft-1.0.0-1.el9.src.rpm
```

### Option 2: GitHub Releases

If the `create-release` job completed successfully:

1. **Go to Releases page**
   ```
   https://github.com/YOUR_USERNAME/pgraft/releases
   ```

2. **Find your release**
   - Tag: `v1.0.0-pg17` (or whatever version you built)

3. **Download packages directly**
   - Packages are attached as release assets
   - Click on .rpm or .deb file to download
   - No ZIP wrapper - direct download

### Option 3: GitHub CLI (Terminal)

If you have GitHub CLI installed:

#### List recent workflow runs:
```bash
gh run list --repo YOUR_USERNAME/pgraft --workflow=build-packages.yml --limit 5
```

Output:
```
STATUS  NAME                        WORKFLOW  BRANCH  EVENT             ID
✓       Build RPM and DEB Packages  build...  main    workflow_dispatch  12345678
```

#### Download artifacts from latest run:
```bash
# Get the latest run ID
RUN_ID=$(gh run list --repo YOUR_USERNAME/pgraft --workflow=build-packages.yml --limit 1 --json databaseId --jq '.[0].databaseId')

# Download all artifacts
gh run download $RUN_ID --repo YOUR_USERNAME/pgraft --dir ./packages

# Result:
ls -R packages/
```

Output:
```
packages/rpm-rockylinux-9-pg17:
pgraft-1.0.0-1.el9.x86_64.rpm
pgraft-1.0.0-1.el9.src.rpm

packages/deb-ubuntu-22.04-pg17:
postgresql-17-pgraft_1.0.0-1_amd64.deb
```

### Option 4: Direct API

```bash
# Get workflow runs
curl -H "Authorization: token YOUR_TOKEN" \
  https://api.github.com/repos/YOUR_USERNAME/pgraft/actions/runs

# Get artifacts from specific run
curl -H "Authorization: token YOUR_TOKEN" \
  https://api.github.com/repos/YOUR_USERNAME/pgraft/actions/runs/RUN_ID/artifacts
```

## 📊 Expected Package Locations

### After GitHub Actions Build:

| Package Type | Artifact Name | Contains |
|--------------|---------------|----------|
| **RPM - CentOS** | `rpm-centos-centos-stream9-pg17` | Binary + Source RPM |
| **RPM - Rocky** | `rpm-rockylinux-9-pg17` | Binary + Source RPM |
| **RPM - AlmaLinux** | `rpm-almalinux-9-pg17` | Binary + Source RPM |
| **DEB - Ubuntu 22.04** | `deb-ubuntu-22.04-pg17` | DEB package |
| **DEB - Ubuntu 24.04** | `deb-ubuntu-24.04-pg17` | DEB package |
| **DEB - Debian 11** | `deb-debian-11-pg17` | DEB package |
| **DEB - Debian 12** | `deb-debian-12-pg17` | DEB package |

### Package File Names:

**RPM:**
- Binary: `pgraft-{version}-{release}.el9.x86_64.rpm`
- Source: `pgraft-{version}-{release}.el9.src.rpm`

**Example:** `pgraft-1.0.0-1.el9.x86_64.rpm`

**DEB:**
- Binary: `postgresql-{pg_version}-pgraft_{version}-{release}_amd64.deb`

**Example:** `postgresql-17-pgraft_1.0.0-1_amd64.deb`

## 🚫 NOT Found Locally

Packages are **NOT** built on your local machine. They are built on GitHub Actions runners and stored in:

1. ✅ GitHub Actions Artifacts (30-90 day retention)
2. ✅ GitHub Releases (permanent, if release job succeeded)

**NOT here:**
- ❌ Your local filesystem
- ❌ `/Users/pgedge/pge/pgraft/` directory
- ❌ `~/rpmbuild/` on your local machine
- ❌ GitHub "Packages" tab (different feature)

## 📝 Quick Reference Card

```
╔═══════════════════════════════════════════════════════════╗
║  WHERE TO FIND PACKAGES                                   ║
╠═══════════════════════════════════════════════════════════╣
║  GitHub UI:                                               ║
║  1. Go to https://github.com/YOUR_USERNAME/pgraft         ║
║  2. Click "Actions" tab                                   ║
║  3. Click on successful workflow run                      ║
║  4. Scroll to bottom → "Artifacts" section                ║
║  5. Click artifact name to download                       ║
╠═══════════════════════════════════════════════════════════╣
║  GitHub CLI:                                              ║
║  gh run download RUN_ID --repo YOUR_USERNAME/pgraft      ║
╠═══════════════════════════════════════════════════════════╣
║  Releases:                                                ║
║  https://github.com/YOUR_USERNAME/pgraft/releases         ║
╚═══════════════════════════════════════════════════════════╝
```

## 🔍 Troubleshooting

### "I don't see Artifacts section"

**Possible reasons:**
1. Workflow didn't complete successfully
2. Build failed before artifact upload step
3. Looking at wrong workflow run

**Solution:** Check workflow status - must be green checkmark ✅

### "Artifacts are empty"

**Possible reasons:**
1. Build failed
2. No files matched the artifact path

**Solution:** Check workflow logs for errors in build steps

### "I see 'Packages' tab but it's empty"

**Explanation:** That's GitHub Packages (Docker/npm/Maven registry).

**Solution:** Look in "Actions" tab → Workflow run → "Artifacts"

## 💡 Local Build (If Needed)

If you want packages on your local machine, build them locally:

### RPM (Rocky Linux)
```bash
docker run -it --rm -v $(pwd):/pgraft rockylinux:9 bash

# Inside container:
cd /pgraft
dnf install -y epel-release
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
dnf install -y rpm-build rpmdevtools gcc make postgresql17-devel golang json-c-devel
rpmdev-setuptree
# ... continue with build steps
```

Packages will be in: `~/rpmbuild/RPMS/x86_64/`

### DEB (Ubuntu)
```bash
docker run -it --rm -v $(pwd):/pgraft ubuntu:22.04 bash

# Inside container:
cd /pgraft
apt-get update
apt-get install -y wget gnupg2 lsb-release
echo "deb http://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" > /etc/apt/sources.list.d/pgdg.list
wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
apt-get update
apt-get install -y build-essential debhelper postgresql-server-dev-17 golang-go libjson-c-dev
# ... continue with build steps
```

Packages will be in parent directory: `../postgresql-17-pgraft_*.deb`

## ✅ Summary

**Your packages ARE built successfully!**

They're just stored in **GitHub Actions Artifacts**, not on your local machine.

**To get them:**
1. Go to GitHub → Actions → Click on your successful run
2. Scroll to bottom → "Artifacts" section
3. Click to download (comes as ZIP)
4. Extract ZIP to get .rpm or .deb files

---

**Still need help?** Check the workflow run logs for the exact artifact names and download links!
