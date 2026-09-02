# SQL Functions & Tables Reference

Complete reference for all SQL functions, tables, and views provided by **pgraft**.

---

## Core Tables

### `pgraft.kv`
Raft-replicated key-value store (etcd-compatible).

| Column      | Type            | Description                |
|-------------|-----------------|----------------------------|
| key         | text            | Primary key                |
| value       | text            | Value for the key          |
| version     | bigint          | Version number             |
| created_at  | timestamptz     | Creation timestamp         |
| updated_at  | timestamptz     | Last update timestamp      |

### `pgraft.applied_entries`
Tracks which Raft log entries have been applied to PostgreSQL.

| Column      | Type            | Description                |
|-------------|-----------------|----------------------------|
| raft_index  | bigint          | Raft log index (PK)        |
| raft_term   | bigint          | Raft term                  |
| entry_type  | integer         | Entry type                 |
| applied_at  | timestamptz     | When applied               |

### `pgraft.log_index_mapping`
Maps Raft log index to PostgreSQL operation for debugging/recovery.

| Column         | Type            | Description             |
|----------------|-----------------|-------------------------|
| raft_index     | bigint          | Raft log index (PK)     |
| operation_type | text            | Operation type          |
| target_table   | text            | Target table            |
| operation_data | jsonb           | Operation data          |
| applied_at     | timestamptz     | When applied            |

---

## Cluster Management Functions

### `pgraft.init()`
Initialize pgraft using GUC variables from `postgresql.conf`.

```sql
SELECT pgraft.init();
```

**Returns:** `boolean` - `true` on success

!!! warning "Deprecated"
    This function is deprecated. The extension now auto-initializes from `postgresql.conf` settings when loaded via `shared_preload_libraries`.

---

### `pgraft.add_node(node_id integer, address text, port integer)`
Add a node to the Raft cluster.

```sql
-- Must be called on the leader
SELECT pgraft.add_node(2, '192.168.1.102', 7002);
```

**Parameters:**
- `node_id` - Unique node identifier
- `address` - IP address or hostname
- `port` - Raft communication port

**Returns:** `boolean` - `true` on success

!!! note "Leader Only"
    This function must be called on the leader node. It automatically replicates to all cluster members.

---

### `pgraft.remove_node(node_id integer)`
Remove a node from the Raft cluster.

```sql
SELECT pgraft.remove_node(3);
```

**Returns:** `boolean` - `true` on success

!!! warning "Leader Only"
    Must be called on the leader node.

---

### `pgraft.get_cluster_status()`
Get comprehensive cluster status information.

```sql
SELECT * FROM pgraft.get_cluster_status();
```

**Returns TABLE:**

| Column               | Type    | Description                     |
|----------------------|---------|--------------------------------|
| node_id              | integer | Current node's ID              |
| current_term         | bigint  | Current Raft term              |
| leader_id            | bigint  | Current leader's ID            |
| state                | text    | Node state (leader/follower)   |
| num_nodes            | integer | Number of cluster nodes        |
| messages_processed   | bigint  | Total messages processed       |
| heartbeats_sent      | bigint  | Heartbeats sent                |
| elections_triggered  | bigint  | Elections triggered            |

---

### `pgraft.get_nodes()`
Get list of all nodes in the cluster.

```sql
SELECT * FROM pgraft.get_nodes();
```

**Returns TABLE:**

| Column     | Type    | Description              |
|------------|---------|--------------------------|
| node_id    | integer | Node ID                  |
| address    | text    | Node address             |
| port       | integer | Raft communication port  |
| is_leader  | boolean | Is this node the leader? |

---

### `pgraft.get_nodes_from_raft()`
Get nodes directly from Raft cluster state (works on replicas).

```sql
SELECT pgraft.get_nodes_from_raft();
```

**Returns:** `text` - JSON array of nodes

---

### `pgraft.is_leader()`
Check if current node is the Raft leader.

```sql
SELECT pgraft.is_leader();
```

**Returns:** `boolean` - `true` if leader, `false` otherwise

---

