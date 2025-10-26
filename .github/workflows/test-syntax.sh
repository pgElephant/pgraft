#!/bin/bash
# Quick workflow syntax check

echo "Checking workflow syntax..."
echo

for workflow in .github/workflows/*.yml; do
    name=$(basename "$workflow")
    echo -n "  $name ... "
    
    # Check for basic YAML structure
    if grep -q "^name:" "$workflow" && \
       grep -q "^on:" "$workflow" && \
       grep -q "^jobs:" "$workflow"; then
        echo "✓"
    else
        echo "✗ Missing required fields"
        exit 1
    fi
done

echo
echo "✓ All workflows have valid structure"
