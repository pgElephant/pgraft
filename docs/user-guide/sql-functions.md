


# SQL Functions, Tables & Views Reference (pgraft)

This page documents all SQL functions, tables, and views available in **pgraft**, part of the [pgElephant](https://pgelephant.com) high-availability suite. All APIs are up to date with the latest release and reflect the current extension SQL.

---


## Core Tables

### `pgraft.kv`
Key-value store table (etcd-compatible, Raft-replicated)

| Column      | Type    | Description                       |
|------------ |---------|-----------------------------------|
| key         | text    | Primary key                       |
| value       | text    | Value for the key                 |
| version     | bigint  | Version number                    |
| created_at  | timestamptz | Creation timestamp           |
| updated_at  | timestamptz | Last update timestamp         |

### `pgraft.applied_entries`
Tracks which Raft log entries have been applied to PostgreSQL.

| Column      | Type    | Description                       |
|------------ |---------|-----------------------------------|
| raft_index  | bigint  | Raft log index (PK)               |
| raft_term   | bigint  | Raft term                         |
| entry_type  | integer | Entry type                        |
| applied_at  | timestamptz | When applied                  |

### `pgraft.log_index_mapping`
Maps Raft log index to PostgreSQL operation for debugging/recovery.

| Column         | Type    | Description                    |
|--------------- |---------|--------------------------------|
| raft_index     | bigint  | Raft log index (PK)            |
| operation_type | text    | Operation type                 |
| target_table   | text    | Target table                   |
| operation_data | jsonb   | Operation data                 |
| applied_at     | timestamptz | When applied               |

---

## Cluster Management Functions

### `pgraft_init()`
Initialize pgraft on the current node.

**Returns:** `boolean` — `true` if successful.

---

### `pgraft_add_node(node_id int, address text, port int)`
Add a node to the cluster (leader only).

---

### `pgraft_remove_node(node_id int)`
Remove a node from the cluster.

---

### `pgraft_get_cluster_status()`
Returns a table with cluster status (node_id, current_term, leader_id, state, num_nodes, etc).

---

### `pgraft_get_nodes()`
Returns a table of all cluster nodes (node_id, address, port, is_leader).

---

### `pgraft_is_leader()`
Returns `boolean` — true if current node is leader.

---

### `pgraft_get_worker_state()`
Returns `text` — background worker state (e.g., "RUNNING").

---

### `pgraft_get_version()`
Returns `text` — extension version.

---

### `pgraft_set_debug(enabled boolean)`
Enable or disable debug logging.

---

### `pgraft_test()`
Test function for verifying pgraft is working.

---

---

## Log Replication Functions

### `pgraft_log_append(term bigint, data text)`
Append a log entry.

---

### `pgraft_log_commit(index bigint)`
Commit a log entry.

---

### `pgraft_log_apply(index bigint)`
Apply a log entry to the state machine.

---

### `pgraft_log_get_entry(index bigint)`
Get a specific log entry (returns text).

---

### `pgraft_log_get_stats()`
Returns table with log statistics (log_size, last_index, commit_index, last_applied, ...).

---

### `pgraft_log_get_replication_status()`
Returns table with replication status for each follower.

---

### `pgraft_log_sync_with_leader()`
Synchronize local log with the leader.

---

### `pgraft_replicate_entry(entry_data text)`
Replicate an entry through Raft consensus.

---

---

## Key/Value Store Functions (etcd-compatible)

### `pgraft_kv_put(key text, value text)`
Store a key/value pair. Returns `boolean`.

---

### `pgraft_kv_get(key text)`
Retrieve value for a key. Returns `text`.

---

### `pgraft_kv_delete(key text)`
Delete a key. Returns `boolean`.

---

### `pgraft_kv_exists(key text)`
Check if a key exists. Returns `boolean`.

---

### `pgraft_kv_list_keys()`
List all keys as a JSON array. Returns `text`.

---

### `pgraft_kv_get_stats()`
Returns table with key/value store statistics (num_entries, total_operations, puts, gets, deletes, ...).

---

### `pgraft_kv_compact()`
Remove deleted entries and optimize storage. Returns `boolean`.

---

### `pgraft_kv_reset()`
Clear all key/value data (use with caution!). Returns `boolean`.

---

---

## Monitoring & Debugging Functions

### `pgraft_get_queue_status()`
Returns table with command queue status.

---

---

## etcd-Compatible Views


The following views provide etcd-style cluster and key-value status for compatibility with etcd tools:

- `pgraft.member_list` — etcdctl member list format (shows all cluster members, peer/client URLs, leader/follower status)
- `pgraft.endpoint_status` — etcdctl endpoint status format (endpoint, isLeader, raftTerm, raftIndex, etc.)
- `pgraft.endpoint_health` — etcdctl endpoint health format (endpoint, health, took)
- `pgraft.cluster_health` — etcdctl cluster-health format (member, isLeader, isLearner, health)
- `pgraft.cluster_info` — etcdctl cluster info format (clusterID, memberCount, leader, raftTerm, raftIndex, raftAppliedIndex)
- `pgraft.kv_status` — etcdctl key-value status (key, value, version, create_revision, mod_revision)
- `pgraft.endpoint_hashkv` — etcdctl endpoint hashkv format (endpoint, hash, hash_revision)
- `pgraft.watch_status` — etcdctl watch status (watcher_id, is_active, watch_count, watch_pending)
- `pgraft.member_details` — etcdctl member details (ID, Name, PeerURLs, ClientURLs, IsLeader, IsLearner)
- `pgraft.auth_status` — etcdctl auth status (enabled, revision)
- `pgraft.alarm_list` — etcdctl alarm list (alarm, memberID)
- `pgraft.snapshot_status` — etcdctl snapshot status (hash, revision, total_key, total_size, version)

- `pgraft.member_list` — etcdctl member list format
- `pgraft.endpoint_status` — etcdctl endpoint status format
- `pgraft.endpoint_health` — etcdctl endpoint health format
- `pgraft.cluster_health` — etcdctl cluster-health format
- `pgraft.cluster_info` — etcdctl cluster info format
- `pgraft.kv_status` — etcdctl key-value status
- `pgraft.endpoint_hashkv` — etcdctl endpoint hashkv format
- `pgraft.watch_status` — etcdctl watch status
- `pgraft.member_details` — etcdctl member details
- `pgraft.auth_status` — etcdctl auth status
- `pgraft.alarm_list` — etcdctl alarm list
- `pgraft.snapshot_status` — etcdctl snapshot status

**Example:**
```sql
SELECT * FROM pgraft.member_list;
SELECT * FROM pgraft.endpoint_status;
SELECT * FROM pgraft.kv_status;
```

---

---

## Internal & Advanced Views

- `pgraft_cluster_state` — Core cluster state (reads from shared memory; combines cluster and worker info)
- `pgraft_worker_status` — Background worker status (worker_state, is_running)
- `pgraft_cluster_overview` — Cluster overview (worker + node status, leader, term, state, etc.)
- `pgraft_nodes` — Node information (with cluster state)
- `pgraft_log_status` — Log replication status (simplified, for quick checks)
- `pgraft_kv_status` — Key/value store status (num_entries, active_entries, deleted_entries, total_operations, puts, gets, deletes, last_applied_index, status)

---

---

## Usage Examples

### Health Check
```sql
SELECT pgraft_is_leader(), pgraft_get_term(), pgraft_get_leader(), pgraft_get_worker_state();
SELECT * FROM pgraft_get_cluster_status();
SELECT * FROM pgraft_get_nodes();
```

### Key/Value Store
```sql
SELECT pgraft_kv_put('foo', 'bar');
SELECT pgraft_kv_get('foo');
SELECT pgraft_kv_delete('foo');
SELECT * FROM pgraft_kv_status;
```

### etcd-Compatible Views
```sql
SELECT * FROM pgraft.member_list;
SELECT * FROM pgraft.endpoint_status;
SELECT * FROM pgraft.cluster_health;
```

### Add Nodes (Leader Only)
```sql
DO $$
BEGIN
    IF NOT pgraft_is_leader() THEN
        RAISE EXCEPTION 'Must run on leader node';
    END IF;
    PERFORM pgraft_add_node(2, '127.0.0.1', 7002);
    PERFORM pgraft_add_node(3, '127.0.0.1', 7003);
END $$;
```

