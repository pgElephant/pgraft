# pgraft Cluster Examples

This directory contains scripts and examples for deploying pgraft PostgreSQL clusters.

## pgraft_cluster.py - Docker-Based Cluster Management

A comprehensive Python script for managing pgraft clusters using Docker containers. This is the **recommended** approach for testing and development.

### Features

- ✅ Docker-based deployment (isolated, reproducible)
- ✅ Automatic container creation with proper networking
- ✅ Support for 1-5 node clusters
- ✅ Automatic pgraft extension installation
- ✅ Pre-configured PostgreSQL with pgraft settings
- ✅ Easy cluster management (init, status, destroy)
- ✅ Persistent data storage
- ✅ Port mapping for external access

### Prerequisites

1. **Docker** installed and running
   ```bash
   docker --version
   ```

2. **Python 3.7+** with psycopg2
   ```bash
   pip install psycopg2-binary
   ```

### Quick Start

#### 1. Create a 3-Node Cluster

```bash
python pgraft_cluster.py --docker --init --nodes 3
```

This will:
- Create a Docker network (`pgraft-network`)
- Launch 3 PostgreSQL containers with pgraft
- Configure Raft consensus automatically
- Map ports for external access

#### 2. Check Cluster Status

```bash
python pgraft_cluster.py --docker --status --nodes 3
```

#### 3. Connect to the Cluster

```bash
# Connect to primary
psql postgresql://postgres:postgres@localhost:5432/postgres

# Connect to replica 1
psql postgresql://postgres:postgres@localhost:5433/postgres

# Connect to replica 2
psql postgresql://postgres:postgres@localhost:5434/postgres
```

#### 4. Test pgraft

```sql
-- Check pgraft status
SELECT * FROM pgraft.cluster_health();

-- View cluster members
SELECT * FROM pgraft.member_list();

-- Check if this node is the leader
SELECT pgraft.is_leader();

-- View Raft statistics
SELECT * FROM pgraft.cluster_statistics();
```

#### 5. Destroy the Cluster

```bash
python pgraft_cluster.py --docker --destroy --nodes 3
```

### Usage Examples

#### Single Node (for testing)

```bash
python pgraft_cluster.py --docker --init --nodes 1
```

#### Two-Node Cluster

```bash
python pgraft_cluster.py --docker --init --nodes 2
```

#### Five-Node Cluster

```bash
python pgraft_cluster.py --docker --init --nodes 5
```

#### Custom Base Directory

```bash
python pgraft_cluster.py --docker --init --nodes 3 --base-dir /custom/path
```

#### Verbose Output

```bash
python pgraft_cluster.py --docker --init --nodes 3 --verbose 1
```

### Cluster Configuration

Each node is configured with:

| Component | Primary (node1) | Replica 1 | Replica 2 | Replica 3 | Replica 4 |
|-----------|-----------------|-----------|-----------|-----------|-----------|
| **Container Name** | `pgraft-primary1` | `pgraft-replica1` | `pgraft-replica2` | `pgraft-replica3` | `pgraft-replica4` |
| **PostgreSQL Port** | 5432 | 5433 | 5434 | 5435 | 5436 |
| **Raft Port** | 7001 | 7002 | 7003 | 7004 | 7005 |
| **Metrics Port** | 9091 | 9092 | 9093 | 9094 | 9095 |
| **Raft Node ID** | 1 | 2 | 3 | 4 | 5 |

### Docker Commands

#### View Container Logs

```bash
# View all logs
docker logs pgraft-primary1

# Follow logs in real-time
docker logs -f pgraft-primary1

# Last 100 lines
docker logs --tail 100 pgraft-primary1
```

#### Execute Commands in Container

```bash
# Get a shell
docker exec -it pgraft-primary1 bash

# Run psql
docker exec -it pgraft-primary1 psql -U postgres

# Check PostgreSQL status
docker exec pgraft-primary1 pg_isready
```

#### Inspect Network

```bash
# List containers in network
docker network inspect pgraft-network

# View container IP addresses
docker inspect -f '{{.Name}} - {{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' $(docker ps -q --filter network=pgraft-network)
```

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    pgraft-network (Docker)                  │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │  primary1    │  │  replica1    │  │  replica2    │    │
│  │              │  │              │  │              │    │
│  │  PG: 5432    │  │  PG: 5433    │  │  PG: 5434    │    │
│  │  Raft: 7001  │  │  Raft: 7002  │  │  Raft: 7003  │    │
│  │              │  │              │  │              │    │
│  │  pgraft      │  │  pgraft      │  │  pgraft      │    │
│  │  Node ID: 1  │  │  Node ID: 2  │  │  Node ID: 3  │    │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘    │
│         │                 │                 │             │
│         └─────────────────┴─────────────────┘             │
│                   Raft Consensus                           │
└─────────────────────────────────────────────────────────────┘
         │                  │                  │
    Port 5432          Port 5433          Port 5434
    (Host Access)      (Host Access)      (Host Access)
