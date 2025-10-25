# Troubleshooting

This page covers common issues and their solutions.

## Worker Not Running

### Symptom

```sql
SELECT pgraft_get_worker_state();
-- Returns: STOPPED or ERROR
```

### Diagnosis

Check if pgraft is in `shared_preload_libraries`:

```sql
SHOW shared_preload_libraries;
```

### Solution

1. Add pgraft to `postgresql.conf`:
```ini
shared_preload_libraries = 'pgraft'
```

2. Restart PostgreSQL:
```bash
pg_ctl restart -D /path/to/data
```

3. Verify:
```sql
SELECT pgraft_get_worker_state();
-- Should return: RUNNING
```

---

## Cannot Add Node

### Symptom

```sql
SELECT pgraft_add_node(2, '127.0.0.1', 7002);
-- Error: "Cannot add node - this node is not the leader"
```

### Diagnosis

You're trying to add a node on a follower. Only the leader can add nodes.

### Solution

1. Find the leader:
```sql
SELECT pgraft_get_leader();  -- Returns leader node ID
```

2. Connect to the leader node and run the command there:
```bash
# If node 1 is leader, connect to its PostgreSQL port
psql -h 127.0.0.1 -p 5432 -c "SELECT pgraft_add_node(2, '127.0.0.1', 7002);"
```

---

## No Leader Elected

### Symptom

```sql
SELECT pgraft_get_leader();
-- Returns: 0 (no leader)

SELECT * FROM pgraft_get_cluster_status();
-- Shows term 0 or very low term
```

### Possible Causes

1. **Cluster just started**: Leader election takes ~1 second
2. **Network issues**: Nodes cannot communicate
3. **No quorum**: Insufficient nodes for majority
4. **Configuration mismatch**: Nodes have different cluster_ids

### Solution

#### 1. Wait for Election

```bash
# Wait 10 seconds and check again
sleep 10
psql -c "SELECT pgraft_get_leader();"
```

#### 2. Check Network Connectivity

```bash
# From Node 1, test connection to Node 2's Raft port
nc -zv 127.0.0.1 7002

# Or use telnet
telnet 127.0.0.1 7002
```

#### 3. Verify Cluster Configuration

```sql
-- On each node, check configuration
SHOW pgraft.cluster_id;  -- Must be same on all nodes
SHOW pgraft.node_id;     -- Must be unique per node
SHOW pgraft.address;     -- Node's own address
SHOW pgraft.port;        -- Raft port (not PostgreSQL port)
```

#### 4. Check Logs

```bash
tail -100 /path/to/postgresql/log/postgresql-*.log | grep pgraft
```

Look for errors like:
- "Connection refused"
- "Network unreachable"
- "Failed to send message"

---

## Frequent Leader Changes

### Symptom

```sql
-- Term keeps increasing rapidly
SELECT pgraft_get_term();
-- Returns: 156 (very high)

-- Different leader each time you check
SELECT pgraft_get_leader();
```

### Possible Causes

1. **Network instability**: Packet loss or high latency
2. **Election timeout too low**: Nodes timeout before receiving heartbeats
3. **Node overload**: Nodes too busy to respond in time

### Solution

#### 1. Increase Election Timeout

Edit `postgresql.conf`:
```ini
# Increase from 1000ms to 2000ms or higher
pgraft.election_timeout = 2000
```

Restart PostgreSQL on all nodes.

#### 2. Check Network Latency

```bash
# Measure latency between nodes
ping -c 10 node2_address

# Check packet loss
ping -c 100 node2_address | grep loss
```

#### 3. Monitor System Load

```bash
# Check CPU usage
top

# Check if PostgreSQL is swapping
vmstat 1 10
```

---

## Node Cannot Join Cluster

### Symptom

Added node using `pgraft_add_node()` but node doesn't appear in cluster:

```sql
SELECT * FROM pgraft_get_nodes();
-- New node not listed
```

### Diagnosis

#### 1. Check if Node is Running

```bash
# On the new node
psql -c "SELECT pgraft_get_worker_state();"
# Should be RUNNING
```

#### 2. Check Node Configuration

```sql
-- On the new node
SHOW pgraft.cluster_id;  -- Must match existing cluster
SHOW pgraft.node_id;     -- Must match ID used in pgraft_add_node()
SHOW pgraft.address;     -- Must match address used in pgraft_add_node()
SHOW pgraft.port;        -- Must match port used in pgraft_add_node()
```

### Solution

1. **Ensure node is initialized:**
```sql
-- On the new node
CREATE EXTENSION IF NOT EXISTS pgraft;
SELECT pgraft_init();
```

2. **Verify network connectivity:**
```bash
# From existing node to new node
nc -zv new_node_address new_node_port

# From new node to existing nodes
nc -zv existing_node_address existing_node_port
```

3. **Check firewall rules:**
```bash
# Ensure Raft ports are open
sudo firewall-cmd --list-all  # CentOS/RHEL
sudo ufw status               # Ubuntu
```

---

## Data Directory Errors

### Symptom

```
ERROR: pgraft: Failed to persist HardState
ERROR: pgraft: Cannot create directory
```

### Solution

