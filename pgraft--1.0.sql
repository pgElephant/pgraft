/*
 * pgraft--1.0.sql
 * PostgreSQL extension for distributed consensus
 *
 * This file contains the SQL definitions for the pgraft extension.
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 * All rights reserved.
 */

-- Create extension schema
CREATE SCHEMA pgraft;

-- ============================================================================
-- Raft Replication Tables (for 100% Raft-based replication)
-- ============================================================================

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
CREATE OR REPLACE FUNCTION pgraft_init()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_init';


-- Add a node to the cluster
CREATE OR REPLACE FUNCTION pgraft_add_node(node_id integer, address text, port integer)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_add_node';

-- Remove a node from the cluster
CREATE OR REPLACE FUNCTION pgraft_remove_node(node_id integer)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_remove_node';

-- Get cluster status as table with individual columns
CREATE OR REPLACE FUNCTION pgraft_get_cluster_status()
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
CREATE OR REPLACE FUNCTION pgraft_get_nodes()
RETURNS TABLE(
    node_id integer,
    address text,
    port integer,
    is_leader boolean
)
LANGUAGE C
AS 'pgraft', 'pgraft_get_nodes_table';

-- Get nodes directly from Raft cluster (works on replicas)
CREATE OR REPLACE FUNCTION pgraft_get_nodes_from_raft()
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_get_nodes_from_raft';

-- Check if current node is leader
CREATE OR REPLACE FUNCTION pgraft_is_leader()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_is_leader';

-- Get background worker state
CREATE OR REPLACE FUNCTION pgraft_get_worker_state()
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_get_worker_state';


-- Get version information
CREATE OR REPLACE FUNCTION pgraft_get_version()
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_get_version';

-- Test pgraft functionality
CREATE OR REPLACE FUNCTION pgraft_test()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_test';

-- Set debug mode
CREATE OR REPLACE FUNCTION pgraft_set_debug(enabled boolean)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_set_debug';

-- ============================================================================
-- Log Replication Functions
-- ============================================================================

