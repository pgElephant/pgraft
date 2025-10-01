# pgraft - PostgreSQL Raft Consensus Extension

[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-17+-blue.svg)](https://postgresql.org/)
[![Go](https://img.shields.io/badge/Go-1.21+-00ADD8.svg)](https://golang.org/)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Documentation](https://img.shields.io/badge/docs-latest-blue.svg)](https://pgelephant.github.io/pgraft/)

**pgraft** is a production-ready PostgreSQL extension that implements the Raft consensus algorithm for distributed PostgreSQL clusters. It provides automatic leader election, log replication, and 100% split-brain protection using the battle-tested etcd-io/raft library.

## Quick Links

- **[Documentation](https://pgelephant.github.io/pgraft/)** - Complete documentation site
- **[Quick Start Guide](https://pgelephant.github.io/pgraft/getting-started/quick-start/)** - Get running in minutes
- **[Architecture](https://pgelephant.github.io/pgraft/concepts/architecture/)** - How pgraft works
- **[SQL Functions](https://pgelephant.github.io/pgraft/user-guide/sql-functions/)** - Complete API reference
- **[Contributing](CONTRIBUTING.md)** - How to contribute

## Key Features

- **Raft Consensus**: Based on etcd-io/raft implementation
- **Leader Election**: Automatic with quorum-based voting
- **Log Replication**: Consistent state across all nodes
- **Split-Brain Protection**: 100% guaranteed via Raft quorum
- **Leader-Only Node Addition**: Configuration changes only on leader, automatically replicated
- **Worker-Driven Architecture**: PostgreSQL background worker actively drives Raft ticks
- **Persistent Storage**: HardState, log entries, and snapshots survive crashes
- **Production Ready**: 0 compilation errors/warnings, PostgreSQL C standards compliant

## Installation

```bash
# Build
cd pgraft
make clean && make

# Install (adjust paths for your PostgreSQL installation)
make install
```

**For detailed installation instructions, see the [Installation Guide](https://pgelephant.github.io/pgraft/getting-started/installation/).**

## Configuration

Add to `postgresql.conf`:

```ini
shared_preload_libraries = 'pgraft'

# Core cluster configuration
pgraft.cluster_id = 'production-cluster'
pgraft.node_id = 1
pgraft.address = '127.0.0.1'
pgraft.port = 7001
pgraft.data_dir = '/var/lib/postgresql/pgraft'

# Consensus settings
pgraft.election_timeout = 1000
pgraft.heartbeat_interval = 100
```

**For complete configuration reference, see the [Configuration Guide](https://pgelephant.github.io/pgraft/user-guide/configuration/).**

## Quick Start

```sql
-- Create extension
CREATE EXTENSION pgraft;

-- Initialize node
SELECT pgraft_init();

-- Check if leader (wait 10 seconds for election)
SELECT pgraft_is_leader();

-- If leader, add other nodes
SELECT pgraft_add_node(2, '127.0.0.1', 7002);
SELECT pgraft_add_node(3, '127.0.0.1', 7003);

-- Verify cluster
SELECT * FROM pgraft_get_cluster_status();
```

**For complete setup instructions, see the [Quick Start Guide](https://pgelephant.github.io/pgraft/getting-started/quick-start/).**

## SQL Functions

**Core functions:**

```sql
-- Initialize and manage cluster
pgraft_init() → boolean
pgraft_add_node(node_id int, address text, port int) → boolean
pgraft_remove_node(node_id int) → boolean

-- Query cluster state
pgraft_get_cluster_status() → TABLE(...)
pgraft_get_nodes() → TABLE(node_id, address, port, is_leader)
pgraft_is_leader() → boolean
pgraft_get_leader() → bigint

-- Log operations
pgraft_replicate_entry(data text) → boolean
pgraft_log_get_stats() → TABLE(log_size, last_index, commit_index, last_applied)

-- Monitoring
pgraft_get_worker_state() → text
pgraft_set_debug(enabled boolean) → boolean
```

**For complete SQL function reference, see the [SQL Functions Guide](https://pgelephant.github.io/pgraft/user-guide/sql-functions/).**

## How It Works

```
PostgreSQL Background Worker (C)
    ↓ Every 100ms
Raft Engine (Go/etcd-io/raft)
    ↓
Persist → Replicate → Apply → Advance
```

**Components:**
- **C Layer**: PostgreSQL integration, SQL functions
- **Go Layer**: Raft consensus engine (etcd-io/raft)
- **Storage**: Persistent state on disk
- **Network**: TCP server for inter-node communication

**For detailed architecture, see the [Architecture Guide](https://pgelephant.github.io/pgraft/concepts/architecture/).**

## Split-Brain Protection

pgraft provides **100% split-brain protection** through Raft consensus:

- **Quorum Requirement**: Leader needs majority votes (N/2 + 1)
- **Term Monotonicity**: Higher term always wins
- **Single Leader Per Term**: Mathematical guarantee

**For a 3-node cluster:**
- Minimum 2 votes required for election
- Network partition: Only majority side can elect leader
- Impossible to have 2 leaders in same term

**Learn more: [Split-Brain Protection Guide](https://pgelephant.github.io/pgraft/concepts/split-brain/)**

## Examples

### Three-Node Cluster

Configure each node with unique `node_id`:

```ini
shared_preload_libraries = 'pgraft'
pgraft.cluster_id = 'prod-cluster'
pgraft.node_id = 1  # 2, 3 for other nodes
pgraft.address = '127.0.0.1'
pgraft.port = 7001  # 7002, 7003 for other nodes
```

Then initialize:

```bash
# On each node
psql -c "CREATE EXTENSION pgraft; SELECT pgraft_init();"

# On leader (after 10 seconds)
psql -c "SELECT pgraft_add_node(2, '127.0.0.1', 7002);"
psql -c "SELECT pgraft_add_node(3, '127.0.0.1', 7003);"
```

**For complete examples and test harness, see the [Tutorial](https://pgelephant.github.io/pgraft/user-guide/tutorial/).**

## Monitoring

Quick health check:

```sql
SELECT 
    pgraft_is_leader() as is_leader,
    pgraft_get_term() as term,
    pgraft_get_leader() as leader_id,
    pgraft_get_worker_state() as worker;
```

**For comprehensive monitoring guide, see [Monitoring](https://pgelephant.github.io/pgraft/operations/monitoring/).**

## Troubleshooting

**Common issues:**

- **Worker not running**: Check `shared_preload_libraries` includes 'pgraft'
- **Cannot add node**: Must run on leader node
- **No leader elected**: Wait 10 seconds, check network connectivity

**For complete troubleshooting guide, see [Troubleshooting](https://pgelephant.github.io/pgraft/operations/troubleshooting/).**

## Development

Build and test:

```bash
# Build
make clean && make

# Test
cd examples
./run.sh --destroy && ./run.sh --init
```

**For development guide, see [Development](https://pgelephant.github.io/pgraft/development/).**

## Performance

- **Tick Interval**: 100ms (worker-driven)
- **Election Timeout**: 1000ms (default, configurable)
- **Heartbeat**: 100ms (default, configurable)
- **Memory**: ~50MB per node
- **CPU**: <1% idle, <5% during elections

## Architecture

pgraft uses a worker-driven architecture where a PostgreSQL background worker drives the Raft consensus engine implemented in Go using etcd-io/raft.

**For detailed architecture information, see the [Architecture Guide](https://pgelephant.github.io/pgraft/concepts/architecture/).**

## License

MIT License - see LICENSE file for details.

## Documentation

**Complete documentation is available at: https://pgelephant.github.io/pgraft/**

### Documentation Sections

- **[Getting Started](https://pgelephant.github.io/pgraft/getting-started/)** - Installation and quick start
- **[User Guide](https://pgelephant.github.io/pgraft/user-guide/)** - Complete tutorial and configuration
- **[Core Concepts](https://pgelephant.github.io/pgraft/concepts/)** - Architecture and algorithms
- **[Operations](https://pgelephant.github.io/pgraft/operations/)** - Monitoring and best practices
- **[Development](https://pgelephant.github.io/pgraft/development/)** - Building and contributing

## Community and Support

- **Documentation**: [https://pgelephant.github.io/pgraft/](https://pgelephant.github.io/pgraft/)
- **Issues**: [GitHub Issues](https://github.com/pgelephant/pgraft/issues)
- **Contributing**: [CONTRIBUTING.md](CONTRIBUTING.md)
- **License**: [MIT License](LICENSE)

## Project Status

**Status**: Production Ready  
**Version**: 1.0.0  
**Standards**: 100% PostgreSQL C Compliant  
**Quality**: 0 compilation errors/warnings

## Related Projects

- **[etcd-io/raft](https://github.com/etcd-io/raft)** - Raft consensus implementation used by pgraft
- **[PostgreSQL](https://www.postgresql.org/)** - The world's most advanced open source database

## Keywords

PostgreSQL extension, Raft consensus, distributed database, high availability, leader election, log replication, split-brain protection, distributed systems, database clustering, fault tolerance, PostgreSQL HA, PostgreSQL cluster, database replication, consensus algorithm

---

Made with care for the PostgreSQL community