### `pgraft.get_leader()`
Get the current leader's node ID.

```sql
SELECT pgraft.get_leader();
```

**Returns:** `bigint` - Leader node ID (0 if no leader)

---

### `pgraft.get_term()`
Get the current Raft term.

```sql
SELECT pgraft.get_term();
```

**Returns:** `bigint` - Current term number

---

### `pgraft.get_worker_state()`
Get background worker status information.

```sql
SELECT pgraft.get_worker_state();
```

**Returns:** `text` - JSON object with worker state

---

### `pgraft.get_version()`
Get pgraft extension version.

```sql
SELECT pgraft.get_version();
```

**Returns:** `text` - Version string

---

## Diagnostic & Debug Functions

### `pgraft.test()`
Run internal diagnostic tests.

```sql
SELECT pgraft.test();
```

**Returns:** `boolean` - Test result

---

### `pgraft.set_debug(enabled boolean)`
Enable or disable debug logging.

```sql
SELECT pgraft.set_debug(true);
```

**Returns:** `void`

---

## Log Replication Functions

### `pgraft.log_append(term bigint, data text)`
Append an entry to the Raft log.

```sql
SELECT pgraft.log_append(1, 'test data');
```

**Returns:** `bigint` - Log index

---

### `pgraft.log_commit(index bigint)`
Mark a log entry as committed.

```sql
SELECT pgraft.log_commit(100);
```

**Returns:** `boolean`

---

### `pgraft.log_apply(index bigint)`
Apply a committed log entry.

```sql
SELECT pgraft.log_apply(100);
```

**Returns:** `boolean`

---

### `pgraft.log_get_entry(index bigint)`
Get a specific log entry.

```sql
SELECT pgraft.log_get_entry(100);
```

**Returns:** `text` - JSON object with entry data

---

### `pgraft.log_get_stats()`
Get log replication statistics.

```sql
SELECT * FROM pgraft.log_get_stats();
```

**Returns TABLE:**

| Column         | Type   | Description        |
|----------------|--------|--------------------|
| total_entries  | bigint | Total log entries  |
| committed_index| bigint | Committed index    |
| applied_index  | bigint | Applied index      |
| snapshot_index | bigint | Last snapshot index|

---

### `pgraft.log_get_replication_status()`
Get replication status for all nodes.

```sql
SELECT * FROM pgraft.log_get_replication_status();
```

**Returns TABLE:**

| Column      | Type    | Description              |
|-------------|---------|--------------------------|
| node_id     | integer | Node ID                  |
| match_index | bigint  | Replicated index         |
| next_index  | bigint  | Next index to send       |
| is_active   | boolean | Is node active?          |

---

### `pgraft.log_sync_with_leader()`
Synchronize log with leader (follower only).

```sql
SELECT pgraft.log_sync_with_leader();
```

**Returns:** `boolean`

---

## Key-Value Store Functions

### `pgraft.kv_put(key text, value text)`
Store a key-value pair (Raft-replicated).

```sql
SELECT pgraft.kv_put('config/setting', 'value123');
```

**Returns:** `boolean` - `true` on success

!!! note "Leader Only"
    Must be called on the leader node.

---

### `pgraft.kv_get(key text)`
Retrieve a value by key.

```sql
SELECT pgraft.kv_get('config/setting');
```

**Returns:** `text` - Value or NULL if not found

---

### `pgraft.kv_delete(key text)`
Delete a key-value pair (Raft-replicated).

```sql
SELECT pgraft.kv_delete('config/setting');
```

**Returns:** `boolean`

!!! note "Leader Only"
    Must be called on the leader node.

---

### `pgraft.kv_exists(key text)`
Check if a key exists.

```sql
SELECT pgraft.kv_exists('config/setting');
```

**Returns:** `boolean`

---

### `pgraft.kv_list_keys()`
List all keys in the store.

```sql
SELECT pgraft.kv_list_keys();
```

**Returns:** `SETOF text` - List of keys

---

### `pgraft.kv_get_stats()`
Get key-value store statistics.

```sql
SELECT * FROM pgraft.kv_get_stats();
```

