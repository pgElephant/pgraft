# SQL Functions Reference

This page provides a complete reference of all SQL functions available in pgraft.

## Cluster Management

### `pgraft_init()`

Initialize pgraft on the current node.

**Syntax:**
```sql
pgraft_init() → boolean
```

**Returns:** `true` if initialization successful, `false` otherwise.

**Example:**
```sql
SELECT pgraft_init();
```

**Usage:**
- Must be called once after creating the extension
- Should be run on all nodes independently
- Creates necessary internal state and starts the Raft node

---

### `pgraft_add_node()`

Add a node to the cluster. **Must be executed on the leader.**

**Syntax:**
```sql
pgraft_add_node(node_id int, address text, port int) → boolean
```

**Parameters:**
- `node_id`: Unique identifier for the new node
- `address`: IP address or hostname of the node
- `port`: Raft communication port (not PostgreSQL port)

**Returns:** `true` if node was successfully added, `false` otherwise.

**Example:**
```sql
SELECT pgraft_add_node(2, '127.0.0.1', 7002);
SELECT pgraft_add_node(3, '192.168.1.103', 7003);
```

!!! warning "Leader Only"
    This function will fail if executed on a follower node. Use `pgraft_is_leader()` to check if the current node is the leader.

---

### `pgraft_remove_node()`

Remove a node from the cluster.

**Syntax:**
```sql
pgraft_remove_node(node_id int) → boolean
```

**Parameters:**
- `node_id`: ID of the node to remove

**Returns:** `true` if node was successfully removed, `false` otherwise.

**Example:**
```sql
SELECT pgraft_remove_node(3);
```

---

### `pgraft_get_cluster_status()`

Get comprehensive cluster status information.

**Syntax:**
```sql
pgraft_get_cluster_status() → TABLE(
    node_id bigint,
    term bigint,
    leader_id bigint,
    state text,
    num_nodes int,
    ...
)
```

**Returns:** A table with detailed cluster status.

**Example:**
```sql
SELECT * FROM pgraft_get_cluster_status();
```

**Sample Output:**
```
 node_id | term | leader_id |  state   | num_nodes 
---------+------+-----------+----------+-----------
       1 |    5 |         1 | Leader   |         3
```

---

### `pgraft_get_nodes()`

Get all nodes in the cluster.

**Syntax:**
```sql
pgraft_get_nodes() → TABLE(
    node_id bigint,
    address text,
    port int,
    is_leader boolean
)
```

**Returns:** A table listing all cluster nodes.

**Example:**
```sql
SELECT * FROM pgraft_get_nodes();
```

**Sample Output:**
```
 node_id |   address   | port | is_leader 
---------+-------------+------+-----------
       1 | 127.0.0.1   | 7001 | t
       2 | 127.0.0.1   | 7002 | f
       3 | 127.0.0.1   | 7003 | f
```

## Leader Information

### `pgraft_get_leader()`

Get the current leader's node ID.

**Syntax:**
```sql
pgraft_get_leader() → bigint
```

**Returns:** Node ID of the current leader, or 0 if no leader.

**Example:**
```sql
SELECT pgraft_get_leader();
```

---

### `pgraft_get_term()`

Get the current Raft term.

**Syntax:**
```sql
pgraft_get_term() → integer
```

**Returns:** Current term number.

**Example:**
```sql
SELECT pgraft_get_term();
```

---

### `pgraft_is_leader()`

Check if the current node is the leader.

**Syntax:**
```sql
pgraft_is_leader() → boolean
```

**Returns:** `true` if current node is leader, `false` otherwise.

**Example:**
```sql
SELECT pgraft_is_leader();

-- Use in conditional logic
DO $$
BEGIN
    IF pgraft_is_leader() THEN
        PERFORM pgraft_add_node(4, '127.0.0.1', 7004);
    ELSE
        RAISE NOTICE 'Not the leader, cannot add node';
    END IF;
END $$;
```

## Log Operations

### `pgraft_log_append()`

Append a log entry.

**Syntax:**
```sql
pgraft_log_append(term bigint, data text) → boolean
```

**Parameters:**
- `term`: Raft term for the entry
- `data`: Log entry data

**Returns:** `true` if successful.

---

### `pgraft_log_commit()`

Commit a log entry.

**Syntax:**
```sql
pgraft_log_commit(index bigint) → boolean
```

**Parameters:**
- `index`: Log index to commit

**Returns:** `true` if successful.

---

### `pgraft_log_apply()`

Apply a log entry to the state machine.

