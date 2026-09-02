/* pgraft--1.0--2.0.0.sql */
-- Upgrade pgraft from 1.0 to 2.0.0
--
-- 2.0.0 makes two changes that this script has to carry out:
--   * every object moves into the pgraft schema and loses the redundant
--     pgraft_ prefix, so the old unqualified objects are dropped and the new
--     ones created, and
--   * the privilege model changes.
--
-- The privilege change is the substantive one: in 1.0 every function was
-- executable by PUBLIC because that is PostgreSQL's default for a new
-- function, which let any logged-in role rewrite cluster membership, push
-- entries into the replicated log, or reset the key/value store.  2.0.0
-- revokes those and grants back only read-only introspection.

\echo Use "ALTER EXTENSION pgraft UPDATE TO '2.0.0'" to load this file. \quit

-- ---------------------------------------------------------------------------
-- 2.0.0 moves every object into the pgraft schema and drops the redundant
-- pgraft_ prefix, so everything 1.0 created has to go first.
--
-- The views come down in reverse creation order, so a view is always dropped
-- before the one it selects from, and the functions follow.  No CASCADE
-- anywhere: an object of yours that depends on a pgraft function should stop
-- the upgrade, not disappear with it.
-- ---------------------------------------------------------------------------

DROP VIEW IF EXISTS pgraft_kv_status;
DROP VIEW IF EXISTS pgraft_log_status;
DROP VIEW IF EXISTS pgraft_nodes;
DROP VIEW IF EXISTS pgraft_cluster_overview;
DROP VIEW IF EXISTS pgraft_worker_status;
DROP VIEW IF EXISTS pgraft_cluster_state;
DROP VIEW IF EXISTS pgraft.snapshot_status;
DROP VIEW IF EXISTS pgraft.alarm_list;
DROP VIEW IF EXISTS pgraft.auth_status;
DROP VIEW IF EXISTS pgraft.member_details;
DROP VIEW IF EXISTS pgraft.watch_status;
DROP VIEW IF EXISTS pgraft.endpoint_hashkv;
DROP VIEW IF EXISTS pgraft.kv_status;
DROP VIEW IF EXISTS pgraft.cluster_info;
DROP VIEW IF EXISTS pgraft.cluster_health;
DROP VIEW IF EXISTS pgraft.endpoint_health;
DROP VIEW IF EXISTS pgraft.endpoint_status;
DROP VIEW IF EXISTS pgraft.member_list_legacy;
DROP VIEW IF EXISTS pgraft.member_list;

DROP FUNCTION IF EXISTS pgraft_init();
DROP FUNCTION IF EXISTS pgraft_add_node(integer, text, integer);
DROP FUNCTION IF EXISTS pgraft_remove_node(integer);
DROP FUNCTION IF EXISTS pgraft_get_cluster_status();
DROP FUNCTION IF EXISTS pgraft_get_nodes();
DROP FUNCTION IF EXISTS pgraft_get_nodes_from_raft();
DROP FUNCTION IF EXISTS pgraft_is_leader();
DROP FUNCTION IF EXISTS pgraft_get_worker_state();
DROP FUNCTION IF EXISTS pgraft_get_version();
DROP FUNCTION IF EXISTS pgraft_test();
DROP FUNCTION IF EXISTS pgraft_set_debug(boolean);
DROP FUNCTION IF EXISTS pgraft_log_append(bigint, text);
DROP FUNCTION IF EXISTS pgraft_log_commit(bigint);
DROP FUNCTION IF EXISTS pgraft_log_apply(bigint);
DROP FUNCTION IF EXISTS pgraft_log_get_entry(bigint);
DROP FUNCTION IF EXISTS pgraft_log_get_stats();
DROP FUNCTION IF EXISTS pgraft_log_get_replication_status();
DROP FUNCTION IF EXISTS pgraft_log_sync_with_leader();
DROP FUNCTION IF EXISTS pgraft_get_leader();
DROP FUNCTION IF EXISTS pgraft_get_term();
DROP FUNCTION IF EXISTS pgraft_get_queue_status();
DROP FUNCTION IF EXISTS pgraft_kv_put(text, text);
DROP FUNCTION IF EXISTS pgraft_kv_get(text);
DROP FUNCTION IF EXISTS pgraft_kv_delete(text);
DROP FUNCTION IF EXISTS pgraft_kv_exists(text);
DROP FUNCTION IF EXISTS pgraft_kv_list_keys();
DROP FUNCTION IF EXISTS pgraft_kv_get_stats();
DROP FUNCTION IF EXISTS pgraft_kv_compact();
DROP FUNCTION IF EXISTS pgraft_kv_reset();
DROP FUNCTION IF EXISTS pgraft_replicate_entry(text);
DROP FUNCTION IF EXISTS pgraft_kv_put_local(text, text);
DROP FUNCTION IF EXISTS pgraft_kv_delete_local(text);
DROP FUNCTION IF EXISTS pgraft_get_applied_index();
DROP FUNCTION IF EXISTS pgraft_record_applied_index(bigint);

