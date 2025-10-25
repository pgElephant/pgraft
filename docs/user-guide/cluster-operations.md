
# Cluster Operations (pgElephant Suite)

This guide covers common cluster operations and management tasks for **pgraft**, part of the unified [pgElephant](https://pgelephant.com) high-availability suite. All steps and best practices are up to date and consistent with the latest release.

## Adding Nodes

### Prerequisites

Before adding a node:

1. **Node is configured** with matching `cluster_id`
2. **PostgreSQL is running** on the new node
3. **pgraft extension is created** and initialized
4. **Network connectivity** exists between nodes
5. **You are on the leader node**

### Step-by-Step

**1. Prepare the new node:**

```bash
# On the new node (e.g., Node 4)
# Edit postgresql.conf
cat >> $PGDATA/postgresql.conf <<EOF
shared_preload_libraries = 'pgraft'
pgraft.cluster_id = 'production-cluster'  # Must match existing cluster
pgraft.node_id = 4                        # Unique ID
pgraft.address = '192.168.1.14'
pgraft.port = 7004
pgraft.data_dir = '/var/lib/postgresql/pgraft'
EOF

# Restart PostgreSQL
pg_ctl restart -D $PGDATA

# Initialize pgraft
psql -c "CREATE EXTENSION pgraft;"
psql -c "SELECT pgraft_init();"
```

**2. Add node from leader:**

```sql
-- On the current leader
-- First, verify you are the leader
SELECT pgraft_is_leader();
-- Should return: t

-- Add the new node
SELECT pgraft_add_node(4, '192.168.1.14', 7004);
-- Should return: t

-- Verify node was added
SELECT * FROM pgraft_get_nodes();
```

**3. Verify on all nodes:**

```bash
# Check on all nodes that they see the new node
for port in 5432 5433 5434; do
    echo "Node on port $port:"
    psql -p $port -c "SELECT * FROM pgraft_get_nodes();"
done
```

### Important Notes

!!! warning "Leader Only"
    Nodes can ONLY be added from the leader. Attempting to add from a follower will fail with: "Cannot add node - this node is not the leader"

!!! info "Automatic Replication"
    When you add a node on the leader, the configuration change automatically replicates to ALL nodes in the cluster. You do not need to run the command on each node.

## Removing Nodes

### When to Remove

- Node is permanently decommissioned
- Downsizing cluster
- Replacing failed hardware

### Step-by-Step

**1. Identify the node to remove:**

```sql
SELECT * FROM pgraft_get_nodes();
```

**2. Remove from cluster (on leader):**

```sql
-- Verify you are the leader
SELECT pgraft_is_leader();

-- Remove the node
SELECT pgraft_remove_node(4);
-- Should return: t
```

**3. Shutdown the removed node:**

```bash
# On the removed node
pg_ctl stop -D $PGDATA
```

**4. Verify removal:**

```sql
-- On any remaining node
SELECT * FROM pgraft_get_nodes();
-- Node 4 should not appear
```

### Important Notes

!!! warning "Quorum Impact"
    Removing nodes reduces fault tolerance. A 5-node cluster tolerates 2 failures, but after removing 2 nodes, the 3-node cluster only tolerates 1 failure.

## Leader Election

### Understanding Elections

Elections occur when:

- Cluster starts (initial election)
- Current leader fails
- Network partition isolates leader
- Leader explicitly steps down (future feature)

### Monitoring Elections

```sql
-- Check current term (increases on each election)
SELECT pgraft_get_term();

-- Check who is leader
SELECT pgraft_get_leader();

-- Check if this node is leader
SELECT pgraft_is_leader();
```

### Election Timeline

Typical election after leader failure:

```
T+0s:   Leader fails
T+1s:   Followers notice missing heartbeats
T+1.5s: Election timeout triggers
T+2s:   New leader elected
T+2.5s: New leader sends first heartbeat
```

### Troubleshooting Elections

**Problem: No leader elected**

```sql
-- Check worker status on all nodes
SELECT pgraft_get_worker_state();

-- Check network connectivity
-- Ensure majority of nodes can communicate
```

**Problem: Frequent elections**

```sql
-- Check term (if increasing rapidly)
SELECT pgraft_get_term();

-- Possible causes:
-- 1. Network instability
-- 2. Election timeout too low
-- 3. System overload
```

## Configuration Changes

### Updating GUC Parameters

Most pgraft parameters require a PostgreSQL restart:

```bash
# Edit postgresql.conf
vim $PGDATA/postgresql.conf

# Change parameters (e.g., election timeout)
pgraft.election_timeout = 2000

# Restart PostgreSQL
pg_ctl restart -D $PGDATA
```

### Dynamic Parameters

Some parameters can be changed without restart (if supported):

```bash
# Reload configuration
pg_ctl reload -D $PGDATA
```

!!! warning "Restart Required"
    Currently, most pgraft parameters require a full restart because the extension is loaded via `shared_preload_libraries`.

## Cluster Maintenance

### Planned Downtime

For maintenance requiring node restarts:

**1. Start with followers:**

```bash
# Identify followers
psql -c "SELECT node_id, pgraft_is_leader() FROM pgraft_get_nodes();"

# Restart followers one at a time
# Wait for each to rejoin before restarting next
```

**2. Finally restart leader:**

```bash
# Last, restart the leader
# A new leader will be elected automatically
pg_ctl restart -D $PGDATA

# Verify new leader elected
psql -c "SELECT pgraft_get_leader();"
```

### Backup and Restore

**Backup:**

```bash
# 1. Backup PostgreSQL data
pg_basebackup -D /backup/pgdata

# 2. Backup pgraft state
tar -czf /backup/pgraft.tar.gz $PGRAFT_DATA_DIR

# 3. Backup configuration
cp $PGDATA/postgresql.conf /backup/
```

**Restore:**

```bash
# 1. Stop PostgreSQL
pg_ctl stop -D $PGDATA

# 2. Restore PostgreSQL data
rm -rf $PGDATA
tar -xzf /backup/pgdata.tar.gz -C $PGDATA

# 3. Restore pgraft state
rm -rf $PGRAFT_DATA_DIR
tar -xzf /backup/pgraft.tar.gz -C /

# 4. Restore configuration
cp /backup/postgresql.conf $PGDATA/

# 5. Start PostgreSQL
pg_ctl start -D $PGDATA
```

## Cluster Expansion

### Growing from 3 to 5 nodes

**Current: 3 nodes** (tolerates 1 failure)  
**Target: 5 nodes** (tolerates 2 failures)

**Steps:**

```bash
# 1. Prepare nodes 4 and 5
# Configure and start them (see "Adding Nodes" above)

# 2. From leader, add node 4
psql -c "SELECT pgraft_add_node(4, '192.168.1.14', 7004);"

# 3. Wait for node 4 to sync
sleep 5

# 4. Add node 5
psql -c "SELECT pgraft_add_node(5, '192.168.1.15', 7005);"

# 5. Verify all 5 nodes
psql -c "SELECT * FROM pgraft_get_nodes();"
```

## Cluster Reduction

### Shrinking from 5 to 3 nodes

**Current: 5 nodes** (tolerates 2 failures)  
**Target: 3 nodes** (tolerates 1 failure)

**Steps:**

```bash
# 1. From leader, remove node 5
psql -c "SELECT pgraft_remove_node(5);"

# 2. Shutdown node 5
ssh node5 "pg_ctl stop -D $PGDATA"

# 3. Remove node 4
psql -c "SELECT pgraft_remove_node(4);"

# 4. Shutdown node 4
ssh node4 "pg_ctl stop -D $PGDATA"

# 5. Verify 3 nodes remain
psql -c "SELECT * FROM pgraft_get_nodes();"
```

## Log Management

### Log Compaction

pgraft automatically compacts logs based on configuration:

```ini
pgraft.snapshot_interval = 10000      # Create snapshot every 10k entries
pgraft.max_log_entries = 1000         # Compact when exceeds threshold
```

### Manual Snapshot

Currently automatic only. Future versions may support manual snapshots.

### Viewing Log Statistics

```sql
SELECT * FROM pgraft_log_get_stats();
```

Output:
```
 log_size | last_index | commit_index | last_applied 
----------+------------+--------------+--------------
     5432 |       5432 |         5432 |         5432
```

## Monitoring During Operations

### During Node Addition

```sql
-- Monitor replication to new node
SELECT * FROM pgraft_log_get_replication_status();

-- Check if new node caught up
SELECT * FROM pgraft_log_get_stats();
```

### During Maintenance

```sql
-- Monitor cluster health
SELECT 
    pgraft_get_leader() as leader,
    pgraft_get_term() as term,
    COUNT(*) as num_nodes
FROM pgraft_get_nodes();
```

## Best Practices

### Adding Nodes

1. Add one node at a time
2. Wait for node to sync before adding next
3. Verify node appears on all members
4. Monitor replication status

### Removing Nodes

1. Ensure cluster will still have quorum after removal
2. Remove from cluster first, then shutdown
3. Verify removal propagated to all nodes
4. Update external documentation/configs

### During Maintenance

1. Start with followers, end with leader
2. Wait for each node to rejoin before proceeding
3. Monitor term changes (should be minimal)
4. Keep majority online at all times

## Emergency Procedures

### Lost Quorum

If majority of nodes fail:

1. **Do not panic** - remaining nodes are read-only
2. **Restore failed nodes** or add new nodes
3. **Once quorum restored**, leader will be elected
4. **Cluster resumes** normal operations

### Split Brain Verification

pgraft prevents split-brain, but you can verify:

```bash
# During partition, check both sides
# Only one side will have a leader
psql -h partition1_node -c "SELECT pgraft_is_leader();"
psql -h partition2_node -c "SELECT pgraft_is_leader();"

# Only one will return 't'
```

### Complete Cluster Failure

If all nodes crash:

1. **Restore nodes** from backup
2. **Start all nodes**
3. **Wait for election** (10 seconds)
4. **Verify leader elected**

```sql
SELECT pgraft_get_leader();
```

## Summary

Key operations:

- **Add node**: `pgraft_add_node()` on leader
- **Remove node**: `pgraft_remove_node()` on leader
- **Monitor**: `pgraft_get_cluster_status()`, `pgraft_get_nodes()`
- **Verify leader**: `pgraft_is_leader()`, `pgraft_get_leader()`

Remember: All configuration changes happen on the leader and automatically replicate to followers!

