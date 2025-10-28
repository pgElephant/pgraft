# pgraft Cluster Management Script

The `pgraft_cluster.py` script provides an easy way to set up and manage a Docker-based pgraft cluster with PostgreSQL streaming replication.

---

## Overview

The script creates a PostgreSQL cluster with:

- **1 Primary node** - Read-write PostgreSQL with pgraft
- **2 Replica nodes** - Streaming replicas with pgraft
- **Raft consensus** - All 3 nodes participate in Raft for leader election and consensus
- **Docker networking** - Isolated network for cluster communication

### Architecture

```
┌─────────────┐     Streaming      ┌─────────────┐
│  PRIMARY    │◄───Replication─────│  REPLICA1   │
│  (Node 1)   │                     │  (Node 2)   │
│  pgraft     │                     │  pgraft     │
└──────┬──────┘                     └──────┬──────┘
       │                                    │
       │         Raft Consensus             │
       │       (Leader Election)            │
       │                                    │
       └──────────┬────────────────────────┘
                  │
           ┌──────┴──────┐
           │  REPLICA2   │
           │  (Node 3)   │
           │  pgraft     │
           └─────────────┘
```

---

## Prerequisites

- **Docker** installed and running
- **Python 3** (3.7 or higher)
- **Network port availability**: 5432-5434 (PostgreSQL), 7001-7003 (Raft)

---

## Installation

The script is located in the `examples/` directory:

```bash
cd pgraft/examples
chmod +x pgraft_cluster.py
```

---

## Usage

### Initialize a 3-Node Cluster

```bash
./pgraft_cluster.py --docker --init --nodes 3
```

This command will:

1. Build a Docker image with PostgreSQL 17 and pgraft
2. Create a Docker network (`pgraft-network`)
3. Start 3 PostgreSQL containers (1 primary + 2 replicas)
4. Configure streaming replication
5. Initialize pgraft on all nodes
6. Wait for Raft leader election

**Expected Output:**

```
✓ Initializing 1 primary + 2 replicas with pgraft Raft cluster...
✓ Creating Docker network...
✓ Building Docker image...
✓ Starting primary node (pgraft-primary)...
✓ Configuring replica pgraft-replica1...
✓ Configuring replica pgraft-replica2...
✓ Waiting for Raft leader election...
✓ Cluster initialized successfully!
```

---

### Check Cluster Status

```bash
./pgraft_cluster.py --docker --status
```

Shows:

- Container status (running/stopped)
- PostgreSQL connectivity
- Streaming replication status
- Raft cluster status
- Leader election state

**Example Output:**

```
=== Container Status ===
pgraft-primary    : running
pgraft-replica1   : running
pgraft-replica2   : running

=== PostgreSQL Status ===
pgraft-primary    : accepting connections (primary)
pgraft-replica1   : accepting connections (replica)
pgraft-replica2   : accepting connections (replica)

=== Raft Status ===
Node 1 (primary)  : state=leader, term=1
Node 2 (replica1) : state=follower, term=1
Node 3 (replica2) : state=follower, term=1

Leader: Node 1
```

---

### Destroy Cluster

```bash
./pgraft_cluster.py --docker --destroy
```

Removes all containers and the Docker network.

---

## Configuration

### Docker Image

The script uses `postgres-pgraft:17` image with:

- PostgreSQL 17
- pgraft extension pre-installed
- Required configuration files

### Network Ports

| Node     | PostgreSQL Port | Raft Port |
|----------|----------------|-----------|
| Primary  | 5432           | 7001      |
| Replica1 | 5433           | 7002      |
| Replica2 | 5434           | 7003      |

### pgraft Configuration

Each node is configured with:

```ini
shared_preload_libraries = 'pgraft'

# Node-specific configuration
pgraft.name = 'primary'  # or 'replica1', 'replica2'
pgraft.listen_address = '0.0.0.0:7001'  # port varies per node
pgraft.initial_cluster = 'primary=pgraft-primary:7001,replica1=pgraft-replica1:7002,replica2=pgraft-replica2:7003'
pgraft.data_dir = '/var/lib/postgresql/pgraft'
```

---

## Advanced Usage

### Custom Number of Nodes

```bash
# Create a 5-node cluster (1 primary + 4 replicas)
./pgraft_cluster.py --docker --init --nodes 5
```

