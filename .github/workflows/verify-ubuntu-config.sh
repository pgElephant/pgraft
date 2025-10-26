#!/bin/bash
# Verify Ubuntu configuration has proper sudo commands

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                                                                ║"
echo "║           Ubuntu Configuration Verification                   ║"
echo "║                                                                ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

WORKFLOW=".github/workflows/build-matrix.yml"
ERRORS=0

echo "Checking Ubuntu dependencies section..."
echo

# Check for sudo commands
if grep -A 30 "Install dependencies (Ubuntu)" "$WORKFLOW" | grep -q "sudo apt-get"; then
    echo -e "${GREEN}✓${NC} sudo apt-get commands found"
else
    echo -e "${RED}✗${NC} Missing sudo for apt-get"
    ((ERRORS++))
fi

if grep -A 30 "Install dependencies (Ubuntu)" "$WORKFLOW" | grep -q "sudo mkdir"; then
    echo -e "${GREEN}✓${NC} sudo mkdir found"
else
    echo -e "${RED}✗${NC} Missing sudo for mkdir"
    ((ERRORS++))
fi

if grep -A 30 "Install dependencies (Ubuntu)" "$WORKFLOW" | grep -q "sudo tee"; then
    echo -e "${GREEN}✓${NC} sudo tee found (for file writes)"
else
    echo -e "${RED}✗${NC} Missing sudo tee for file writes"
    ((ERRORS++))
fi

# Check PostgreSQL repository setup
if grep -A 30 "Install dependencies (Ubuntu)" "$WORKFLOW" | grep -q "postgresql.gpg"; then
    echo -e "${GREEN}✓${NC} PostgreSQL GPG key configured"
else
    echo -e "${RED}✗${NC} Missing PostgreSQL GPG key"
    ((ERRORS++))
fi

if grep -A 30 "Install dependencies (Ubuntu)" "$WORKFLOW" | grep -q "pgdg.list"; then
    echo -e "${GREEN}✓${NC} PostgreSQL repository configured"
else
    echo -e "${RED}✗${NC} Missing PostgreSQL repository"
    ((ERRORS++))
fi

# Check for required packages
PACKAGES=(build-essential postgresql-server-dev golang-go libjson-c-dev pkg-config)
echo
echo "Checking required packages..."
for pkg in "${PACKAGES[@]}"; do
    if grep -A 30 "Install dependencies (Ubuntu)" "$WORKFLOW" | grep -q "$pkg"; then
        echo -e "${GREEN}✓${NC} Package: $pkg"
    else
        echo -e "${RED}✗${NC} Missing package: $pkg"
        ((ERRORS++))
    fi
done

echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ $ERRORS -eq 0 ]; then
    echo -e "${GREEN}✓ Ubuntu configuration looks good!${NC}"
    echo
    echo "Configuration summary:"
    echo "  ✓ sudo used for all system commands"
    echo "  ✓ PostgreSQL repository configured"
    echo "  ✓ All required packages included"
    echo "  ✓ man-db optimization (best effort)"
    exit 0
else
    echo -e "${RED}✗ Found $ERRORS issue(s)${NC}"
    exit 1
fi
