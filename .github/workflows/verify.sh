#!/bin/bash
# GitHub Actions Workflow Verification Script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKFLOWS_DIR="$SCRIPT_DIR"

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                                                                ║"
echo "║     GitHub Actions Workflow Verification                      ║"
echo "║                                                                ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

ERRORS=0
WARNINGS=0

check_file() {
    local file=$1
    local name=$(basename "$file")
    
    if [ ! -f "$file" ]; then
        echo -e "${RED}✗${NC} $name - File not found"
        ((ERRORS++))
        return 1
    fi
    
    echo -e "${GREEN}✓${NC} $name - File exists"
    return 0
}

check_yaml_syntax() {
    local file=$1
    local name=$(basename "$file")
    
    if command -v python3 &> /dev/null; then
        if python3 -c "import yaml; yaml.safe_load(open('$file'))" 2>/dev/null; then
            echo -e "  ${GREEN}✓${NC} Valid YAML syntax"
        else
            echo -e "  ${RED}✗${NC} Invalid YAML syntax"
            ((ERRORS++))
        fi
    else
        echo -e "  ${YELLOW}!${NC} Python3 not available, skipping YAML validation"
        ((WARNINGS++))
    fi
}

check_workflow_triggers() {
    local file=$1
    local name=$(basename "$file")
    
    if grep -q "workflow_dispatch:" "$file"; then
        echo -e "  ${GREEN}✓${NC} Has manual trigger (workflow_dispatch)"
    else
        echo -e "  ${YELLOW}!${NC} No manual trigger"
        ((WARNINGS++))
    fi
    
    if grep -q "on:" "$file"; then
        echo -e "  ${GREEN}✓${NC} Has trigger definition"
    else
        echo -e "  ${RED}✗${NC} Missing trigger definition"
        ((ERRORS++))
    fi
}

check_workflow_jobs() {
    local file=$1
    
    if grep -q "jobs:" "$file"; then
        echo -e "  ${GREEN}✓${NC} Has jobs definition"
        local job_count=$(grep -c "^  [a-zA-Z_-]*:" "$file" || true)
        echo -e "  ${GREEN}✓${NC} Found approximately $job_count jobs"
    else
        echo -e "  ${RED}✗${NC} Missing jobs definition"
        ((ERRORS++))
    fi
}

check_timeout() {
    local file=$1
    
    if grep -q "timeout-minutes:" "$file"; then
        echo -e "  ${GREEN}✓${NC} Has timeout configuration"
    else
        echo -e "  ${YELLOW}!${NC} No timeout configured (recommended)"
        ((WARNINGS++))
    fi
}

check_matrix_strategy() {
    local file=$1
    
    if grep -q "strategy:" "$file" && grep -q "matrix:" "$file"; then
        echo -e "  ${GREEN}✓${NC} Uses matrix strategy"
    fi
}

check_artifact_upload() {
    local file=$1
    
    if grep -q "actions/upload-artifact@" "$file"; then
        echo -e "  ${GREEN}✓${NC} Uploads artifacts"
    fi
}

check_postgresql_versions() {
    local file=$1
    
    if grep -q "pg_version" "$file"; then
        echo -e "  ${GREEN}✓${NC} Supports PostgreSQL versioning"
    fi
}

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Checking Main Workflows"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

# Check build-matrix.yml
echo "1. build-matrix.yml"
if check_file "$WORKFLOWS_DIR/build-matrix.yml"; then
    check_yaml_syntax "$WORKFLOWS_DIR/build-matrix.yml"
    check_workflow_triggers "$WORKFLOWS_DIR/build-matrix.yml"
    check_workflow_jobs "$WORKFLOWS_DIR/build-matrix.yml"
    check_timeout "$WORKFLOWS_DIR/build-matrix.yml"
    check_matrix_strategy "$WORKFLOWS_DIR/build-matrix.yml"
    check_artifact_upload "$WORKFLOWS_DIR/build-matrix.yml"
    check_postgresql_versions "$WORKFLOWS_DIR/build-matrix.yml"
fi
echo