### Connect to Nodes

```bash
# Connect to primary
docker exec -it pgraft-primary psql -U postgres

# Connect to replica
docker exec -it pgraft-replica1 psql -U postgres
```

### View Logs

```bash
# Primary logs
docker logs pgraft-primary

# Replica logs  
docker logs pgraft-replica1

# Follow logs
docker logs -f pgraft-primary
```

### Execute SQL on All Nodes

```bash
# On primary
docker exec pgraft-primary psql -U postgres -c "SELECT * FROM pgraft_get_cluster_status();"

# On replica
docker exec pgraft-replica1 psql -U postgres -c "SELECT pgraft_is_leader();"
```

---

## Testing Scenarios

### 1. Leader Election

```bash
# Stop the current leader
docker stop pgraft-primary

# Check status - a new leader should be elected
./pgraft_cluster.py --docker --status
```

### 2. Streaming Replication

```bash
# Insert data on primary
docker exec pgraft-primary psql -U postgres -c "
CREATE TABLE test (id int, data text);
INSERT INTO test VALUES (1, 'hello');
"

# Verify on replica
docker exec pgraft-replica1 psql -U postgres -c "SELECT * FROM test;"
```

### 3. Network Partition

```bash
# Disconnect a replica
docker network disconnect pgraft-network pgraft-replica2

# Check Raft status
docker exec pgraft-primary psql -U postgres -c "SELECT * FROM pgraft_get_nodes();"

# Reconnect
docker network connect pgraft-network pgraft-replica2
```

---

## Troubleshooting

### Containers Won't Start

```bash
# Check Docker status
docker ps -a

# View logs
docker logs pgraft-primary

# Check ports
netstat -an | grep -E '5432|5433|5434|7001|7002|7003'
```

### Replication Not Working

```bash
# Check replication status on primary
docker exec pgraft-primary psql -U postgres -c "SELECT * FROM pg_stat_replication;"

# Check recovery status on replica
docker exec pgraft-replica1 psql -U postgres -c "SELECT pg_is_in_recovery();"
```

### Raft Leader Not Elected

```bash
# Check pgraft status on all nodes
docker exec pgraft-primary psql -U postgres -c "SELECT * FROM pgraft_get_cluster_status();"

# Check pgraft logs
docker exec pgraft-primary tail -100 /var/log/postgresql/postgresql-17-main.log
```

### Clean Start

```bash
# Destroy and reinitialize
./pgraft_cluster.py --docker --destroy
./pgraft_cluster.py --docker --init --nodes 3
```

---

## Script Options

| Option      | Description                                    |
|-------------|------------------------------------------------|
| `--docker`  | Use Docker for cluster management             |
| `--init`    | Initialize a new cluster                       |
| `--nodes N` | Number of nodes (1 primary + N-1 replicas)    |
| `--status`  | Show cluster status                            |
| `--destroy` | Destroy the cluster                            |

---

## Internal Details

### Dockerfile

The script expects a Dockerfile in the repository root that:

1. Uses PostgreSQL 17 base image
2. Installs pgraft extension
3. Copies configuration files
4. Sets up replication user

### Replication Setup

1. Primary creates replication user
2. Replicas use `pg_basebackup` to clone data
3. `standby.signal` file marks replicas
4. Streaming replication configured via `primary_conninfo`

### pgraft Initialization

1. Extension loaded via `shared_preload_libraries`
2. Auto-initialization from `postgresql.conf`
3. Nodes discover each other via `initial_cluster`
4. Leader election happens automatically

---

## Best Practices

1. **Always check status** after initialization:
   ```bash
   ./pgraft_cluster.py --docker --status
   ```

2. **Wait for leader election** before running queries (5-10 seconds)

3. **Use primary for writes**, replicas for reads

4. **Monitor Raft status** regularly:
   ```sql
   SELECT * FROM pgraft_get_cluster_status();
   ```

5. **Clean up** when done:
   ```bash
   ./pgraft_cluster.py --docker --destroy
   ```

---

## Next Steps

- [Configuration Reference](configuration.md) - Tune pgraft settings
- [SQL Functions](sql-functions.md) - Learn pgraft SQL API
- [Cluster Operations](cluster-operations.md) - Advanced cluster management
- [Troubleshooting](../operations/troubleshooting.md) - Solve common issues

