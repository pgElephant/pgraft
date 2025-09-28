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

# Set PostgreSQL 17.6 PATH
export PATH="/usr/local/pgsql.17/bin:$PATH"

# Activate virtual environment and run the script
source venv/bin/activate
python pgraft_cluster.py "$@"
