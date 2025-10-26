#!/bin/bash
# Verify build-matrix.yml workflow

set -e

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                                                                ║"
echo "║        Build Matrix Workflow Verification                     ║"
echo "║                                                                ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

ERRORS=0
WARNINGS=0

WORKFLOW=".github/workflows/build-matrix.yml"

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "1. File Structure Checks"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if [ -f "$WORKFLOW" ]; then
    echo -e "${GREEN}✓${NC} Workflow file exists"
else
    echo -e "${RED}✗${NC} Workflow file not found"
    exit 1
fi

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "2. Trigger Configuration"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if grep -q "workflow_dispatch:" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} Manual trigger configured (workflow_dispatch)"
else
    echo -e "${RED}✗${NC} Missing manual trigger"
    ((ERRORS++))
fi

if grep -q "push:" "$WORKFLOW" || grep -q "pull_request:" "$WORKFLOW"; then
    echo -e "${RED}✗${NC} Has automatic triggers (should be manual-only)"
    ((ERRORS++))
else
    echo -e "${GREEN}✓${NC} No automatic triggers (manual-only)"
fi

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "3. Job Definitions"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

JOBS=(prepare build package-deb package-rpm test-packages release summary)
for job in "${JOBS[@]}"; do
    if grep -q "^  ${job}:" "$WORKFLOW"; then
        echo -e "${GREEN}✓${NC} Job: $job"
    else
        echo -e "${YELLOW}!${NC} Job: $job (not found)"
        ((WARNINGS++))
    fi
done

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "4. Platform Support"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if grep -q '"ubuntu"' "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} Ubuntu platform"
fi

if grep -q '"macos"' "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} macOS platform"
fi

if grep -q '"rocky"' "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} Rocky Linux platform"
fi

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "5. Critical Fixes Verification"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

# Check for Rocky Linux fix
if grep -q "redhat-rpm-config" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} Rocky Linux fix: redhat-rpm-config installed"
else
    echo -e "${RED}✗${NC} Missing redhat-rpm-config package"
    ((ERRORS++))
fi

# Check for DEB permission fix
if grep -q "mkdir -p /etc/dpkg/dpkg.cfg.d" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} DEB fix: Directory creation added"
else
    echo -e "${YELLOW}!${NC} Warning: Missing mkdir -p for dpkg config"
    ((WARNINGS++))
fi

# Check for jq compact output
if grep -q "jq -nc" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} JSON fix: Compact output configured"
else
    echo -e "${RED}✗${NC} Missing compact JSON output"
    ((ERRORS++))
fi

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "6. PostgreSQL Repository Configuration"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if grep -q "postgresql.org" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} Official PostgreSQL repositories configured"
else
    echo -e "${RED}✗${NC} Missing PostgreSQL repository setup"
    ((ERRORS++))
fi

# Check PGDG setup for Ubuntu
if grep -q "apt.postgresql.org" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} PGDG APT repository (Ubuntu/Debian)"
fi

# Check PGDG setup for Rocky
if grep -q "download.postgresql.org/pub/repos/yum" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} PGDG YUM repository (Rocky/RHEL)"
fi

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "7. Build Optimizations"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if grep -q "DEBIAN_FRONTEND=noninteractive" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} DEB: Non-interactive mode"
fi

if grep -q "man-db" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} DEB: man-db optimization"
fi

if grep -q "deltarpm=0" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} RPM: deltarpm disabled"
fi

if grep -q "install_weak_deps=false" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} RPM: weak dependencies disabled"
fi

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "8. Timeout Configuration"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if grep -q "timeout-minutes:" "$WORKFLOW"; then
    TIMEOUT_COUNT=$(grep -c "timeout-minutes:" "$WORKFLOW")
    echo -e "${GREEN}✓${NC} Timeouts configured ($TIMEOUT_COUNT jobs)"
else
    echo -e "${YELLOW}!${NC} No timeouts configured"
    ((WARNINGS++))
fi

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "9. Matrix Strategy"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if grep -q "strategy:" "$WORKFLOW" && grep -q "matrix:" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} Matrix strategy configured"
else
    echo -e "${RED}✗${NC} Missing matrix strategy"
    ((ERRORS++))
fi

if grep -q "fail-fast: false" "$WORKFLOW"; then
    echo -e "${GREEN}✓${NC} fail-fast disabled (builds all combinations)"
fi

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "10. Dependencies Check"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

DEPS=(gcc make golang json-c-devel postgresql-server-dev)
for dep in "${DEPS[@]}"; do
    if grep -q "$dep" "$WORKFLOW"; then
        echo -e "${GREEN}✓${NC} Dependency: $dep"
    fi
done

echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Summary"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✓ All checks passed!${NC}"
    echo
    echo "Build matrix workflow is ready to use:"
    echo "  gh workflow run build-matrix.yml"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}! Verification completed with warnings${NC}"
    echo
    echo "Errors: $ERRORS"
    echo "Warnings: $WARNINGS"
    echo
    echo "Workflow is functional but could be improved."
    exit 0
else
    echo -e "${RED}✗ Verification failed${NC}"
    echo
    echo "Errors: $ERRORS"
    echo "Warnings: $WARNINGS"
    exit 1
fi
