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

-- Get current leader ID
CREATE OR REPLACE FUNCTION pgraft_get_leader()
RETURNS bigint
LANGUAGE C
AS 'pgraft', 'pgraft_get_leader';

-- Get current term
CREATE OR REPLACE FUNCTION pgraft_get_term()
RETURNS integer
LANGUAGE C
AS 'pgraft', 'pgraft_get_term';

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

-- Get cluster nodes as table with individual columns
CREATE OR REPLACE FUNCTION pgraft_get_nodes()
RETURNS TABLE(
    node_id integer,
    address text,
    port integer,
    is_leader boolean
)
LANGUAGE C
AS 'pgraft', 'pgraft_get_nodes_table';

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

-- ============================================================================
-- JSON Wrapper Functions for Testing
-- ============================================================================

-- Get cluster status as JSON
CREATE OR REPLACE FUNCTION pgraft_get_cluster_status_json()
RETURNS text
LANGUAGE SQL
AS $$
    SELECT json_build_object(
        'node_id', node_id,
        'term', current_term,
        'leader_id', leader_id,
        'state', state,
        'num_nodes', num_nodes,
        'messages_processed', messages_processed,
        'heartbeats_sent', heartbeats_sent,
        'elections_triggered', elections_triggered
    )::text
    FROM pgraft_get_cluster_status();
$$;

-- Get log status as JSON  
CREATE OR REPLACE FUNCTION pgraft_get_log_status_json()
RETURNS text
LANGUAGE SQL
AS $$
    SELECT json_build_object(
        'log_size', log_size,
        'last_index', last_index,
        'commit_index', commit_index,
        'last_applied', last_applied,
        'entries_replicated', replicated,
        'entries_committed', committed,
        'entries_applied', applied,
        'replication_errors', errors
    )::text
    FROM pgraft_log_get_replication_status();
$$;

-- Replicate a log entry via the Raft leader
CREATE OR REPLACE FUNCTION pgraft_replicate_entry(entry_data text)
RETURNS boolean
LANGUAGE C
AS 'pgraft', 'pgraft_replicate_entry_func';

-- Reset search path
SET search_path = public;