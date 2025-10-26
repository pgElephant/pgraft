#!/bin/bash
#
# Validate all GitHub Actions workflows
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKFLOWS_DIR="$(dirname "$SCRIPT_DIR")/workflows"

echo "╔════════════════════════════════════════════════════════╗"
echo "║  GitHub Actions Workflow Validator                    ║"
echo "╚════════════════════════════════════════════════════════╝"
echo

cd "$WORKFLOWS_DIR"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

errors=0
warnings=0
validated=0

# Validate each workflow file
shopt -s nullglob
for workflow in *.yml reusable/*.yml; do
    if [ ! -f "$workflow" ]; then
        continue
    fi
    
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "📄 Validating: $workflow"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # Check 1: File exists and is readable
    if [ ! -r "$workflow" ]; then
        echo -e "${RED}✗${NC} File not readable"
        ((errors++))
        continue
    fi
    
    # Check 2: Has required fields
    if ! grep -q "^name:" "$workflow"; then
        echo -e "${RED}✗${NC} Missing 'name:' field"
        ((errors++))
    else
        name=$(grep "^name:" "$workflow" | head -1 | cut -d':' -f2- | xargs)
        echo -e "${GREEN}✓${NC} Name: $name"
    fi
    
    # Check 3: Has 'on:' trigger
    if ! grep -q "^on:" "$workflow"; then
        echo -e "${RED}✗${NC} Missing 'on:' trigger"
        ((errors++))
    else
        echo -e "${GREEN}✓${NC} Has trigger definition"
        
        # Check for manual trigger
        if grep -A 5 "^on:" "$workflow" | grep -q "workflow_dispatch\|workflow_call"; then
            echo -e "${GREEN}✓${NC} Manual trigger configured"
        else
            echo -e "${YELLOW}⚠${NC} No manual trigger (workflow_dispatch/workflow_call)"
            ((warnings++))
        fi
        
        # Check for automatic triggers
        if grep -A 10 "^on:" "$workflow" | grep -qE "push:|pull_request:|schedule:"; then
            echo -e "${YELLOW}⚠${NC} Has automatic triggers"
            ((warnings++))
        fi
    fi
    
    # Check 4: Has jobs
    if ! grep -q "^jobs:" "$workflow"; then
        echo -e "${RED}✗${NC} Missing 'jobs:' section"
        ((errors++))
    else
        job_count=$(grep -E "^  [a-z_-]+:" "$workflow" | wc -l)
        echo -e "${GREEN}✓${NC} Jobs defined: $job_count"
    fi
    
    # Check 5: Uses official actions
    if grep -q "uses:" "$workflow"; then
        third_party=$(grep "uses:" "$workflow" | grep -v "actions/" | grep -v "^\s*#" | grep -v "./.github" || true)
        if [ -n "$third_party" ]; then
            echo -e "${YELLOW}⚠${NC} Uses third-party actions:"
            echo "$third_party" | sed 's/^/    /'
            ((warnings++))
        else
            echo -e "${GREEN}✓${NC} Uses official actions only"
        fi
    fi
    
    # Check 6: YAML syntax
    if command -v python3 &> /dev/null; then
        if python3 -c "import yaml; yaml.safe_load(open('$workflow'))" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} Valid YAML syntax"
        else
            echo -e "${RED}✗${NC} Invalid YAML syntax"
            ((errors++))
        fi
    fi
    
    ((validated++))
    echo
done

# Summary
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📊 Validation Summary"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Workflows validated: $validated"
echo -e "  Errors: ${RED}$errors${NC}"
echo -e "  Warnings: ${YELLOW}$warnings${NC}"
echo

if [ $errors -eq 0 ]; then
    echo -e "${GREEN}✅ All workflows are valid!${NC}"
    exit 0
else
    echo -e "${RED}❌ Found $errors errors${NC}"
    exit 1
fi

