# pgraft Three-Node Cluster Example

This directory contains a complete example of setting up and running a three-node PostgreSQL cluster with pgraft consensus for testing and demonstration purposes.

## Overview

This example demonstrates:
- **Three-node cluster**: One primary and two replicas
- **Automatic leader election** using Raft consensus
- **Distributed state synchronization**
- **Split-brain protection**
- **Cluster management** using Python scripts

## Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Primary1      │     │   Replica1      │     │   Replica2      │
│   Port: 5432    │     │   Port: 5433    │     │   Port: 5434    │
│   Raft: 7000    │────▶│   Raft: 7001    │────▶│   Raft: 7002    │
│   Node ID: 1    │     │   Node ID: 2    │     │   Node ID: 3    │
└─────────────────┘     └─────────────────┘     └─────────────────┘
         │                       │                       │
         └───────────────────────┴───────────────────────┘
                    Raft Consensus Layer
```

## Prerequisites

### Required Software

1. **PostgreSQL** (16, 17, or 18)
   - `pg_ctl`, `initdb`, `psql` must be in PATH
   - Or specify path explicitly

2. **Python 3.8+**
   ```bash
   python3 --version
   ```

3. **pgraft extension** (built and available)
   - See main [BUILD_GUIDE.md](../BUILD_GUIDE.md) for building instructions

### Required Python Packages

Install dependencies:
```bash
pip install -r requirements.txt
```

Or manually:
```bash
pip install psycopg2-binary PyYAML colorama
```

## Quick Start

### 1. Setup Environment

Run the setup script to prepare your environment:

```bash
./setup.sh
```

This will:
- Check for Python 3
- Check for PostgreSQL installation
- Create Python virtual environment
- Install Python dependencies
- Verify pgraft extension availability

### 2. Initialize Three-Node Cluster

**Option A: Using the Python script (Recommended)**

```bash
python3 pgraft_cluster.py --init
```

This will create three PostgreSQL instances:
- **primary1**: Port 5432, Node ID 1, Raft port 7000
- **replica1**: Port 5433, Node ID 2, Raft port 7001
- **replica2**: Port 5434, Node ID 3, Raft port 7002

**Option B: Using the shell script**

```bash
./run.sh
```

### 3. Verify Cluster Health

```bash
python3 pgraft_cluster.py --status
```

This shows:
- Node status (running/stopped)
- Leader election status
- Cluster membership
- Raft consensus state

### 4. Check Cluster Connectivity

```bash
python3 pgraft_cluster.py --verify
```

Verifies:
- All nodes are accessible
- pgraft extension is loaded
- Cluster is forming consensus

## Cluster Management

### Start the Cluster

```bash
python3 pgraft_cluster.py --init
```

### Check Status

```bash
python3 pgraft_cluster.py --status
```

### Connect to Nodes

**Primary node:**
```bash
psql -h localhost -p 5432 -U postgres
```

**Replica 1:**
```bash
psql -h localhost -p 5433 -U postgres
```

**Replica 2:**
```bash
psql -h localhost -p 5434 -U postgres
```

### Query pgraft Status

Once connected to any node:

```sql
-- Check if pgraft is loaded
SELECT * FROM pg_available_extensions WHERE name = 'pgraft';

-- Check cluster status
SELECT pgraft.cluster_status();

-- Check leader information
SELECT pgraft.leader_info();

-- View cluster members
SELECT pgraft.cluster_members();
```

### View Logs

Logs are stored in the `logs/` directory:

```bash
# Primary logs
tail -f logs/primary1/pgraft.log
tail -f logs/primary1/postgresql.log

# Replica 1 logs
tail -f logs/replica1/pgraft.log
tail -f logs/replica1/postgresql.log

# Replica 2 logs
tail -f logs/replica2/pgraft.log
tail -f logs/replica2/postgresql.log
```

### Stop the Cluster

```bash
python3 pgraft_cluster.py --destroy
```

This will:
- Stop all PostgreSQL instances gracefully
- Clean up data directories
- Remove temporary files

## Configuration Files

### Node Configurations

- **primary1.conf**: Primary node PostgreSQL configuration
- **replica1.conf**: Replica 1 PostgreSQL configuration
- **replica2.conf**: Replica 2 PostgreSQL configuration

Each configuration includes:
- Port settings
- pgraft extension loading
- Shared memory settings
- Logging configuration

### Cluster Script Configuration

Edit `pgraft_cluster.py` to customize:

```python
# Node configurations (lines ~50-80)
NodeConfig(
    name="primary1",
    port=5432,           # PostgreSQL port
    pgraft_port=7000,    # Raft consensus port
    data_dir="...",
    config_file="primary1.conf",
    metrics_port=9100,
    go_library_path="..."
)
```

## Testing Split-Brain Protection

### Scenario 1: Kill Primary Node

```bash
# Check current leader
python3 pgraft_cluster.py --status

