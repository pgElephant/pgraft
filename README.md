# pgraft — Raft-based PostgreSQL extension for leader election & high availability

[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-16+-blue.svg)](https://postgresql.org/)
[![Go](https://img.shields.io/badge/Go-1.21+-00ADD8.svg)](https://golang.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/f9bfcf1114f946059578c2efc0b6a2fb)](https://app.codacy.com/gh/pgElephant/pgraft/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)
[![Documentation](https://img.shields.io/badge/docs-latest-blue.svg)](https://pgelephant.github.io/pgraft/)

## Build Status

| Platform | PostgreSQL 16 | PostgreSQL 17 | PostgreSQL 18 |
|----------|---------------|---------------|---------------|
| **Ubuntu** | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-ubuntu-pg-16.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-ubuntu-pg-16.yml) | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-ubuntu-pg-17.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-ubuntu-pg-17.yml) | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-ubuntu-pg-18.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-ubuntu-pg-18.yml) |
| **macOS** | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-mac-pg-16.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-mac-pg-16.yml) | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-mac-pg-17.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-mac-pg-17.yml) | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-mac-pg-18.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-mac-pg-18.yml) |
| **Rocky** | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-rocky-pg-16.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-rocky-pg-16.yml) | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-rocky-pg-17.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-rocky-pg-17.yml) | [![Build](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-rocky-pg-18.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/pgraft-build-rocky-pg-18.yml) |

**pgraft** is a production-ready PostgreSQL extension that embeds the Raft consensus protocol to deliver automatic leader election, deterministic failover, crash-safe log replication, and 100% split-brain prevention for PostgreSQL clusters—powered by the proven etcd-io/raft library.

**Now part of the unified pgElephant high-availability suite.**
- Consistent UI and documentation across [pgraft](https://pgelephant.com/pgraft), [RAM](https://pgelephant.com/ram), [RALE](https://pgelephant.com/rale), and [FauxDB](https://pgelephant.com/fauxdb).
- All product pages use a single, professional template for a seamless experience.
- See the [website](https://pgelephant.com) for live demos and feature comparisons.

**Supported PostgreSQL versions**: 16, 17, 18

## Quick Links

- **[Documentation](https://pgelephant.github.io/pgraft/)** - Complete documentation site
- **[Quick Start Guide](https://pgelephant.github.io/pgraft/getting-started/quick-start/)** - Get running in minutes
- **[Architecture](https://pgelephant.github.io/pgraft/concepts/architecture/)** - How pgraft works
- **[SQL Functions](https://pgelephant.github.io/pgraft/user-guide/sql-functions/)** - Complete API reference
- **[Contributing](CONTRIBUTING.md)** - How to contribute

## Detailed Features List

- **Raft Consensus Engine**: Embedded etcd-io/raft for proven, production-grade consensus.
- **Automatic Leader Election**: Quorum-based, deterministic, and fully automated.
- **Crash-Safe Log Replication**: All state changes are replicated and persisted across nodes.
- **100% Split-Brain Prevention**: Mathematical guarantee—never more than one leader per term.
- **Zero-Downtime Failover**: Seamless failover with sub-second detection and recovery.
- **Leader-Driven Cluster Management**: Node addition/removal and configuration changes are always performed by the elected leader and automatically replicated.
- **Background Worker Architecture**: PostgreSQL C background worker drives Raft ticks and state transitions.
- **Persistent Storage**: HardState, log entries, and snapshots survive crashes and restarts.
- **Production-Ready Quality**: 0 compilation errors/warnings, 100% PostgreSQL C standards compliant, and comprehensive test coverage.
- **Observability**: Built-in monitoring functions, Prometheus metrics, and detailed logging.
- **Secure by Design**: Follows PostgreSQL security best practices; supports SSL/TLS and role-based access.
- **Unified UI & Documentation**: Consistent, professional product pages and documentation across the pgElephant suite.

## Installation

## Quick install (60 seconds)

Prerequisites: PostgreSQL 16–18 with server headers, make, gcc/clang

```bash
# from repo root
make
sudo make install
```

Enable in postgresql.conf and restart:

```conf
shared_preload_libraries = 'pgraft'
```

Create the extension:

```sql
CREATE EXTENSION pgraft;
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
- **[RAM](https://pgelephant.com/ram)** - PostgreSQL clustering and failover manager
- **[RALE](https://pgelephant.com/rale)** - Distributed consensus and key-value store
- **[FauxDB](https://pgelephant.com/fauxdb)** - MongoDB-compatible query proxy for PostgreSQL

## SEO/Discoverability keywords

PostgreSQL Raft, Postgres leader election, PostgreSQL automatic failover, PostgreSQL high availability, PostgreSQL clustering, Raft log replication, split‑brain prevention, Postgres background worker consensus, deterministic failover, HA Postgres on Kubernetes, distributed consensus for PostgreSQL, PostgreSQL Raft extension

---

Made with care for the PostgreSQL community