```

### Data Persistence

Data is stored in: `/tmp/pgraft/docker/<node_name>/`

Each node directory contains:
- `data/` - PostgreSQL data directory
- `config/` - PostgreSQL configuration files

### Troubleshooting

#### Container Won't Start

```bash
# Check Docker logs
docker logs pgraft-primary1

# Check if port is in use
netstat -an | grep 5432

# Remove and recreate
python pgraft_cluster.py --docker --destroy --nodes 3
python pgraft_cluster.py --docker --init --nodes 3
```

#### Extension Not Loading

```bash
# Check if pgraft files exist
docker exec pgraft-primary1 ls -la /usr/local/lib/postgresql/pgraft*

# Check shared_preload_libraries
docker exec pgraft-primary1 psql -U postgres -c "SHOW shared_preload_libraries"

# Rebuild extension
docker exec pgraft-primary1 sh -c "cd /pgraft && make clean && make install"
docker restart pgraft-primary1
```

#### Network Issues

```bash
# Recreate network
docker network rm pgraft-network
docker network create pgraft-network

# Check network connectivity
docker exec pgraft-primary1 ping pgraft-replica1
```

#### Reset Everything

```bash
# Stop all containers
docker stop $(docker ps -q --filter network=pgraft-network)

# Remove all containers
docker rm $(docker ps -aq --filter network=pgraft-network)

# Remove network
docker network rm pgraft-network

# Clean data
rm -rf /tmp/pgraft/docker

# Start fresh
python pgraft_cluster.py --docker --init --nodes 3
```

### Performance Testing

#### Write Performance

```bash
# Create test table
psql postgresql://postgres:postgres@localhost:5432/postgres << EOF
CREATE TABLE test_writes (
    id SERIAL PRIMARY KEY,
    data TEXT,
    created_at TIMESTAMP DEFAULT NOW()
);
EOF

# Insert 10,000 rows
psql postgresql://postgres:postgres@localhost:5432/postgres -c "
    INSERT INTO test_writes (data)
    SELECT 'test data ' || i FROM generate_series(1, 10000) i;
"

# Check replication
for port in 5432 5433 5434; do
    echo "Port $port:"
    psql postgresql://postgres:postgres@localhost:$port/postgres -c "SELECT COUNT(*) FROM test_writes"
done
```

#### Failover Testing

```bash
# Stop primary
docker stop pgraft-primary1

# Check new leader
psql postgresql://postgres:postgres@localhost:5433/postgres -c "
    SELECT * FROM pgraft.cluster_health();
"

# Restart primary
docker start pgraft-primary1
```

### Advanced Configuration

#### Custom PostgreSQL Image

Modify `pgraft_cluster.py`:

```python
self.docker_image = "postgres:16-alpine"  # Use PostgreSQL 16
```

#### Custom Network

```python
self.docker_network = "my-custom-network"
```

#### Resource Limits

Add to `docker_create_container()`:

```python
'--memory=1g',
'--cpus=2',
```

### Integration with Other Tools

#### pgBalancer

Add pgbalancer container:

```bash
docker run -d \
  --name pgbalancer \
  --network pgraft-network \
  -p 5435:5435 \
  pgelephant/pgbalancer:latest \
  --backend-hosts pgraft-primary1:5432,pgraft-replica1:5432
```

#### Prometheus Monitoring

Expose metrics endpoint:

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'pgraft'
    static_configs:
      - targets:
        - 'localhost:9091'  # primary1
        - 'localhost:9092'  # replica1
        - 'localhost:9093'  # replica2
```

### Command Reference

```bash
# Initialize
python pgraft_cluster.py --docker --init --nodes 3

# Status
python pgraft_cluster.py --docker --status --nodes 3

# Destroy
python pgraft_cluster.py --docker --destroy --nodes 3

# Verbose mode
python pgraft_cluster.py --docker --init --nodes 3 -v 1

# Custom directory
python pgraft_cluster.py --docker --init --nodes 3 --base-dir /my/path
```

### Best Practices

1. **Always use Docker mode** for development and testing
2. **Start with 3 nodes** for proper Raft quorum
3. **Check logs** if something doesn't work: `docker logs pgraft-primary1`
4. **Clean up** when done: `--destroy` removes all resources
5. **Use persistent volumes** in production (modify mount paths)
6. **Monitor metrics** for performance insights
7. **Test failover** scenarios before production use

### Limitations

- Docker mode only (local mode deprecated)
- macOS and Linux supported (Windows with WSL2)
- Requires Docker 20.10+
- Resource limits depend on Docker configuration

### Support

For issues or questions:
- Check Docker logs: `docker logs pgraft-primary1`
- View pgraft logs inside container: `docker exec pgraft-primary1 cat /var/lib/postgresql/data/log/postgresql-*.log`
- GitHub Issues: https://github.com/pgElephant/pgraft/issues

---

**Last Updated**: October 2025
**Version**: 2.0 (Docker-based)
