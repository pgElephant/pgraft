# Building from Source

This guide covers building pgraft from source for development and contributing.

## Prerequisites

Ensure you have all required dependencies:

- **PostgreSQL 17+**: With development headers
- **Go 1.21+**: For Raft implementation
- **GCC**: C compiler
- **Make**: Build system

See [Installation](../getting-started/installation.md) for system-specific installation instructions.

## Build Process

### 1. Clone Repository

```bash
git clone https://github.com/pgelephant/pgraft.git
cd pgraft
```

### 2. Build

```bash
make clean
make
```

The build process:

1. **Compiles C sources** (`src/*.c`) to object files
2. **Builds Go library** (`src/pgraft_go.go`) to shared library
3. **Links everything** into final `pgraft.dylib` (or `.so` on Linux)
4. **Creates extension SQL** from `pgraft--1.0.sql`

### 3. Install

```bash
# Find PostgreSQL paths
PG_LIB=$(pg_config --libdir)
PG_SHARE=$(pg_config --sharedir)

# Install files
cp pgraft.dylib $PG_LIB/
cp src/pgraft_go.dylib $PG_LIB/
cp pgraft.control $PG_SHARE/extension/
cp pgraft--1.0.sql $PG_SHARE/extension/
```

## Build Targets

### Clean Build

```bash
make clean
make
```

### Install After Build

```bash
make install
```

### Build with Debugging

```bash
make clean
CFLAGS="-g -O0" make
```

This builds with:
- `-g`: Debug symbols
- `-O0`: No optimization

## Verifying Build

### Check for Errors

```bash
# Should have no errors
make 2>&1 | grep -i error

# Should have no warnings
make 2>&1 | grep -i warning
```

### Check Binary

```bash
# macOS
otool -L pgraft.dylib

# Linux
ldd pgraft.so

# Should show dependencies on libpq, PostgreSQL, etc.
```

### Test Extension

```bash
# Start PostgreSQL with extension
psql -c "CREATE EXTENSION pgraft;"
psql -c "SELECT pgraft_test();"
```

## Development Workflow

### Edit-Compile-Test Cycle

```bash
# 1. Edit source files
vim src/pgraft_core.c

# 2. Rebuild
make clean && make

# 3. Reinstall
make install

# 4. Restart PostgreSQL
pg_ctl restart -D /path/to/data

# 5. Test
psql -c "SELECT pgraft_test();"
```

### Incremental Builds

For faster development, you can rebuild only changed files:

```bash
# Rebuild only C files (if Go unchanged)
make pgraft.dylib

# Rebuild only Go (if C unchanged)
cd src
go build -buildmode=c-shared -o pgraft_go.dylib pgraft_go.go
```

## Code Organization

### C Source Files (`src/`)

| File | Purpose |
|------|---------|
| `pgraft.c` | Main extension entry point, background worker |
| `pgraft_core.c` | Core Raft interface to Go layer |
| `pgraft_sql.c` | SQL function implementations |
| `pgraft_guc.c` | GUC (configuration) parameters |
| `pgraft_state.c` | State management, shared memory |
| `pgraft_log.c` | Log replication functions |
| `pgraft_kv.c` | Key-value store implementation |
| `pgraft_kv_sql.c` | KV SQL interface |
| `pgraft_util.c` | Utility functions |
| `pgraft_go.c` | CGO wrapper for Go library |

### Header Files (`include/`)

| File | Purpose |
|------|---------|
| `pgraft_core.h` | Core function declarations |
| `pgraft_go.h` | Go library interface |
| `pgraft_guc.h` | GUC declarations |
| `pgraft_sql.h` | SQL function declarations |
| `pgraft_state.h` | State structures |
| `pgraft_log.h` | Log function declarations |
| `pgraft_kv.h` | KV store declarations |

### Go Implementation

- `src/pgraft_go.go` - Complete Raft implementation (2900+ lines)

## Coding Standards

pgraft follows **PostgreSQL C coding standards**:

### C89/C90 Compliance

```c
/* Correct: Variables at function start */
void my_function(void)
{
    int result;
    char *message;
    
    result = 0;
    message = "Hello";
    /* ... function code ... */
}

/* Wrong: Variables declared in middle */
void bad_function(void)
{
    int result = 0;
    /* some code */
    char *message = "Hello";  /* NOT at start */
}
```

### Comments

```c
/* Correct: C-style comments */
/* This is a proper comment */

// Wrong: C++ style comments
// This is not allowed
```

### Indentation

- **Tabs only** (not spaces)
- Tab width: 4 spaces
- Opening brace on same line:

```c
/* Correct */
if (condition)
{
    /* code */
}

/* Wrong */
if (condition) {
    /* code */
}
```

## Testing

### Unit Tests

```bash
cd examples
./run.sh --destroy
./run.sh --init
./run.sh --status
```

### Manual Testing

```sql
-- Test basic functionality
SELECT pgraft_test();
SELECT pgraft_init();
SELECT pgraft_is_leader();

-- Test cluster operations
SELECT pgraft_add_node(2, '127.0.0.1', 7002);
SELECT * FROM pgraft_get_cluster_status();
```

## Debugging

### Enable Debug Output

```sql
SELECT pgraft_set_debug(true);
```

### Check Logs

```bash
tail -f $PGDATA/log/postgresql-*.log | grep pgraft
```

### Use GDB

```bash
# Start PostgreSQL with gdb
gdb --args postgres -D /path/to/data

# Set breakpoints
(gdb) break pgraft_init
(gdb) run

# When breakpoint hit
(gdb) backtrace
(gdb) print variable_name
```

### Common Debug Points

```c
/* Add logging in C code */
elog(LOG, "pgraft: Debug point reached, value=%d", value);

/* Add error logging */
elog(ERROR, "pgraft: Error occurred: %s", error_message);
```

## Contributing

See [Contributing](contributing.md) for guidelines on:
- Code style
- Pull request process
- Testing requirements
- Documentation

## Performance Profiling

### CPU Profiling

```bash
# Use perf on Linux
perf record -g postgres
perf report

# Use Instruments on macOS
instruments -t "Time Profiler" postgres
```

### Memory Profiling

```bash
# Valgrind
valgrind --leak-check=full postgres -D /path/to/data

# macOS Instruments
instruments -t "Leaks" postgres
```

## Troubleshooting Build Issues

### PostgreSQL Headers Not Found

```bash
# Set PG_CONFIG explicitly
export PG_CONFIG=/usr/local/pgsql/bin/pg_config
make clean && make
```

### Go Build Fails

```bash
# Ensure Go modules are downloaded
cd src
go mod download
go mod tidy
```

### Link Errors

```bash
# Check library paths
export DYLD_LIBRARY_PATH=/usr/local/pgsql/lib  # macOS
export LD_LIBRARY_PATH=/usr/local/pgsql/lib    # Linux
```

## Build System Details

The `Makefile` uses PostgreSQL's PGXS build system:

```makefile
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
```

This automatically handles:
- Finding PostgreSQL headers
- Setting correct compiler flags
- Linking with PostgreSQL libraries
- Installing to correct directories