**Returns TABLE:**

| Column      | Type    | Description          |
|-------------|---------|----------------------|
| total_keys  | bigint  | Number of keys       |
| total_size  | bigint  | Total size in bytes  |

---

### `pgraft.kv_compact()`
Compact the key-value store.

```sql
SELECT pgraft.kv_compact();
```

**Returns:** `boolean`

---

### `pgraft.kv_reset()`
Reset/clear the key-value store.

```sql
SELECT pgraft.kv_reset();
```

**Returns:** `boolean`

!!! danger "Destructive Operation"
    This deletes all key-value data. Use with caution.

---

## Internal Functions

### `pgraft.replicate_entry(entry_data text)`
Replicate a log entry (internal use).

```sql
SELECT pgraft.replicate_entry('{"type":"config","data":"..."}');
```

**Returns:** `boolean`

---

### `pgraft.kv_put_local(key text, value text)`
Put key-value locally without replication (internal use).

```sql
SELECT pgraft.kv_put_local('local/cache', 'value');
```

**Returns:** `void`

---

### `pgraft.kv_delete_local(key text)`
Delete key locally without replication (internal use).

```sql
SELECT pgraft.kv_delete_local('local/cache');
```

**Returns:** `void`

---

### `pgraft.get_applied_index()`
Get the last applied log index.

```sql
SELECT pgraft.get_applied_index();
```

**Returns:** `bigint`

---

### `pgraft.record_applied_index(index bigint)`
Record that a log index has been applied.

```sql
SELECT pgraft.record_applied_index(100);
```

**Returns:** `void`

---

### `pgraft.get_queue_status()`
Get message queue status.

```sql
SELECT * FROM pgraft.get_queue_status();
```

**Returns TABLE:**

| Column         | Type    | Description              |
|----------------|---------|--------------------------|
| pending_msgs   | integer | Pending messages         |
| processed_msgs | bigint  | Processed messages       |
| queue_full     | boolean | Is queue full?           |

---

## Usage Examples

### Check Cluster Health

```sql
-- Quick health check
SELECT 
    node_id,
    state,
    leader_id,
    num_nodes
FROM pgraft.get_cluster_status();

-- Verify all nodes are present
SELECT * FROM pgraft.get_nodes();
```

### Store and Retrieve Data

```sql
-- On leader: Store configuration
SELECT pgraft.kv_put('app/config', '{"timeout":30,"retries":3}');

-- On any node: Retrieve configuration
SELECT pgraft.kv_get('app/config');
```

### Monitor Replication

```sql
-- Check replication status
SELECT * FROM pgraft.log_get_replication_status();

-- View log statistics
SELECT * FROM pgraft.log_get_stats();
```

---

## Function Categories

| Category             | Functions                                                              |
|----------------------|------------------------------------------------------------------------|
| Cluster Management   | `pgraft.init`, `pgraft.add_node`, `pgraft.remove_node`                 |
| Status & Monitoring  | `pgraft.get_cluster_status`, `pgraft.get_nodes`, `pgraft.is_leader`    |
| Log Replication      | `pgraft.log_*` functions                                               |
| Key-Value Store      | `pgraft.kv_*` functions                                                |
| Diagnostics          | `pgraft.test`, `pgraft.set_debug`, `pgraft.get_queue_status`           |

---

## Best Practices

1. **Leader-Only Operations**: Always check `pgraft.is_leader()` before calling functions that modify cluster state
2. **Read Operations**: Can be performed on any node (leader or follower)
3. **Error Handling**: Wrap cluster operations in exception handlers
4. **Monitoring**: Regularly check `pgraft.get_cluster_status()` and `pgraft.log_get_replication_status()`

```sql
-- Example: Safe node addition
DO $$
BEGIN
    IF pgraft.is_leader() THEN
        PERFORM pgraft.add_node(4, '192.168.1.104', 7004);
        RAISE NOTICE 'Node added successfully';
    ELSE
        RAISE EXCEPTION 'Must run on leader node';
    END IF;
END $$;
```