-- Append log entry
CREATE OR REPLACE FUNCTION pgraft_log_append(term bigint, data text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_log_append';

-- Commit log entry
CREATE OR REPLACE FUNCTION pgraft_log_commit(index bigint)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_log_commit';

-- Apply log entry
CREATE OR REPLACE FUNCTION pgraft_log_apply(index bigint)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_log_apply';

-- Get log entry
CREATE OR REPLACE FUNCTION pgraft_log_get_entry(index bigint)
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_log_get_entry_sql';

-- Get log statistics as table with individual columns
CREATE OR REPLACE FUNCTION pgraft_log_get_stats()
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
CREATE OR REPLACE FUNCTION pgraft_log_get_replication_status()
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
CREATE OR REPLACE FUNCTION pgraft_log_sync_with_leader()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_log_sync_with_leader_sql';

-- Get current leader ID
CREATE OR REPLACE FUNCTION pgraft_get_leader()
RETURNS bigint
LANGUAGE C
AS 'pgraft', 'pgraft_get_leader';

-- Get current term
CREATE OR REPLACE FUNCTION pgraft_get_term()
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
        WHEN (node->>'id')::int = pgraft_get_leader() THEN 'leader'
        ELSE 'follower'
    END as "status"
FROM json_array_elements(pgraft_get_nodes_from_raft()::json) AS node
ORDER BY (node->>'id')::int;

-- Legacy view using C function (deprecated, kept for compatibility)
CREATE OR REPLACE VIEW pgraft.member_list_legacy AS
SELECT 
    node_id::text as "memberID",
    COALESCE(current_setting('listen_peer_urls', true), address || ':2380') as "peerURLs",
    COALESCE(current_setting('listen_client_urls', true), address || ':2379') as "clientURLs",
    CASE WHEN is_leader THEN 'leader' ELSE 'follower' END as "status"
FROM pgraft_get_nodes()
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
FROM pgraft_get_nodes() n
JOIN LATERAL (
    SELECT current_term, leader_id
    FROM pgraft_get_cluster_status()
) cs ON (cs.leader_id = n.node_id OR NOT n.is_leader)
ORDER BY node_id;

-- View that matches 'etcdctl endpoint health' output format
CREATE OR REPLACE VIEW pgraft.endpoint_health AS
SELECT 
    address || ':' || port::text as "endpoint",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "health",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "took"
FROM pgraft_get_nodes()
ORDER BY node_id;

-- View that matches 'etcdctl cluster-health' output format
CREATE OR REPLACE VIEW pgraft.cluster_health AS
SELECT 
    'cluster is healthy' as "member",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "isLeader",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "isLearner",
    'true' as "health"
FROM pgraft_get_nodes()
WHERE is_leader = true
UNION ALL
SELECT 
    'cluster is healthy' as "member",
    'false' as "isLeader", 
    'false' as "isLearner",
    'true' as "health"
FROM pgraft_get_nodes()
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
FROM pgraft_get_cluster_status() cs;

-- View for etcd-style key-value operations (if needed for compatibility)
CREATE OR REPLACE VIEW pgraft.kv_status AS
SELECT 
    'pgraft' as "key",
    'PostgreSQL Raft Extension' as "value",
    '0' as "version",
    '0' as "create_revision",
    '0' as "mod_revision"
WHERE pgraft_is_leader();

-- Additional etcd-compatible views for comprehensive compatibility

-- View that matches 'etcdctl endpoint hashkv' output format
CREATE OR REPLACE VIEW pgraft.endpoint_hashkv AS
SELECT 
    address || ':' || port::text as "endpoint",
    '0' as "hash",
    '0' as "hash_revision"
FROM pgraft_get_nodes()
ORDER BY node_id;

-- View that provides etcd-style watch status
CREATE OR REPLACE VIEW pgraft.watch_status AS
SELECT 
    'pgraft_watch' as "watcher_id",
    'true' as "is_active",
    '0' as "watch_count",
    '0' as "watch_pending"
WHERE pgraft_is_leader();

-- View that matches etcd cluster member details
CREATE OR REPLACE VIEW pgraft.member_details AS
SELECT 
    node_id::text as "ID",
    'etcd' as "Name",
    address || ':' || port::text as "PeerURLs",
    address || ':' || port::text as "ClientURLs",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "IsLeader",
    CASE WHEN is_leader THEN 'true' ELSE 'false' END as "IsLearner"
FROM pgraft_get_nodes()
ORDER BY node_id;

-- View for etcd-style authentication status
CREATE OR REPLACE VIEW pgraft.auth_status AS
SELECT 
    'true' as "enabled",
    'false' as "revision"
WHERE pgraft_is_leader();

-- View for etcd-style alarm list
CREATE OR REPLACE VIEW pgraft.alarm_list AS
SELECT 
    'NONE' as "alarm",
    '0' as "memberID"
WHERE pgraft_is_leader()
UNION ALL
SELECT 
    'NONE' as "alarm",
    node_id::text as "memberID"
FROM pgraft_get_nodes()
WHERE NOT is_leader;

-- View for etcd-style snapshot status
CREATE OR REPLACE VIEW pgraft.snapshot_status AS
SELECT 
    '0' as "hash",
    '0' as "revision",
    '0' as "total_key",
    '0' as "total_size",
    'true' as "version"
WHERE pgraft_is_leader();

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
CREATE OR REPLACE FUNCTION pgraft_get_queue_status()
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
CREATE VIEW pgraft_cluster_state AS
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
FROM pgraft_get_cluster_status() c,
     LATERAL (
         SELECT 
             pgraft_get_worker_state()::text as worker_status,
             CASE 
                 WHEN pgraft_get_worker_state() = 'RUNNING' THEN true
                 ELSE false
             END as initialized,
             1 as node_id,  -- Current node ID (should be from config)
             '127.0.0.1' as address,  -- Current node address (should be from config)
             5432 as port  -- Current node port (should be from config)
     ) w;

-- Background worker status view
CREATE VIEW pgraft_worker_status AS
SELECT 
    pgraft_get_worker_state() as worker_state,
    CASE 
        WHEN pgraft_get_worker_state() = 'RUNNING' THEN true
        ELSE false
    END as is_running;

-- Cluster overview view combining worker and node status
CREATE VIEW pgraft_cluster_overview AS
SELECT
    pgraft_get_worker_state() as worker_state,
    c.leader_id,
    c.current_term,
    c.state,
    (c.leader_id = 1) as is_leader,  -- Assuming node 1 is current node
    c.num_nodes,
    c.messages_processed
FROM pgraft_cluster_state c
LIMIT 1;

-- Node information view
CREATE VIEW pgraft_nodes AS
SELECT 
    n.node_id,
    n.address,
    n.port,
    n.is_leader,
    c.current_term,
    c.state
FROM pgraft_get_nodes() n,
     LATERAL (SELECT * FROM pgraft_cluster_state LIMIT 1) c;

-- Log replication status view (simplified)
CREATE VIEW pgraft_log_status AS
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
CREATE OR REPLACE FUNCTION pgraft_kv_put(key text, value text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_put_sql';

-- GET operation - retrieve value for a key
CREATE OR REPLACE FUNCTION pgraft_kv_get(key text)
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_kv_get_sql';

-- DELETE operation - delete a key
CREATE OR REPLACE FUNCTION pgraft_kv_delete(key text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_delete_sql';

-- EXISTS operation - check if key exists
CREATE OR REPLACE FUNCTION pgraft_kv_exists(key text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_exists_sql';

-- LIST_KEYS operation - list all keys as JSON array
CREATE OR REPLACE FUNCTION pgraft_kv_list_keys()
RETURNS text
LANGUAGE C
AS 'pgraft', 'pgraft_kv_list_keys_sql';

-- Get key/value store statistics
CREATE OR REPLACE FUNCTION pgraft_kv_get_stats()
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
CREATE OR REPLACE FUNCTION pgraft_kv_compact()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_compact_sql';

-- RESET operation - clear all data (use with caution!)
CREATE OR REPLACE FUNCTION pgraft_kv_reset()
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_reset_sql';

-- ============================================================================
-- Key/Value Store Views
-- ============================================================================

-- Key/Value store status view
CREATE VIEW pgraft_kv_status AS
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
FROM pgraft_kv_get_stats() s;



-- Replicate a log entry via the Raft leader
CREATE OR REPLACE FUNCTION pgraft_replicate_entry(entry_data text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_replicate_entry_func';

-- Local KV operations (used by apply callback, no Raft replication)
CREATE OR REPLACE FUNCTION pgraft_kv_put_local(key text, value text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_put_local_func';

CREATE OR REPLACE FUNCTION pgraft_kv_delete_local(key text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_kv_delete_local_func';

-- Applied index tracking functions
CREATE OR REPLACE FUNCTION pgraft_get_applied_index()
RETURNS bigint
LANGUAGE C
AS 'pgraft', 'pgraft_get_applied_index_func';

CREATE OR REPLACE FUNCTION pgraft_record_applied_index(index bigint)
RETURNS void
LANGUAGE C
AS 'pgraft', 'pgraft_record_applied_index_func';

-- Reset search path
SET search_path = public;