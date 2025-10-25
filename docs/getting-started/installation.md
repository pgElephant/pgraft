
# Installation (pgElephant Suite)

This guide will walk you through installing **pgraft**, part of the unified [pgElephant](https://pgelephant.com) high-availability suite. All steps and troubleshooting are up to date for the latest release.

## Prerequisites

Before installing pgraft, ensure you have the following:

- **PostgreSQL**: Version 17.6 or higher
- **Go**: Version 1.21 or higher
- **GCC**: C compiler
- **PostgreSQL development headers**

### System-Specific Prerequisites

=== "Ubuntu/Debian"
    ```bash
    sudo apt-get update
    sudo apt-get install postgresql-17 postgresql-server-dev-17 golang-go build-essential
    ```

=== "CentOS/RHEL"
    ```bash
    sudo yum install postgresql17 postgresql17-devel golang gcc make
    ```

=== "macOS"
    ```bash
    brew install postgresql@17 go
    ```

## Build from Source

### 1. Clone the Repository

```bash
git clone https://github.com/pgelephant/pgraft.git
cd pgraft
```

### 2. Build the Extension

```bash
make clean
make
```

!!! tip "Check for Errors"
    You can verify the build completed without errors:
    ```bash
    make 2>&1 | grep -i error
    make 2>&1 | grep -i warning
    ```

### 3. Install the Extension

```bash
# Manual installation
cp pgraft.dylib /usr/local/pgsql.17/lib/
cp src/pgraft_go.dylib /usr/local/pgsql.17/lib/
cp pgraft.control /usr/local/pgsql.17/share/extension/
cp pgraft--1.0.sql /usr/local/pgsql.17/share/extension/
```

!!! warning "Path Configuration"
    Adjust the paths above based on your PostgreSQL installation location. Common paths:
    - macOS (Homebrew): `/usr/local/opt/postgresql@17/`
    - Linux: `/usr/lib/postgresql/17/`
    - Custom: Use `pg_config --libdir` and `pg_config --sharedir`

## Verify Installation

After installation, verify that pgraft is properly installed:

```bash
# Check that the extension files exist
ls -l $(pg_config --libdir)/pgraft*.dylib
ls -l $(pg_config --sharedir)/extension/pgraft*
```

## Next Steps

Now that pgraft is installed, you can:

- [Set up your first cluster](quick-start.md)
- [Learn about configuration options](../user-guide/configuration.md)
- [Follow the complete tutorial](../user-guide/tutorial.md)

## Troubleshooting

### PostgreSQL Development Headers Not Found

If you get errors about missing PostgreSQL headers during build:

```bash
# Ubuntu/Debian
sudo apt-get install postgresql-server-dev-17

# CentOS/RHEL
sudo yum install postgresql17-devel
```

### Go Not Found

Ensure Go is installed and in your PATH:

```bash
go version  # Should show Go 1.21 or higher
```

### Permission Denied

If you get permission errors during installation:

```bash
# Use sudo for copying to system directories
sudo cp pgraft.dylib /usr/local/pgsql.17/lib/
```

