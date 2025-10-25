#!/bin/bash
# run.sh - Convenience script to run pgraft_cluster.py with virtual environment

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Change to the script directory
cd "$SCRIPT_DIR"

# Check if virtual environment exists
if [ ! -d "venv" ]; then
    echo "Virtual environment not found. Running setup..."
    ./setup.sh
fi

# Set PostgreSQL 17.6 PATH and library paths
export PATH="/usr/local/pgsql.17/bin:$PATH"
export DYLD_LIBRARY_PATH="/usr/local/pgsql.17/lib:/opt/homebrew/lib:$DYLD_LIBRARY_PATH"

# Activate virtual environment and run the script
source venv/bin/activate
python3 pgraft_cluster.py "$@"