# Kill primary (simulate failure)
pg_ctl -D /tmp/pgraft/primary1 stop -m immediate

# Wait for leader election (5-10 seconds)
sleep 10

# Check new leader
python3 pgraft_cluster.py --status
```

### Scenario 2: Network Partition Simulation

```bash
# Isolate one node by stopping it temporarily
pg_ctl -D /tmp/pgraft/replica1 stop

# Verify cluster continues with 2 nodes
python3 pgraft_cluster.py --status

# Restart the isolated node
pg_ctl -D /tmp/pgraft/replica1 start

# Node should rejoin cluster
python3 pgraft_cluster.py --status
```

## Troubleshooting

### Cluster Won't Start

**Check PostgreSQL is in PATH:**
```bash
which pg_ctl initdb psql
```

**Check ports are available:**
```bash
lsof -i :5432
lsof -i :5433
lsof -i :5434
```

**Check pgraft extension:**
```bash
ls -la ../pgraft.so  # Linux
ls -la ../pgraft.dylib  # macOS
```

### Connection Refused

**Verify nodes are running:**
```bash
python3 pgraft_cluster.py --status
```

**Check PostgreSQL logs:**
```bash
tail -100 logs/primary1/postgresql.log
```

### pgraft Extension Not Loading

**Check extension files:**
```bash
# Ensure extension files exist
ls -la ../pgraft.control
ls -la ../pgraft--1.0.sql

# Check Go library
ls -la ../src/pgraft_go.so  # Linux
ls -la ../src/pgraft_go.dylib  # macOS
```

**Check PostgreSQL configuration:**
```bash
# Verify shared_preload_libraries includes pgraft
grep shared_preload_libraries primary1.conf
```

### Leader Election Issues

**Allow more time:**
- Leader election typically takes 5-10 seconds
- Network latency can delay consensus

**Check Raft logs:**
```bash
tail -100 logs/primary1/pgraft.log
grep -i "leader\|election\|vote" logs/*/pgraft.log
```

## Files in This Directory

- **pgraft_cluster.py** - Main cluster management script (Python)
- **setup.sh** - Environment setup script
- **run.sh** - Quick cluster startup script
- **requirements.txt** - Python dependencies
- **primary1.conf** - Primary node configuration
- **replica1.conf** - Replica 1 configuration
- **replica2.conf** - Replica 2 configuration
- **logs/** - Directory containing all node logs
  - `primary1/` - Primary node logs
  - `replica1/` - Replica 1 logs
  - `replica2/` - Replica 2 logs

## Advanced Usage

### Custom Base Directory

```bash
python3 pgraft_cluster.py --init --base-dir /custom/path
```

### Verbose Output

```bash
python3 pgraft_cluster.py --init --verbose
python3 pgraft_cluster.py --status -v
```

### Custom PostgreSQL Installation

If PostgreSQL isn't in PATH:

```bash
export PATH="/usr/local/pgsql.18/bin:$PATH"
python3 pgraft_cluster.py --init
```

## Performance Testing

### Write Performance Test

```bash
# Connect to primary
psql -h localhost -p 5432 -U postgres -c "
CREATE TABLE test_data (id SERIAL PRIMARY KEY, data TEXT);
INSERT INTO test_data (data) 
SELECT md5(random()::text) FROM generate_series(1, 10000);
"

# Verify replication
psql -h localhost -p 5433 -U postgres -c "SELECT count(*) FROM test_data;"
```

### Failover Test

```bash
# Kill leader and measure failover time
time (pg_ctl -D /tmp/pgraft/primary1 stop -m immediate && sleep 10 && python3 pgraft_cluster.py --status | grep Leader)
```

## Integration with CI/CD

This example cluster is perfect for:
- **Integration testing** pgraft features
- **Performance benchmarking** Raft consensus overhead
- **Failover testing** automated recovery scenarios
- **Development** and debugging pgraft

## Next Steps

- Read the main [Tutorial](../TUTORIAL.md) for pgraft concepts
- See [User Guide](../docs/user-guide/) for production deployment
- Check [Configuration Guide](../docs/user-guide/configuration.md) for tuning
- Review [Best Practices](../docs/operations/best-practices.md) for production

## Support

- **Issues**: https://github.com/pgElephant/pgraft/issues
- **Documentation**: https://pgelephant.github.io/pgraft/
- **Examples**: This directory

---

**Note:** This is a development/testing example. For production deployments, see the [DEPLOY_GUIDE.md](../DEPLOY_GUIDE.md) and [Best Practices](../docs/operations/best-practices.md).

