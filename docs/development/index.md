# Development Guide

Resources for developing, testing, and contributing to pgraft.

## Overview

This section is for developers who want to build pgraft from source, contribute code, or understand the internals.

## Contents

### Building from Source

Complete guide to building pgraft on your development machine.

[Building Guide](building.md){ .md-button .md-button--primary }

### Testing

How to test pgraft, including the test harness and creating test scenarios.

[Testing Guide](testing.md){ .md-button }

### Contributing

Guidelines for contributing code, documentation, and bug reports.

[Contributing Guide](contributing.md){ .md-button }

## Quick Start for Developers

### Clone and Build

```bash
# Clone repository
git clone https://github.com/pgelephant/pgraft.git
cd pgraft

# Build
make clean && make

# Install
make install

# Test
cd examples
./run.sh --destroy
./run.sh --init
./run.sh --status
```

### Development Environment

**Prerequisites:**
- PostgreSQL 17+ with development headers
- Go 1.21+
- GCC or Clang
- Make

**Recommended Tools:**
- Git
- Text editor with C and Go support
- gdb or lldb for debugging
- Valgrind for memory leak detection (Linux)

## Code Structure

### Repository Layout

```
pgraft/
├── src/                    # Source code
│   ├── pgraft.c           # Main extension entry
│   ├── pgraft_core.c      # Core Raft interface
│   ├── pgraft_sql.c       # SQL functions
│   ├── pgraft_guc.c       # Configuration
│   ├── pgraft_state.c     # State management
│   ├── pgraft_log.c       # Log replication
│   ├── pgraft_kv.c        # Key-value store
│   ├── pgraft_util.c      # Utilities
│   └── pgraft_go.go       # Go Raft implementation
├── include/               # Header files
│   ├── pgraft_core.h
│   ├── pgraft_go.h
│   ├── pgraft_guc.h
│   ├── pgraft_sql.h
│   ├── pgraft_state.h
│   ├── pgraft_log.h
│   └── pgraft_kv.h
├── examples/              # Test harness
├── docs/                  # Documentation
├── Makefile              # Build system
├── pgraft.control        # Extension control file
└── pgraft--1.0.sql       # SQL definitions
```

### Component Layers

**C Layer:**
- PostgreSQL integration
- Background worker
- SQL function interface
- Shared memory management

**Go Layer:**
- Raft consensus engine (etcd-io/raft)
- Network communication
- Log persistence
- Snapshot management

**Storage Layer:**
- HardState persistence
- Log entry storage
- Snapshot files

## Development Workflow

### 1. Make Changes

```bash
# Edit source files
vim src/pgraft_core.c
```

### 2. Build

```bash
# Clean build
make clean && make

# Check for errors
make 2>&1 | grep -i error

# Check for warnings
make 2>&1 | grep -i warning
```

### 3. Install

```bash
make install

# Or manual install
cp pgraft.dylib /usr/local/pgsql.17/lib/
cp src/pgraft_go.dylib /usr/local/pgsql.17/lib/
```

### 4. Test

```bash
# Restart PostgreSQL
pg_ctl restart -D /path/to/data

# Run tests
cd examples
./run.sh --destroy && ./run.sh --init
```

### 5. Debug

```bash
# Enable debug logging
psql -c "SELECT pgraft_set_debug(true);"

# Check logs
tail -f $PGDATA/log/postgresql-*.log | grep pgraft:
```

## Coding Standards

### C Code

**PostgreSQL C Standards:**
- C89/C90 compliance
- All variables at function start
- C-style comments only (`/* */`)
- Tab indentation (4 spaces)
- No warnings, no errors

**Example:**
```c
void my_function(void)
{
    int result;
    char *message;
    
    /* Initialize variables */
    result = 0;
    message = NULL;
    
    /* Function logic */
    elog(LOG, "pgraft: Function called");
}
```

### Go Code

**Standard Go conventions:**
- Use `gofmt` for formatting
- Add comments for exported functions
- Handle errors explicitly
- Follow Go idioms

**Example:**
```go
// ProcessRaftMessage handles incoming Raft protocol messages
func ProcessRaftMessage(msg raftpb.Message) error {
    if err := raftNode.Step(msg); err != nil {
        return fmt.Errorf("failed to process message: %w", err)
    }
    return nil
}
```

## Testing Guidelines

### Unit Tests

Write tests for new functionality:

```sql
-- Test new feature
SELECT new_function() = expected_result;
```

### Integration Tests

Test with the test harness:

```bash
cd examples
./run.sh --destroy
./run.sh --init
# Test your feature
```

### Regression Tests

Ensure existing functionality still works:

```bash
# Run full test suite
cd examples
./run.sh --destroy
./run.sh --init
./run.sh --status
# Check all nodes are healthy
```

## Debugging Tools

### GDB

```bash
# Start with debugger
gdb --args postgres -D /path/to/data

# Set breakpoints
(gdb) break pgraft_init
(gdb) run

# Debug
(gdb) backtrace
(gdb) print variable
(gdb) next
```

### Logging

```c
/* Add debug logging */
elog(LOG, "pgraft: Debug info: %d", value);
elog(WARNING, "pgraft: Warning: %s", message);
elog(ERROR, "pgraft: Error: %s", error);
```

### Memory Debugging

```bash
# Valgrind (Linux)
valgrind --leak-check=full postgres -D /path/to/data

# macOS Instruments
instruments -t "Leaks" postgres
```

## Contributing

### Before Submitting

- [ ] Code compiles without errors or warnings
- [ ] All tests pass
- [ ] Code follows style guidelines
- [ ] Documentation updated if needed
- [ ] Commit messages are clear

### Pull Request Process

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit pull request
6. Respond to review feedback

See [Contributing Guide](contributing.md) for details.

## Resources

- **Build instructions**: [Building from Source](building.md)
- **Test procedures**: [Testing Guide](testing.md)
- **Contribution guidelines**: [Contributing Guide](contributing.md)
- **Architecture details**: [Architecture](../concepts/architecture.md)

## Getting Help

- **Documentation**: This site
- **Issues**: [GitHub Issues](https://github.com/pgelephant/pgraft/issues)
- **Source**: [GitHub Repository](https://github.com/pgelephant/pgraft)

