
# Quick Start (pgElephant Suite)

Get your first **pgraft** cluster up and running in minutes! All steps and health checks are up to date for the latest release and unified with the [pgElephant](https://pgelephant.com) suite.

## Step 1: Configure PostgreSQL

Add these settings to your `postgresql.conf`:

```ini
shared_preload_libraries = 'pgraft'

# Core cluster configuration
pgraft.cluster_id = 'production-cluster'
pgraft.node_id = 1
pgraft.address = '127.0.0.1'
pgraft.port = 7001
pgraft.data_dir = '/var/lib/postgresql/pgraft'

# Consensus settings (optional, these are defaults)
pgraft.election_timeout = 1000        # milliseconds
pgraft.heartbeat_interval = 100       # milliseconds
pgraft.snapshot_interval = 10000      # entries
pgraft.max_log_entries = 1000         # compaction threshold
```

!!! tip "Configuration Tips"
    - `node_id` must be unique for each node (1, 2, 3, ...)
    - `cluster_id` must be the same for all nodes in the cluster
    - `port` is for Raft communication (not PostgreSQL port)

## Step 2: Restart PostgreSQL

```bash
# Restart PostgreSQL to load the extension
pg_ctl restart -D /path/to/data
```

## Step 3: Initialize pgraft

Connect to PostgreSQL and create the extension:

```sql
-- Create extension
CREATE EXTENSION pgraft;

-- Initialize node
SELECT pgraft_init();
```

??? success "Expected Output"
    ```
     pgraft_init 
    -------------
     t
    (1 row)
    ```

## Step 4: Set Up Additional Nodes

Repeat steps 1-3 on other nodes with different `node_id` values:

**Node 2** (`postgresql.conf`):
```ini
pgraft.node_id = 2
pgraft.port = 7002
```

**Node 3** (`postgresql.conf`):
```ini
pgraft.node_id = 3
pgraft.port = 7003
```

## Step 5: Add Nodes to Cluster

!!! warning "Leader Only"
    Node addition must be performed **only on the leader node**.

Wait 10 seconds for leader election, then check which node is the leader:

```sql
-- Check if current node is leader
SELECT pgraft_is_leader();

-- Get leader ID
SELECT pgraft_get_leader();
```

On the **leader node**, add the other nodes:

```sql
SELECT pgraft_add_node(2, '127.0.0.1', 7002);
SELECT pgraft_add_node(3, '127.0.0.1', 7003);
```

## Step 6: Verify Cluster Status

On any node, check the cluster status:

```sql
-- Get cluster status
SELECT * FROM pgraft_get_cluster_status();

-- Get all nodes
SELECT * FROM pgraft_get_nodes();

-- Check worker status
SELECT pgraft_get_worker_state();
```

??? success "Expected Output"
    ```sql
    SELECT * FROM pgraft_get_nodes();
    
     node_id |   address   | port | is_leader 
    ---------+-------------+------+-----------
           1 | 127.0.0.1   | 7001 | t
           2 | 127.0.0.1   | 7002 | f
           3 | 127.0.0.1   | 7003 | f
    (3 rows)
    ```

## Quick Health Check

Run this query to quickly verify your cluster is healthy:

```sql
SELECT 
    pgraft_is_leader() as is_leader,
    pgraft_get_term() as term,
    pgraft_get_leader() as leader_id,
    pgraft_get_worker_state() as worker;
```

!!! success "Success"
    You now have a working pgraft cluster with automatic leader election and log replication!

## Next Steps

- [Learn more about configuration options](../user-guide/configuration.md)
- [Follow the complete tutorial](../user-guide/tutorial.md)
- [Understand the architecture](../concepts/architecture.md)
- [Learn about automatic replication](../concepts/automatic-replication.md)

## Using the Test Harness

For testing and development, use the included test harness:

```bash
cd examples

# Destroy existing cluster
./run.sh --destroy

# Initialize new cluster
./run.sh --init

# Check status
./run.sh --status

# View logs
tail -f logs/primary1/postgresql.log
```

