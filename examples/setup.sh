#!/bin/bash
# setup.sh - Setup script for pgraft cluster management

set -e

echo "Setting up PostgreSQL pgraft Cluster Management..."

# Check if Python 3 is available
if ! command -v python3 &> /dev/null; then
    echo "Error: Python 3 is required but not installed."
    exit 1
fi

# Check if PostgreSQL is available
echo "Checking PostgreSQL installation..."

# Check for pg_ctl
if ! command -v pg_ctl &> /dev/null; then
    echo "Error: PostgreSQL pg_ctl command not found in PATH."
    echo "Please install PostgreSQL and ensure pg_ctl is in your PATH."
    echo "Common PostgreSQL installation paths:"
    echo "  - /usr/local/pgsql/bin/pg_ctl"
    echo "  - /opt/homebrew/bin/pg_ctl"
    echo "  - /usr/bin/pg_ctl"
    echo "  - /usr/pgsql-*/bin/pg_ctl"
    exit 1
fi

# Check for initdb
if ! command -v initdb &> /dev/null; then
    echo "Error: PostgreSQL initdb command not found in PATH."
    echo "Please install PostgreSQL and ensure initdb is in your PATH."
    echo "Common PostgreSQL installation paths:"
    echo "  - /usr/local/pgsql/bin/initdb"
    echo "  - /opt/homebrew/bin/initdb"
    echo "  - /usr/bin/initdb"
    echo "  - /usr/pgsql-*/bin/initdb"
    exit 1
fi

# Check for psql
if ! command -v psql &> /dev/null; then
    echo "Error: PostgreSQL psql command not found in PATH."
    echo "Please install PostgreSQL and ensure psql is in your PATH."
    exit 1
fi

# Display PostgreSQL version and path
echo "✓ PostgreSQL found:"
echo "  pg_ctl: $(which pg_ctl)"
echo "  initdb: $(which initdb)"
echo "  psql: $(which psql)"
echo "  Version: $(pg_ctl --version)"

# Create virtual environment if it doesn't exist
if [ ! -d "venv" ]; then
    echo "Creating Python virtual environment..."
    python3 -m venv venv
fi

# Activate virtual environment and install dependencies
echo "Installing Python dependencies..."
source venv/bin/activate
pip install -r requirements.txt

echo "Setup completed successfully!"
echo ""
echo "To use the pgraft cluster management script:"
echo "1. Activate the virtual environment: source venv/bin/activate"
echo "2. Run the script: python pgraft_cluster.py --help"
echo ""
echo "Or use the convenience script: ./run.sh --help"