1. **Check directory permissions:**
```bash
# Ensure PostgreSQL can write to data_dir
ls -ld /var/lib/postgresql/pgraft
# Should be owned by postgres user

# If not, fix permissions:
sudo chown -R postgres:postgres /var/lib/postgresql/pgraft
sudo chmod 700 /var/lib/postgresql/pgraft
```

2. **Check disk space:**
```bash
df -h /var/lib/postgresql/pgraft
```

3. **Verify configuration:**
```sql
SHOW pgraft.data_dir;
```

---

## Extension Won't Load

### Symptom

```sql
CREATE EXTENSION pgraft;
-- ERROR: could not load library "pgraft"
```

### Solution

1. **Verify extension files are installed:**
```bash
# Check library files
ls -l $(pg_config --libdir)/pgraft*.dylib
ls -l $(pg_config --libdir)/pgraft_go.dylib

# Check SQL files
ls -l $(pg_config --sharedir)/extension/pgraft*
```

2. **Check file permissions:**
```bash
chmod 755 $(pg_config --libdir)/pgraft*.dylib
```

3. **Verify architecture compatibility:**
```bash
# Check if library is for correct architecture
file $(pg_config --libdir)/pgraft.dylib

# Should match your system architecture
uname -m
```

4. **Check for missing dependencies:**
```bash
# macOS
otool -L $(pg_config --libdir)/pgraft.dylib

# Linux
ldd $(pg_config --libdir)/pgraft.so
```

---

## Compilation Errors

### Symptom

```
make
-- Errors during compilation
```

### Solution

#### 1. PostgreSQL Development Headers Missing

```bash
# Ubuntu/Debian
sudo apt-get install postgresql-server-dev-17

# CentOS/RHEL
sudo yum install postgresql17-devel

# macOS
brew install postgresql@17
```

#### 2. Go Not Found

```bash
# Check Go installation
go version

# If not installed:
# Ubuntu/Debian
sudo apt-get install golang-go

# macOS
brew install go
```

#### 3. pg_config Not in PATH

```bash
# Find pg_config
which pg_config

# If not found, add to PATH:
export PATH="/usr/local/pgsql/bin:$PATH"

# Or set PG_CONFIG in Makefile
make PG_CONFIG=/path/to/pg_config
```

---

## Performance Issues

### Symptom

- High CPU usage
- Slow replication
- Queries taking too long

### Diagnosis

```sql
-- Check log lag
SELECT * FROM pgraft_log_get_stats();

-- Monitor replication
SELECT * FROM pgraft_log_get_replication_status();
```

### Solution

#### 1. Tune Batch Settings

```ini
# Increase batch size for better throughput
pgraft.batch_size = 200
pgraft.max_batch_delay = 20
```

#### 2. Adjust Snapshot Frequency

```ini
# More frequent snapshots reduce log size
pgraft.snapshot_interval = 5000
pgraft.max_log_entries = 500
```

#### 3. Check System Resources

```bash
# CPU usage
top -p $(pidof postgres)

# Memory usage
free -h

# I/O wait
iostat -x 1 10
```

---

## Split-Brain Concerns

### Symptom

"I'm worried about split-brain. How do I verify protection?"

### Verification

1. **Test minority partition:**
```bash
# In a 3-node cluster, isolate one node
# On isolated node:
psql -c "SELECT pgraft_is_leader();"  # Should be false
psql -c "SELECT pgraft_add_node(4, '127.0.0.1', 7004);"  # Should fail

# On majority partition (2 nodes):
psql -c "SELECT pgraft_is_leader();"  # One should be true
psql -c "SELECT pgraft_add_node(4, '127.0.0.1', 7004);"  # Should succeed
```

2. **Monitor term numbers:**
```sql
-- Term should be stable during normal operation
SELECT pgraft_get_term();
```

See [Split-Brain Protection](../concepts/split-brain.md) for detailed explanation.

---

## Debug Mode

### Enable Debug Logging

```sql
SELECT pgraft_set_debug(true);
```

This will log detailed information about:
- Raft messages
- State transitions
- Log replication
- Network events

### View Debug Logs

```bash
tail -f /path/to/postgresql/log/postgresql-*.log | grep "pgraft:"
```

### Disable Debug Logging

```sql
SELECT pgraft_set_debug(false);
```

---

## Getting Help

If you're still experiencing issues:

1. **Check logs:**
```bash
tail -100 /path/to/postgresql/log/postgresql-*.log | grep pgraft
```

2. **Gather diagnostic information:**
```sql
SELECT pgraft_get_version();
SELECT * FROM pgraft_get_cluster_status();
SELECT * FROM pgraft_get_nodes();
SELECT * FROM pgraft_log_get_stats();
SELECT pgraft_get_worker_state();
```

3. **Enable debug mode and reproduce the issue:**
```sql
SELECT pgraft_set_debug(true);
-- Reproduce the problem
-- Copy relevant logs
SELECT pgraft_set_debug(false);
```

4. **Report the issue** on the GitHub repository with:
   - pgraft version
   - PostgreSQL version
   - Operating system
   - Configuration (postgresql.conf relevant sections)
   - Error messages and logs
   - Steps to reproduce