**Syntax:**
```sql
pgraft_log_apply(index bigint) → boolean
```

**Parameters:**
- `index`: Log index to apply

**Returns:** `true` if successful.

---

### `pgraft_log_get_entry()`

Get a specific log entry.

**Syntax:**
```sql
pgraft_log_get_entry(index bigint) → text
```

**Parameters:**
- `index`: Log index to retrieve

**Returns:** Log entry data as text.

---

### `pgraft_log_get_stats()`

Get log statistics.

**Syntax:**
```sql
pgraft_log_get_stats() → TABLE(
    log_size bigint,
    last_index bigint,
    commit_index bigint,
    last_applied bigint
)
```

**Returns:** Table with log statistics.

**Example:**
```sql
SELECT * FROM pgraft_log_get_stats();
```

---

### `pgraft_log_get_replication_status()`

Get replication status for all followers.

**Syntax:**
```sql
pgraft_log_get_replication_status() → TABLE(...)
```

**Returns:** Table with replication status for each follower.

---

### `pgraft_log_sync_with_leader()`

Synchronize local log with the leader.

**Syntax:**
```sql
pgraft_log_sync_with_leader() → boolean
```

**Returns:** `true` if sync successful.

---

### `pgraft_replicate_entry()`

Replicate an entry through the Raft consensus protocol.

**Syntax:**
```sql
pgraft_replicate_entry(data text) → boolean
```

**Parameters:**
- `data`: Data to replicate

**Returns:** `true` if entry was successfully replicated to quorum.

**Example:**
```sql
SELECT pgraft_replicate_entry('{"action": "user_update", "user_id": 123}');
```

## Monitoring & Debugging

### `pgraft_get_worker_state()`

Get the background worker's current state.

**Syntax:**
```sql
pgraft_get_worker_state() → text
```

**Returns:** Worker state as text (e.g., "RUNNING", "STOPPED").

**Example:**
```sql
SELECT pgraft_get_worker_state();
```

---

### `pgraft_get_queue_status()`

Get command queue status.

**Syntax:**
```sql
pgraft_get_queue_status() → TABLE(...)
```

**Returns:** Table with queue statistics.

---

### `pgraft_get_version()`

Get pgraft extension version.

**Syntax:**
```sql
pgraft_get_version() → text
```

**Returns:** Version string.

**Example:**
```sql
SELECT pgraft_get_version();
```

---

### `pgraft_set_debug()`

Enable or disable debug mode.

**Syntax:**
```sql
pgraft_set_debug(enabled boolean) → boolean
```

**Parameters:**
- `enabled`: `true` to enable debug logging, `false` to disable

**Returns:** `true` if successful.

**Example:**
```sql
SELECT pgraft_set_debug(true);  -- Enable debug logging
SELECT pgraft_set_debug(false); -- Disable debug logging
```

---

### `pgraft_test()`

Test function for verifying pgraft is working.

**Syntax:**
```sql
pgraft_test() → boolean
```

**Returns:** `true` if test successful.

**Example:**
```sql
SELECT pgraft_test();
```

## Usage Examples

### Health Check Script

```sql
-- Complete health check
SELECT 
    pgraft_is_leader() as is_leader,
    pgraft_get_term() as term,
    pgraft_get_leader() as leader_id,
    pgraft_get_worker_state() as worker_state;

-- Get cluster details
SELECT * FROM pgraft_get_cluster_status();
SELECT * FROM pgraft_get_nodes();
```

### Leader Election Check

```sql
-- Wait for leader election
DO $$
DECLARE
    leader_id bigint;
    attempts int := 0;
BEGIN
    LOOP
        leader_id := pgraft_get_leader();
        EXIT WHEN leader_id > 0 OR attempts >= 30;
        PERFORM pg_sleep(1);
        attempts := attempts + 1;
    END LOOP;
    
    IF leader_id > 0 THEN
        RAISE NOTICE 'Leader elected: node %', leader_id;
    ELSE
        RAISE EXCEPTION 'No leader elected after 30 seconds';
    END IF;
END $$;
```

### Add Multiple Nodes

```sql
-- Add nodes (must run on leader)
DO $$
BEGIN
    IF NOT pgraft_is_leader() THEN
        RAISE EXCEPTION 'Must run on leader node';
    END IF;
    
    PERFORM pgraft_add_node(2, '127.0.0.1', 7002);
    PERFORM pgraft_add_node(3, '127.0.0.1', 7003);
    
    RAISE NOTICE 'Nodes added successfully';
END $$;
```

