# pgraft

**Raft consensus extension for PostgreSQL**

[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-16%20|%2017%20|%2018-blue.svg)](https://postgresql.org/)
[![Go](https://img.shields.io/badge/Go-1.21+-00ADD8.svg)](https://golang.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Build Matrix](https://github.com/pgelephant/pgraft/actions/workflows/build-matrix.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/build-matrix.yml)
[![Build Packages](https://github.com/pgelephant/pgraft/actions/workflows/build-packages.yml/badge.svg)](https://github.com/pgelephant/pgraft/actions/workflows/build-packages.yml)
[![Documentation](https://img.shields.io/badge/docs-latest-blue.svg)](https://pgelephant.github.io/pgraft/)

pgraft embeds the Raft consensus protocol into PostgreSQL, providing automatic leader election, crash-safe log replication, and 100% split-brain prevention for distributed PostgreSQL clusters.

## Features

- **Automatic Leader Election** — Quorum-based, deterministic, fully automated
- **Crash-Safe Replication** — All state changes replicated and persisted
- **Split-Brain Prevention** — Mathematical guarantee via Raft consensus
- **etcd-io/raft Integration** — Production-proven consensus library
- **Background Worker Architecture** — Native PostgreSQL integration
- **SQL Functions** — Full cluster management via SQL
- **Observability** — Built-in monitoring, Prometheus metrics, detailed logging

**Supported versions:** PostgreSQL 16, 17, 18 | Platforms: Linux, macOS

📚 **[Complete Documentation](https://pgelephant.github.io/pgraft/)**

---

## Quick Start

### 1. Install

#### From Source
```bash
# Prerequisites: PostgreSQL 16-18, Go 1.21+, json-c
make
sudo make install
```

#### RPM (RHEL/Rocky/AlmaLinux 9)
```bash
# Download from releases
sudo dnf install pgraft_17-1.0.0-1.el9.x86_64.rpm
```

#### DEB (Ubuntu/Debian)
```bash
# Download from releases
sudo apt install ./postgresql-17-pgraft_1.0.0-1_amd64.deb
```

📦 **[Download Packages](https://github.com/pgelephant/pgraft/releases)**

### 2. Configure

Add to `postgresql.conf`:

```ini
shared_preload_libraries = 'pgraft'

# Cluster configuration (required)
pgraft.cluster_id = 'prod-cluster'      # Same on all nodes
pgraft.node_id = 1                      # Unique per node (1, 2, 3, ...)
pgraft.address = '192.168.1.101'        # This node's address
pgraft.port = 7001                      # Raft port (not PostgreSQL port)
pgraft.data_dir = '/var/lib/postgresql/pgraft'

# Timing configuration (optional)
pgraft.election_timeout = 1000          # milliseconds
pgraft.heartbeat_interval = 100         # milliseconds
```

**Configuration notes:**
- `cluster_id` must be identical on all nodes
- `node_id` must be unique (typically 1, 2, 3, ...)
- `address` is the IP/hostname other nodes use to reach this node
- `port` is for Raft traffic (separate from PostgreSQL port 5432)
- `data_dir` stores Raft state (logs, snapshots)

Restart PostgreSQL after configuration.

### 3. Initialize

On each node:
```sql
CREATE EXTENSION pgraft;
SELECT pgraft_init();
```

Wait 10 seconds for leader election, then on the leader:
```sql
-- Add other nodes (run on leader only)
SELECT pgraft_add_node(2, '192.168.1.102', 7002);
SELECT pgraft_add_node(3, '192.168.1.103', 7003);

-- Verify cluster
SELECT * FROM pgraft_get_cluster_status();
```

---

## Usage

### Check Cluster Status
```sql
-- Am I the leader?
SELECT pgraft_is_leader();

-- Who is the leader?
SELECT pgraft_get_leader();

-- Full cluster status
SELECT * FROM pgraft_get_cluster_status();
```

### Manage Nodes
```sql
-- Add node (leader only)
SELECT pgraft_add_node(node_id, 'address', port);

-- Remove node (leader only)
SELECT pgraft_remove_node(node_id);

-- List all nodes
SELECT * FROM pgraft_get_nodes();
```

### Monitor Health
```sql
SELECT 
    pgraft_is_leader() as is_leader,
    pgraft_get_term() as term,
    pgraft_get_leader() as leader_id,
    pgraft_get_worker_state() as worker;
```

📖 **[SQL Functions Reference](https://pgelephant.github.io/pgraft/user-guide/sql-functions/)**

---

## Architecture

```
PostgreSQL Process
├── Background Worker (C)
│   └── Tick every 100ms
│       └── Go Raft Engine (etcd-io/raft)
│           ├── Leader Election
│           ├── Log Replication
│           └── Persistent State
└── SQL Functions (C)
    └── Cluster Management API
```

**Components:**
- **C Layer** — PostgreSQL integration, SQL functions, background worker
- **Go Layer** — Raft consensus engine (etcd-io/raft library)
- **Storage** — Persistent logs, snapshots, HardState on disk
- **Network** — TCP server for inter-node Raft communication

📐 **[Architecture Guide](https://pgelephant.github.io/pgraft/concepts/architecture/)**

---

## Configuration Examples

### 3-Node Local Cluster (Testing)

**Node 1** (`postgresql.conf`):
```ini
port = 5432
shared_preload_libraries = 'pgraft'
pgraft.cluster_id = 'test-cluster'
pgraft.node_id = 1
pgraft.address = '127.0.0.1'
pgraft.port = 7001
pgraft.data_dir = '/tmp/pgraft/node1'
```

**Node 2** (`postgresql.conf`):
```ini
port = 5433
shared_preload_libraries = 'pgraft'
pgraft.cluster_id = 'test-cluster'
pgraft.node_id = 2
pgraft.address = '127.0.0.1'
pgraft.port = 7002
pgraft.data_dir = '/tmp/pgraft/node2'
```

**Node 3** (`postgresql.conf`):
```ini
port = 5434
shared_preload_libraries = 'pgraft'
pgraft.cluster_id = 'test-cluster'
pgraft.node_id = 3
pgraft.address = '127.0.0.1'
pgraft.port = 7003
pgraft.data_dir = '/tmp/pgraft/node3'
```

### 3-Node Production Cluster

**Node 1** (`postgresql.conf`):
```ini
shared_preload_libraries = 'pgraft'
pgraft.cluster_id = 'prod-cluster'
pgraft.node_id = 1
pgraft.address = '10.0.1.11'
pgraft.port = 7001
pgraft.data_dir = '/var/lib/postgresql/pgraft'
pgraft.election_timeout = 1000
pgraft.heartbeat_interval = 100
```

**Node 2** (`postgresql.conf`):
```ini
shared_preload_libraries = 'pgraft'
pgraft.cluster_id = 'prod-cluster'
pgraft.node_id = 2
pgraft.address = '10.0.1.12'
pgraft.port = 7002
pgraft.data_dir = '/var/lib/postgresql/pgraft'
pgraft.election_timeout = 1000
pgraft.heartbeat_interval = 100
```

**Node 3** (`postgresql.conf`):
```ini
shared_preload_libraries = 'pgraft'
pgraft.cluster_id = 'prod-cluster'
pgraft.node_id = 3
pgraft.address = '10.0.1.13'
pgraft.port = 7003
pgraft.data_dir = '/var/lib/postgresql/pgraft'
pgraft.election_timeout = 1000
pgraft.heartbeat_interval = 100
```

**Setup:**
```bash
# On each node
psql -c "CREATE EXTENSION pgraft; SELECT pgraft_init();"

# Wait 10 seconds, then on leader (check with: SELECT pgraft_is_leader();)
psql -c "SELECT pgraft_add_node(2, '10.0.1.12', 7002);"
psql -c "SELECT pgraft_add_node(3, '10.0.1.13', 7003);"

# Verify
psql -c "SELECT * FROM pgraft_get_cluster_status();"
```

🔧 **[Configuration Guide](https://pgelephant.github.io/pgraft/user-guide/configuration/)**

---

## Documentation

**Complete documentation:** [https://pgelephant.github.io/pgraft/](https://pgelephant.github.io/pgraft/)

- **[Quick Start](https://pgelephant.github.io/pgraft/getting-started/quick-start/)** — Get running in 5 minutes
- **[Installation](https://pgelephant.github.io/pgraft/getting-started/installation/)** — Detailed install guide
- **[Configuration](https://pgelephant.github.io/pgraft/user-guide/configuration/)** — All GUC parameters
- **[SQL Functions](https://pgelephant.github.io/pgraft/user-guide/sql-functions/)** — Complete API reference
- **[Architecture](https://pgelephant.github.io/pgraft/concepts/architecture/)** — How pgraft works
- **[Split-Brain Protection](https://pgelephant.github.io/pgraft/concepts/split-brain/)** — Raft consensus explained
- **[Monitoring](https://pgelephant.github.io/pgraft/operations/monitoring/)** — Health checks and metrics
- **[Best Practices](https://pgelephant.github.io/pgraft/operations/best-practices/)** — Production deployment
- **[Troubleshooting](https://pgelephant.github.io/pgraft/operations/troubleshooting/)** — Common issues

---

## Building from Source

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt install build-essential postgresql-server-dev-17 golang-go libjson-c-dev pkg-config

# RHEL/Rocky/AlmaLinux
sudo dnf install gcc make postgresql17-devel golang json-c-devel pkg-config

# macOS
brew install postgresql@17 go json-c
```

### Build
```bash
git clone https://github.com/pgelephant/pgraft.git
cd pgraft
make
sudo make install
```

### Test
```bash
# Run test cluster
cd examples
./pgraft_cluster.py --init

# Verify
psql -p 5432 -c "SELECT * FROM pgraft_get_cluster_status();"
```

🛠️ **[Development Guide](https://pgelephant.github.io/pgraft/development/)**

---

## Performance

- **Tick Interval:** 100ms (background worker)
- **Election Timeout:** 1000ms (default, configurable)
- **Heartbeat Interval:** 100ms (default, configurable)
- **Memory:** ~50MB per node
- **CPU:** <1% idle, <5% during elections
- **Network:** ~1KB/s per node (heartbeats)

---

## Troubleshooting

### Worker Not Running
```sql
-- Check if pgraft is loaded
SHOW shared_preload_libraries;  -- Must include 'pgraft'

-- Check worker status
SELECT pgraft_get_worker_state();
```

### No Leader Elected
```bash
# Wait 10 seconds after init
sleep 10

# Check leader status
psql -c "SELECT pgraft_get_leader();"

# Check logs
tail -f /var/log/postgresql/postgresql-*.log | grep pgraft
```

### Cannot Add Node
```sql
-- Must run on leader
SELECT pgraft_is_leader();  -- Should return true

-- Check configuration matches
SHOW pgraft.cluster_id;
SHOW pgraft.node_id;
```

⚠️ **[Full Troubleshooting Guide](https://pgelephant.github.io/pgraft/operations/troubleshooting/)**

---

## Contributing

We welcome contributions! See **[CONTRIBUTING.md](CONTRIBUTING.md)** for guidelines.

**Quick links:**
- [Report a bug](https://github.com/pgelephant/pgraft/issues/new?labels=bug)
- [Request a feature](https://github.com/pgelephant/pgraft/issues/new?labels=enhancement)
- [Ask a question](https://github.com/pgelephant/pgraft/discussions)

---

## License

MIT License — See [LICENSE](LICENSE) file for details.

---

## Project Status

✅ **Production Ready**

- **Version:** 1.0.0
- **Standards:** 100% PostgreSQL C compliant
- **Quality:** 0 compilation errors/warnings
- **Testing:** Comprehensive test suite
- **Documentation:** Complete user and developer guides

---

## Related Projects

- [etcd-io/raft](https://github.com/etcd-io/raft) — Raft consensus implementation
- [PostgreSQL](https://www.postgresql.org/) — World's most advanced open source database

---

**Made with ❤️ for the PostgreSQL community**