-- Key-value store (etcd-compatible)
-- This table stores data replicated via Raft
CREATE TABLE IF NOT EXISTS pgraft.kv (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    version BIGINT NOT NULL DEFAULT 1,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Applied Raft log entries tracking
-- Tracks which entries have been applied to PostgreSQL
CREATE TABLE IF NOT EXISTS pgraft.applied_entries (
    raft_index BIGINT PRIMARY KEY,
    raft_term BIGINT NOT NULL,
    entry_type INTEGER NOT NULL,
    applied_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Index mapping: Raft index → PostgreSQL operation
-- Useful for debugging and crash recovery
CREATE TABLE IF NOT EXISTS pgraft.log_index_mapping (
    raft_index BIGINT PRIMARY KEY,
    operation_type TEXT NOT NULL,
    target_table TEXT,
    operation_data JSONB,
    applied_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- ============================================================================
-- Core Raft Functions
-- ============================================================================

-- Initialize pgraft using GUC variables
CREATE OR REPLACE FUNCTION pgraft.init()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_init';


-- Add a node to the cluster
CREATE OR REPLACE FUNCTION pgraft.add_node(node_id integer, address text, port integer)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_add_node';

-- Remove a node from the cluster
CREATE OR REPLACE FUNCTION pgraft.remove_node(node_id integer)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_remove_node';

-- Get cluster status as table with individual columns
CREATE OR REPLACE FUNCTION pgraft.get_cluster_status()
RETURNS TABLE(
    node_id integer,
    current_term bigint,
    leader_id bigint,
    state text,
    num_nodes integer,
    messages_processed bigint,
    heartbeats_sent bigint,
    elections_triggered bigint
)
LANGUAGE C
AS 'pgraft', 'pgraft_get_cluster_status_table';

-- Get nodes information
CREATE OR REPLACE FUNCTION pgraft.get_nodes()
RETURNS TABLE(
    node_id integer,
    address text,
    port integer,
    is_leader boolean
)
LANGUAGE C
AS 'pgraft', 'pgraft_get_nodes_table';

-- Get nodes directly from Raft cluster (works on replicas)
CREATE OR REPLACE FUNCTION pgraft.get_nodes_from_raft()
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_get_nodes_from_raft';

-- Check if current node is leader
CREATE OR REPLACE FUNCTION pgraft.is_leader()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_is_leader';

-- Get background worker state
CREATE OR REPLACE FUNCTION pgraft.get_worker_state()
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_get_worker_state';


-- Get version information
CREATE OR REPLACE FUNCTION pgraft.get_version()
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_get_version';

-- Test pgraft functionality
CREATE OR REPLACE FUNCTION pgraft.test()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_test';

-- Set debug mode
CREATE OR REPLACE FUNCTION pgraft.set_debug(enabled boolean)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_set_debug';

-- ============================================================================
-- Log Replication Functions
-- ============================================================================

-- Append log entry
CREATE OR REPLACE FUNCTION pgraft.log_append(term bigint, data text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_log_append';

-- Commit log entry
CREATE OR REPLACE FUNCTION pgraft.log_commit(index bigint)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_log_commit';

-- Apply log entry
CREATE OR REPLACE FUNCTION pgraft.log_apply(index bigint)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_log_apply';

-- Get log entry
CREATE OR REPLACE FUNCTION pgraft.log_get_entry(index bigint)
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_log_get_entry_sql';

-- Get log statistics as table with individual columns
CREATE OR REPLACE FUNCTION pgraft.log_get_stats()
RETURNS TABLE(
    log_size bigint,
    last_index bigint,
    commit_index bigint,
    last_applied bigint,
    replicated bigint,
    committed bigint,
    applied bigint,
    errors bigint
)
LANGUAGE C
AS 'pgraft', 'pgraft_log_get_stats_table';

-- Get replication status as table with individual columns
CREATE OR REPLACE FUNCTION pgraft.log_get_replication_status()
RETURNS TABLE(
    log_size bigint,
    last_index bigint,
    commit_index bigint,
    last_applied bigint,
    replicated bigint,
    committed bigint,
    applied bigint,
    errors bigint
)
LANGUAGE C
AS 'pgraft', 'pgraft_log_get_replication_status_table';

-- Sync with leader
CREATE OR REPLACE FUNCTION pgraft.log_sync_with_leader()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_log_sync_with_leader_sql';

-- Get current leader ID
CREATE OR REPLACE FUNCTION pgraft.get_leader()
RETURNS bigint
LANGUAGE C
AS 'pgraft', 'pgraft_get_leader';

-- Get current term
CREATE OR REPLACE FUNCTION pgraft.get_term()
RETURNS bigint
LANGUAGE C
AS 'pgraft', 'pgraft_get_term';

-- ============================================================================
-- etcd-compatible Views
-- ============================================================================

-- View that matches 'etcdctl member list' output format (with dead node detection)
CREATE OR REPLACE VIEW pgraft.member_list AS
SELECT 
    (node->>'id')::text as "memberID",
    (node->>'address')::text as "peerURLs",
    (node->>'address')::text as "clientURLs",
    CASE 
        WHEN (node->>'active')::boolean = false THEN 'unavailable'
        WHEN (node->>'id')::int = pgraft.get_leader() THEN 'leader'
        ELSE 'follower'
    END as "status"
FROM json_array_elements(pgraft.get_nodes_from_raft()::json) AS node
ORDER BY (node->>'id')::int;

-- Legacy view using C function (deprecated, kept for compatibility)
CREATE OR REPLACE VIEW pgraft.member_list_legacy AS
SELECT 
    node_id::text as "memberID",
    COALESCE(current_setting('listen_peer_urls', true), address || ':2380') as "peerURLs",
    COALESCE(current_setting('listen_client_urls', true), address || ':2379') as "clientURLs",
    CASE WHEN is_leader THEN 'leader' ELSE 'follower' END as "status"
FROM pgraft.get_nodes()
ORDER BY node_id;

-- View that matches 'etcdctl endpoint status' output format
CREATE OR REPLACE VIEW pgraft.endpoint_status AS
SELECT 
    COALESCE(current_setting('listen_client_urls', true), address || ':2379') as "endpoint",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "isLeader",
    current_term::text as "raftTerm",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "raftIndex",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "raftAppliedIndex",
    COALESCE(current_setting('data_dir', true), '/var/lib/etcd') as "dbSize",
    COALESCE(current_setting('log_level', true), 'info') as "leader",
    COALESCE(current_setting('heartbeat_interval', true), '100') as "leaderIndex",
    COALESCE(current_setting('election_timeout', true), '1000') as "uptime"
FROM pgraft.get_nodes() n
JOIN LATERAL (
    SELECT current_term, leader_id
    FROM pgraft.get_cluster_status()
) cs ON (cs.leader_id = n.node_id OR NOT n.is_leader)
ORDER BY node_id;

-- View that matches 'etcdctl endpoint health' output format
CREATE OR REPLACE VIEW pgraft.endpoint_health AS
SELECT 
    address || ':' || port::text as "endpoint",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "health",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "took"
FROM pgraft.get_nodes()
ORDER BY node_id;

-- View that matches 'etcdctl cluster-health' output format
CREATE OR REPLACE VIEW pgraft.cluster_health AS
SELECT 
    'cluster is healthy' as "member",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "isLeader",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "isLearner",
    'true' as "health"
FROM pgraft.get_nodes()
WHERE is_leader = true
UNION ALL
SELECT 
    'cluster is healthy' as "member",
    'false' as "isLeader", 
    'false' as "isLearner",
    'true' as "health"
FROM pgraft.get_nodes()
WHERE is_leader = false;

-- View that provides etcd-style cluster information
CREATE OR REPLACE VIEW pgraft.cluster_info AS
SELECT 
    cs.current_term::text as "clusterID",
    cs.num_nodes::text as "memberCount", 
    cs.leader_id::text as "leader",
    cs.state as "raftTerm",
    cs.messages_processed::text as "raftIndex",
    cs.heartbeats_sent::text as "raftAppliedIndex"
FROM pgraft.get_cluster_status() cs;

-- View for etcd-style key-value operations (if needed for compatibility)
CREATE OR REPLACE VIEW pgraft.kv_status AS
SELECT 
    'pgraft' as "key",
    'PostgreSQL Raft Extension' as "value",
    '0' as "version",
    '0' as "create_revision",
    '0' as "mod_revision"
WHERE pgraft.is_leader();

-- Additional etcd-compatible views for comprehensive compatibility

-- View that matches 'etcdctl endpoint hashkv' output format
CREATE OR REPLACE VIEW pgraft.endpoint_hashkv AS
SELECT 
    address || ':' || port::text as "endpoint",
    '0' as "hash",
    '0' as "hash_revision"
FROM pgraft.get_nodes()
ORDER BY node_id;

-- View that provides etcd-style watch status
CREATE OR REPLACE VIEW pgraft.watch_status AS
SELECT 
    'pgraft.watch' as "watcher_id",
    'true' as "is_active",
    '0' as "watch_count",
    '0' as "watch_pending"
WHERE pgraft.is_leader();

-- View that matches etcd cluster member details
CREATE OR REPLACE VIEW pgraft.member_details AS
SELECT 
    node_id::text as "ID",
    'etcd' as "Name",
    address || ':' || port::text as "PeerURLs",
    address || ':' || port::text as "ClientURLs",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "IsLeader",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "IsLearner"
FROM pgraft.get_nodes()
ORDER BY node_id;

-- View for etcd-style authentication status
CREATE OR REPLACE VIEW pgraft.auth_status AS
SELECT 
    'true' as "enabled",
    'false' as "revision"
WHERE pgraft.is_leader();

-- View for etcd-style alarm list
CREATE OR REPLACE VIEW pgraft.alarm_list AS
SELECT 
    'NONE' as "alarm",
    '0' as "memberID"
WHERE pgraft.is_leader()
UNION ALL
SELECT 
    'NONE' as "alarm",
    node_id::text as "memberID"
FROM pgraft.get_nodes()
WHERE NOT is_leader;

-- View for etcd-style snapshot status
CREATE OR REPLACE VIEW pgraft.snapshot_status AS
SELECT 
    '0' as "hash",
    '0' as "revision",
    '0' as "total_key",
    '0' as "total_size",
    'true' as "version"
WHERE pgraft.is_leader();

-- Grant permissions for all views
GRANT SELECT ON pgraft.member_list TO PUBLIC;
GRANT SELECT ON pgraft.member_list_legacy TO PUBLIC;
GRANT SELECT ON pgraft.endpoint_status TO PUBLIC;
GRANT SELECT ON pgraft.endpoint_health TO PUBLIC;
GRANT SELECT ON pgraft.cluster_health TO PUBLIC;
GRANT SELECT ON pgraft.cluster_info TO PUBLIC;
GRANT SELECT ON pgraft.kv_status TO PUBLIC;
GRANT SELECT ON pgraft.endpoint_hashkv TO PUBLIC;
GRANT SELECT ON pgraft.watch_status TO PUBLIC;
GRANT SELECT ON pgraft.member_details TO PUBLIC;
GRANT SELECT ON pgraft.auth_status TO PUBLIC;
GRANT SELECT ON pgraft.alarm_list TO PUBLIC;
GRANT SELECT ON pgraft.snapshot_status TO PUBLIC;

-- Command queue inspection function
CREATE OR REPLACE FUNCTION pgraft.get_queue_status()
RETURNS TABLE(
    cmd_position integer,
    command_type integer,
    node_id integer,
    address text,
    port integer,
    log_data text
)
LANGUAGE C
AS 'pgraft', 'pgraft_get_queue_status';

-- Core cluster state view (reads from shared memory)
CREATE OR REPLACE VIEW pgraft.cluster_state AS
SELECT 
    c.leader_id,
    c.current_term,
    c.state,
    c.num_nodes,
    c.messages_processed,
    c.heartbeats_sent,
    c.elections_triggered,
    w.node_id,
    w.address,
    w.port,
    w.worker_status,
    w.initialized,
    (c.leader_id = w.node_id) as is_leader
FROM pgraft.get_cluster_status() c,
     LATERAL (
         SELECT 
             pgraft.get_worker_state()::text as worker_status,
             CASE 
                 WHEN pgraft.get_worker_state() = 'RUNNING' THEN true
                 ELSE false
             END as initialized,
             1 as node_id,  -- Current node ID (should be from config)
             '127.0.0.1' as address,  -- Current node address (should be from config)
             5432 as port  -- Current node port (should be from config)
     ) w;

-- Background worker status view
CREATE OR REPLACE VIEW pgraft.worker_status AS
SELECT 
    pgraft.get_worker_state() as worker_state,
    CASE 
        WHEN pgraft.get_worker_state() = 'RUNNING' THEN true
        ELSE false
    END as is_running;

-- Cluster overview view combining worker and node status
CREATE OR REPLACE VIEW pgraft.cluster_overview AS
SELECT
    pgraft.get_worker_state() as worker_state,
    c.leader_id,
    c.current_term,
    c.state,
    (c.leader_id = 1) as is_leader,  -- Assuming node 1 is current node
    c.num_nodes,
    c.messages_processed
FROM pgraft.cluster_state c
LIMIT 1;

-- Node information view
CREATE OR REPLACE VIEW pgraft.nodes AS
SELECT 
    n.node_id,
    n.address,
    n.port,
    n.is_leader,
    c.current_term,
    c.state
FROM pgraft.get_nodes() n,
     LATERAL (SELECT * FROM pgraft.cluster_state LIMIT 1) c;

-- Log replication status view (simplified)
CREATE OR REPLACE VIEW pgraft.log_status AS
SELECT
    1 as node_id,
    0 as log_size,
    0 as last_index,
    0 as commit_index,
    0 as last_applied,
    false as replicated,
    false as committed,
    false as applied,
    0 as errors;



-- ============================================================================
-- Key/Value Store Functions (etcd-like interface)
-- ============================================================================

-- PUT operation - store a key/value pair
CREATE OR REPLACE FUNCTION pgraft.kv_put(key text, value text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_put_sql';

-- GET operation - retrieve value for a key
CREATE OR REPLACE FUNCTION pgraft.kv_get(key text)
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_kv_get_sql';

-- DELETE operation - delete a key
CREATE OR REPLACE FUNCTION pgraft.kv_delete(key text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_delete_sql';

-- EXISTS operation - check if key exists
CREATE OR REPLACE FUNCTION pgraft.kv_exists(key text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_exists_sql';

-- LIST_KEYS operation - list all keys as JSON array
CREATE OR REPLACE FUNCTION pgraft.kv_list_keys()
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_kv_list_keys_sql';

-- Get key/value store statistics
CREATE OR REPLACE FUNCTION pgraft.kv_get_stats()
RETURNS TABLE(
    num_entries integer,
    total_operations bigint,
    last_applied_index bigint,
    puts bigint,
    deletes bigint,
    gets bigint,
    active_entries integer,
    deleted_entries integer
)
LANGUAGE C
AS 'pgraft', 'pgraft_kv_get_stats_table';

-- COMPACT operation - remove deleted entries and optimize storage
CREATE OR REPLACE FUNCTION pgraft.kv_compact()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_compact_sql';

-- RESET operation - clear all data (use with caution!)
CREATE OR REPLACE FUNCTION pgraft.kv_reset()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_reset_sql';

-- ============================================================================
-- Key/Value Store Views
-- ============================================================================

-- Key/Value store status view
CREATE OR REPLACE VIEW pgraft.kv_store_status AS
SELECT 
    s.num_entries,
    s.active_entries,
    s.deleted_entries,
    s.total_operations,
    s.puts,
    s.gets,
    s.deletes,
    s.last_applied_index,
    CASE 
        WHEN s.num_entries = 0 THEN 'EMPTY'
        WHEN s.deleted_entries > s.active_entries THEN 'NEEDS_COMPACTION'
        ELSE 'HEALTHY'
    END as status
FROM pgraft.kv_get_stats() s;



-- Replicate a log entry via the Raft leader
CREATE OR REPLACE FUNCTION pgraft.replicate_entry(entry_data text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_replicate_entry_func';

-- Local KV operations (used by apply callback, no Raft replication)
CREATE OR REPLACE FUNCTION pgraft.kv_put_local(key text, value text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_put_local_func';

CREATE OR REPLACE FUNCTION pgraft.kv_delete_local(key text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_delete_local_func';

-- Applied index tracking functions
CREATE OR REPLACE FUNCTION pgraft.get_applied_index()
RETURNS bigint
LANGUAGE C
AS 'pgraft', 'pgraft_get_applied_index_func';

CREATE OR REPLACE FUNCTION pgraft.record_applied_index(index bigint)
RETURNS void
LANGUAGE C
AS 'pgraft', 'pgraft_record_applied_index_func';

-- ---------------------------------------------------------------------------
-- Privileges
--
-- PostgreSQL grants EXECUTE on a new function to PUBLIC by default, which left
-- every one of these callable by any logged-in role: cluster membership could
-- be rewritten with pgraft.remove_node(), arbitrary payloads pushed into the
-- replicated log with pgraft.replicate_entry(), and the key/value store
-- rewritten at will.  Revoke the lot, then grant back only the read-only
-- introspection functions.
-- ---------------------------------------------------------------------------

REVOKE ALL ON FUNCTION pgraft.init() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.add_node(integer, text, integer) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.remove_node(integer) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.set_debug(boolean) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.test() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.log_append(bigint, text) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.log_commit(bigint) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.log_apply(bigint) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.log_sync_with_leader() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.replicate_entry(text) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_put(text, text) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_delete(text) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_put_local(text, text) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_delete_local(text) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_compact() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_reset() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.record_applied_index(bigint) FROM PUBLIC;

-- Read-only introspection: revoke the default grant, then hand it back
-- explicitly so the intent is recorded rather than inherited.
REVOKE ALL ON FUNCTION pgraft.get_cluster_status() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.get_nodes() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.get_nodes_from_raft() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.is_leader() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.get_worker_state() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.get_version() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.get_leader() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.get_term() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.get_queue_status() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.get_applied_index() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.log_get_entry(bigint) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.log_get_stats() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.log_get_replication_status() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_get(text) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_exists(text) FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_list_keys() FROM PUBLIC;
REVOKE ALL ON FUNCTION pgraft.kv_get_stats() FROM PUBLIC;

GRANT EXECUTE ON FUNCTION pgraft.get_cluster_status() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.get_nodes() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.get_nodes_from_raft() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.is_leader() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.get_worker_state() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.get_version() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.get_leader() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.get_term() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.get_queue_status() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.get_applied_index() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.log_get_entry(bigint) TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.log_get_stats() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.log_get_replication_status() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.kv_get(text) TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.kv_exists(text) TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.kv_list_keys() TO PUBLIC;
GRANT EXECUTE ON FUNCTION pgraft.kv_get_stats() TO PUBLIC;

-- Reset search path
SET search_path = public;