# Check build-packages.yml
echo "2. build-packages.yml"
if check_file "$WORKFLOWS_DIR/build-packages.yml"; then
    check_yaml_syntax "$WORKFLOWS_DIR/build-packages.yml"
    check_workflow_triggers "$WORKFLOWS_DIR/build-packages.yml"
    check_workflow_jobs "$WORKFLOWS_DIR/build-packages.yml"
    check_timeout "$WORKFLOWS_DIR/build-packages.yml"
    check_postgresql_versions "$WORKFLOWS_DIR/build-packages.yml"
fi
echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Checking Reusable Workflows"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

# Check reusable workflows
if [ -d "$WORKFLOWS_DIR/reusable" ]; then
    for workflow in "$WORKFLOWS_DIR/reusable"/*.yml; do
        if [ -f "$workflow" ]; then
            name=$(basename "$workflow")
            echo "3. reusable/$name"
            check_file "$workflow"
            check_yaml_syntax "$workflow"
            
            if grep -q "workflow_call:" "$workflow"; then
                echo -e "  ${GREEN}✓${NC} Is a reusable workflow"
            else
                echo -e "  ${YELLOW}!${NC} Missing workflow_call trigger"
                ((WARNINGS++))
            fi
            
            if grep -q "inputs:" "$workflow"; then
                echo -e "  ${GREEN}✓${NC} Defines inputs"
            fi
            
            check_workflow_jobs "$workflow"
            check_timeout "$workflow"
            echo
        fi
    done
else
    echo -e "${YELLOW}!${NC} No reusable workflows directory found"
    ((WARNINGS++))
fi

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Additional Checks"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

# Check for PostgreSQL repository setup
echo "4. PostgreSQL Repository Configuration"
if grep -r "postgresql.org" "$WORKFLOWS_DIR"/*.yml &>/dev/null; then
    echo -e "${GREEN}✓${NC} Uses official PostgreSQL repositories"
else
    echo -e "${RED}✗${NC} Missing PostgreSQL repository setup"
    ((ERRORS++))
fi

# Check for optimization flags
echo
echo "5. Build Optimizations"
if grep -r "DEBIAN_FRONTEND=noninteractive" "$WORKFLOWS_DIR"/*.yml &>/dev/null; then
    echo -e "${GREEN}✓${NC} DEB builds use noninteractive mode"
fi
if grep -r "man-db" "$WORKFLOWS_DIR"/*.yml &>/dev/null; then
    echo -e "${GREEN}✓${NC} man-db triggers disabled (faster builds)"
fi
if grep -r "deltarpm=0" "$WORKFLOWS_DIR"/*.yml &>/dev/null; then
    echo -e "${GREEN}✓${NC} RPM deltarpm disabled (faster builds)"
fi

# Check for security
echo
echo "6. Security Configuration"
if grep -r "permissions:" "$WORKFLOWS_DIR"/*.yml &>/dev/null; then
    echo -e "${GREEN}✓${NC} Has explicit permissions"
else
    echo -e "${YELLOW}!${NC} No explicit permissions (uses defaults)"
    ((WARNINGS++))
fi

# Check for test jobs
echo
echo "7. Testing Configuration"
if grep -r "test" "$WORKFLOWS_DIR"/*.yml | grep -q "name:"; then
    echo -e "${GREEN}✓${NC} Has test jobs"
else
    echo -e "${YELLOW}!${NC} No test jobs found"
    ((WARNINGS++))
fi

# Check for release automation
echo
echo "8. Release Automation"
if grep -r "release" "$WORKFLOWS_DIR"/*.yml | grep -q "name:"; then
    echo -e "${GREEN}✓${NC} Has release automation"
else
    echo -e "${YELLOW}!${NC} No release automation"
    ((WARNINGS++))
fi

# Summary
echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Summary"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✓ All checks passed!${NC}"
    echo
    echo "✓ Workflows are properly configured"
    echo "✓ All required files present"
    echo "✓ Best practices followed"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}! Verification completed with warnings${NC}"
    echo
    echo "Errors: $ERRORS"
    echo "Warnings: $WARNINGS"
    echo
    echo "Workflows are functional but could be improved."
    exit 0
else
    echo -e "${RED}✗ Verification failed${NC}"
    echo
    echo "Errors: $ERRORS"
    echo "Warnings: $WARNINGS"
    echo
    echo "Please fix errors before deploying workflows."
    exit 1
fi

