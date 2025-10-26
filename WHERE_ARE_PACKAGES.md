# Where Are My pgraft Packages After Successful Build?

## 📍 Package Locations

### 1. **GitHub Actions Artifacts** ⏰ (Temporary)
   - **Location**: GitHub → Actions → Your workflow run
   - **Duration**: 30-90 days
   - **How to access**: 
     1. Go to https://github.com/YOUR_USERNAME/pgraft
     2. Click "Actions" tab
     3. Click on your workflow run (✅ green checkmark)
     4. Scroll to bottom → See "Artifacts" section
     5. Click artifact name to download

### 2. **GitHub Releases** ✅ (Permanent) - **RECOMMENDED**
   - **Location**: https://github.com/YOUR_USERNAME/pgraft/releases
   - **Duration**: Forever
   - **How to access**:
     1. Go to https://github.com/YOUR_USERNAME/pgraft/releases
     2. Find your release (e.g., `REL_1_0` or `v1.0.0`)
     3. Download .rpm or .deb files directly

### 3. **GitHub Packages Tab** ⚠️ (NOT for RPM/DEB)
   - **What it is**: Package registry for npm, Docker, Maven, NuGet
   - **Why it's empty**: RPM/DEB packages go to **Releases**, not Packages registry
   - **This is normal!** ✅

## 🚀 How to Create a Release

### Option 1: Manual Trigger (Recommended)

```bash
# Using GitHub CLI
gh workflow run build-packages.yml \
  -f create_release=true \
  -f release_tag=v1.0.0 \
  -f pg_versions=16,17,18
```

### Option 2: GitHub Web UI

1. Go to: https://github.com/YOUR_USERNAME/pgraft/actions/workflows/build-packages.yml
2. Click "Run workflow" (right side)
3. Set inputs:
   - ✅ **create_release**: `true`
   - **release_tag**: `v1.0.0` (or whatever you want)
   - **pg_versions**: `16,17,18` (or your preferred versions)
4. Click "Run workflow"

### Option 3: Automatic on Git Tag

```bash
# Create and push a tag
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0

# This triggers the workflow automatically
```

## 📦 What You'll Get

After the workflow completes with `create_release: true`:

```
https://github.com/YOUR_USERNAME/pgraft/releases/tag/v1.0.0

📦 Assets:
  ├── pgraft_16-1.0.0-1.el9.x86_64.rpm
  ├── pgraft_16-1.0.0-1.el9.src.rpm
  ├── pgraft_17-1.0.0-1.el9.x86_64.rpm
  ├── pgraft_17-1.0.0-1.el9.src.rpm
  ├── postgresql-16-pgraft_1.0.0-1_amd64.deb
  ├── postgresql-17-pgraft_1.0.0-1_amd64.deb
  └── SHA256SUMS
```

## 🔍 Checking Package Status

### Check if Release Exists:
```bash
gh release list --repo YOUR_USERNAME/pgraft
```

### Download All Release Assets:
```bash
gh release download v1.0.0 --repo YOUR_USERNAME/pgraft
```

### View Release in Browser:
```bash
gh release view v1.0.0 --repo YOUR_USERNAME/pgraft --web
```

## ✅ Summary

| Location | Purpose | Duration | Shows in "Packages" tab? |
|----------|---------|----------|--------------------------|
| **Actions Artifacts** | Temporary build outputs | 30-90 days | ❌ No |
| **GitHub Releases** | Permanent downloads | Forever | ❌ No |
| **Packages Registry** | npm/Docker/Maven | Forever | ✅ Yes |

**Your RPM/DEB packages belong in GitHub Releases, not the Packages registry.**

## 💡 Quick Command Reference

```bash
# Download packages from latest release
gh release download --repo YOUR_USERNAME/pgraft

# List all releases
gh release list --repo YOUR_USERNAME/pgraft

# Create new release
gh release create v1.0.0 --title "pgraft 1.0.0" --notes "Release notes here"

# Upload packages to release
gh release upload v1.0.0 *.rpm *.deb --repo YOUR_USERNAME/pgraft
```

---

**Note**: The "Packages" tab is for package registries (npm, Docker), not generic files. Your RPM/DEB files are correctly stored in **GitHub Releases**! ✅
