
# Quick Start

Get your first **pgraft** cluster up and running in minutes!

## Step 1: Configure PostgreSQL

Add these settings to your `postgresql.conf`:

```ini
shared_preload_libraries = 'pgraft'

# Cluster identification and networking
pgraft.name = 'node1'
pgraft.listen_address = '0.0.0.0:7001'
pgraft.initial_cluster = 'node1=127.0.0.1:7001,node2=127.0.0.1:7002,node3=127.0.0.1:7003'

# Storage location
pgraft.data_dir = '/var/lib/postgresql/pgraft'

# Consensus settings (optional, these are defaults)
pgraft.election_timeout = 1000        # milliseconds
pgraft.heartbeat_interval = 100       # milliseconds
pgraft.snapshot_interval = 10000      # entries
```

!!! tip "Configuration Tips"
    - `pgraft.name` must be unique for each node and match a name in `initial_cluster`
    - `pgraft.initial_cluster` must be identical on all nodes
    - Node IDs are automatically assigned based on position in `initial_cluster`
    - `listen_address` is for Raft communication (not PostgreSQL port)

## Step 2: Restart PostgreSQL

```bash
# Restart PostgreSQL to load the extension
pg_ctl restart -D /path/to/data
```

## Step 3: Create Extension

Connect to PostgreSQL and create the extension:

```sql
-- Create extension (automatically initializes from configuration)
CREATE EXTENSION pgraft;
```

The extension will automatically initialize using the settings from `postgresql.conf`.

## Step 4: Set Up Additional Nodes

Repeat steps 1-3 on other nodes with different `pgraft.name` values:

**Node 2** (`postgresql.conf`):
```ini
pgraft.name = 'node2'
pgraft.listen_address = '0.0.0.0:7002'
pgraft.initial_cluster = 'node1=127.0.0.1:7001,node2=127.0.0.1:7002,node3=127.0.0.1:7003'
```

**Node 3** (`postgresql.conf`):
```ini
pgraft.name = 'node3'
pgraft.listen_address = '0.0.0.0:7003'
pgraft.initial_cluster = 'node1=127.0.0.1:7001,node2=127.0.0.1:7002,node3=127.0.0.1:7003'
```

!!! important "Initial Cluster Configuration"
    The `initial_cluster` string must be **identical** on all nodes. This ensures consistent node ID assignment based on the order in the list.

## Step 5: Verify Cluster Formation

Wait a few seconds for leader election, then check the cluster status:

```sql
-- Check cluster status
SELECT * FROM pgraft_get_cluster_status();

-- View all nodes (automatically discovered from initial_cluster)
SELECT * FROM pgraft_get_nodes();

-- Check if current node is leader
SELECT pgraft_is_leader();
```

!!! success "Automatic Node Discovery"
    Nodes are automatically added to the cluster based on the `initial_cluster` configuration. No manual `pgraft_add_node()` calls are needed for initial setup!

## Step 6: Verify Cluster Health

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

