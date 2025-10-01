/*-------------------------------------------------------------------------
 *
 * pgraft_sql.c
 *      SQL interface functions for pgraft
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "utils/elog.h"
#include "utils/builtins.h"
#include "access/htup_details.h"
#include "access/tupdesc.h"
#include "funcapi.h"
#include "utils/typcache.h"
#include "utils/tuplestore.h"
#include "utils/guc.h"

#include "../include/pgraft_sql.h"
#include "../include/pgraft_core.h"
#include "../include/pgraft_go.h"
#include "../include/pgraft_state.h"
#include "../include/pgraft_log.h"
#include "../include/pgraft_guc.h"

/* Function info macros for core functions */
PG_FUNCTION_INFO_V1(pgraft_init);
PG_FUNCTION_INFO_V1(pgraft_init_guc);
PG_FUNCTION_INFO_V1(pgraft_add_node);
PG_FUNCTION_INFO_V1(pgraft_remove_node);
PG_FUNCTION_INFO_V1(pgraft_get_cluster_status_table);
PG_FUNCTION_INFO_V1(pgraft_get_nodes_table);
PG_FUNCTION_INFO_V1(pgraft_get_leader);
PG_FUNCTION_INFO_V1(pgraft_get_term);
PG_FUNCTION_INFO_V1(pgraft_is_leader);
PG_FUNCTION_INFO_V1(pgraft_get_worker_state);
PG_FUNCTION_INFO_V1(pgraft_get_queue_status);
PG_FUNCTION_INFO_V1(pgraft_get_version);
PG_FUNCTION_INFO_V1(pgraft_test);
PG_FUNCTION_INFO_V1(pgraft_set_debug);

/* Function info macros for log functions */
PG_FUNCTION_INFO_V1(pgraft_log_append);
PG_FUNCTION_INFO_V1(pgraft_log_commit);
PG_FUNCTION_INFO_V1(pgraft_log_apply);
PG_FUNCTION_INFO_V1(pgraft_log_get_entry_sql);
PG_FUNCTION_INFO_V1(pgraft_log_get_stats_table);
PG_FUNCTION_INFO_V1(pgraft_log_get_replication_status_table);

/* Background worker functions - removed as they are now handled automatically */
PG_FUNCTION_INFO_V1(pgraft_log_sync_with_leader_sql);
PG_FUNCTION_INFO_V1(pgraft_replicate_entry_func);



/*
 * Initialize pgraft node with GUC variables (etcd-style configuration)
 */
Datum
pgraft_init(PG_FUNCTION_ARGS)
{
	pgraft_go_config_t config;
	char	   *cluster_id;
	char	   *data_dir;
	int			result;
	
	/* Get configuration from etcd-style GUC variables */
	config.node_id = pgraft_node_id;
	config.port = pgraft_port;
	config.address = pgraft_address;
	config.election_timeout = pgraft_election_timeout;
	config.heartbeat_interval = pgraft_heartbeat_interval;
	config.snapshot_interval = pgraft_snapshot_interval;
	config.max_log_entries = pgraft_max_log_entries;
	config.batch_size = pgraft_batch_size;
	config.max_batch_delay = pgraft_max_batch_delay;
	
	/* Handle cluster_id (prefer new parameter, fall back to legacy) */
	cluster_id = pgraft_cluster_id;
	if (!cluster_id || strlen(cluster_id) == 0) {
		cluster_id = pgraft_cluster_name;
		if (!cluster_id || strlen(cluster_id) == 0) {
			elog(ERROR, "pgraft: pgraft.cluster_id must be set (similar to etcd initial-cluster-token)");
			PG_RETURN_BOOL(false);
		}
		elog(WARNING, "pgraft: Using deprecated pgraft.cluster_name, please use pgraft.cluster_id");
	}
	config.cluster_id = cluster_id;
	
	/* Handle data_dir (prefer new parameter, generate default if not set) */
	data_dir = pgraft_data_dir;
	if (!data_dir || strlen(data_dir) == 0) {
		/* Use PostgreSQL-style path in temp directory */
		data_dir = psprintf("/tmp/pgraft/node_%d", config.node_id);
		elog(INFO, "pgraft: pgraft.data_dir not set, using default: %s", data_dir);
	}
	config.data_dir = data_dir;
	
	/* Validate required configuration */
	if (!config.address || strlen(config.address) == 0) {
		elog(ERROR, "pgraft: pgraft.address must be set (similar to etcd listen-peer-urls)");
		PG_RETURN_BOOL(false);
	}
	
	if (config.port < 1024 || config.port > 65535) {
		elog(ERROR, "pgraft: pgraft.port must be between 1024 and 65535");
		PG_RETURN_BOOL(false);
	}
	
	/* Validate configuration (calls etcd-style validation) */
	pgraft_validate_configuration();
	
	elog(INFO, "pgraft: Initializing node %d (cluster: %s) at %s:%d", 
		 config.node_id, config.cluster_id, config.address, config.port);
	elog(INFO, "pgraft: etcd-style config: election_timeout=%dms, heartbeat_interval=%dms, data_dir=%s",
		 config.election_timeout, config.heartbeat_interval, config.data_dir);
	
	/* Load Go library if not already loaded */
	if (!pgraft_go_is_loaded()) {
		elog(INFO, "pgraft: Loading Go library...");
		if (pgraft_go_load_library() != 0) {
			elog(ERROR, "pgraft: Failed to load Go library");
			PG_RETURN_BOOL(false);
		}
	}
	
	/* Initialize with configuration */
	result = pgraft_go_init_with_config(&config);
	if (result != 0) {
		elog(ERROR, "pgraft: Failed to initialize raft node");
		PG_RETURN_BOOL(false);
	}
	
	elog(INFO, "pgraft: Node initialized successfully");
	
	/* Start the raft node */
	result = pgraft_go_start();
	if (result != 0) {
		elog(ERROR, "pgraft: Failed to start raft node");
		PG_RETURN_BOOL(false);
	}
	
	elog(INFO, "pgraft: Raft consensus started successfully");
	
	/* Start network server */
	result = pgraft_go_start_network_server(config.port);
	if (result != 0) {
		elog(WARNING, "pgraft: Failed to start network server (may already be running)");
	}
	
    PG_RETURN_BOOL(true);
}

/*
 * Initialize pgraft using GUC variables (parameterless version)
 */
Datum
pgraft_init_guc(PG_FUNCTION_ARGS)
{
	/* Same as pgraft_init but uses GUC variables */
	return pgraft_init(fcinfo);
}

/*
 * Add node to cluster (leader-only operation)
 * 
 * This operation MUST be performed on the leader node.
 * The configuration change will automatically propagate to all nodes
 * through the raft consensus log.
 */
Datum
pgraft_add_node(PG_FUNCTION_ARGS)
{
	int32_t		node_id;
	text	   *address_text;
	int32_t		port;
	char	   *address;
	pgraft_go_add_peer_func add_peer_func;
	int			result;
	int			leader_status;
	
	node_id = PG_GETARG_INT32(0);
	address_text = PG_GETARG_TEXT_PP(1);
	port = PG_GETARG_INT32(2);
	
	address = text_to_cstring(address_text);
	
	/* Validate parameters */
	if (node_id < 1 || node_id > 1000) {
		elog(ERROR, "pgraft: Invalid node_id %d, must be between 1 and 1000", node_id);
		PG_RETURN_BOOL(false);
	}
	
	if (!address || strlen(address) == 0) {
		elog(ERROR, "pgraft: Address cannot be empty");
		PG_RETURN_BOOL(false);
	}
	
	if (port < 1024 || port > 65535) {
		elog(ERROR, "pgraft: Invalid port %d, must be between 1024 and 65535", port);
		PG_RETURN_BOOL(false);
	}
	
	/* Check if Go library is loaded */
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: Go library not loaded. Initialize cluster first with pgraft_init()");
		PG_RETURN_BOOL(false);
	}
	
	/* Check if this node is the leader */
	leader_status = pgraft_go_is_leader();
	if (leader_status < 0) {
		elog(ERROR, "pgraft: Cannot add node - raft consensus not ready. "
			"Please wait a moment after initialization and try again.");
		PG_RETURN_BOOL(false);
	}
	if (leader_status == 0) {
		elog(ERROR, "pgraft: Cannot add node - this node is not the leader. "
			"Node addition must be performed on the leader. "
			"Current leader can be found with pgraft_get_leader()");
		PG_RETURN_BOOL(false);
	}
	
	elog(INFO, "pgraft: Adding node %d at %s:%d (leader-only operation)", 
		 node_id, address, port);
	elog(INFO, "pgraft: Configuration change will automatically propagate to all cluster members");
	
	/* Get add_peer function from Go library */
	add_peer_func = pgraft_go_get_add_peer_func();
	if (add_peer_func == NULL) {
		elog(ERROR, "pgraft: Failed to get add_peer function from Go library");
		PG_RETURN_BOOL(false);
	}
	
	/* Call Go library to add peer (will propose ConfChange to raft) */
	result = add_peer_func(node_id, address, port);
	if (result != 0) {
		elog(ERROR, "pgraft: Failed to add node %d - Go library returned error %d", 
			 node_id, result);
		PG_RETURN_BOOL(false);
	}
	
	elog(INFO, "pgraft: Node %d added successfully. Configuration change committed to raft log.", 
		 node_id);
	elog(INFO, "pgraft: All cluster members will automatically receive and apply this change");
    PG_RETURN_BOOL(true);
}

/*
 * Remove node from cluster
 */
Datum
pgraft_remove_node(PG_FUNCTION_ARGS)
{
    int32_t node_id = PG_GETARG_INT32(0);
    
    elog(INFO, "pgraft: Removing node %d", node_id);
    
    /* Remove from core system */
    if (pgraft_core_remove_node(node_id) != 0) {
        elog(ERROR, "pgraft: Failed to remove node from core system");
        PG_RETURN_BOOL(false);
    }
    
    /* Remove from Go library if loaded */
    if (pgraft_go_is_loaded()) {
        pgraft_go_remove_peer_func remove_peer_func = pgraft_go_get_remove_peer_func();
        if (remove_peer_func && remove_peer_func(node_id) != 0) {
            elog(ERROR, "pgraft: Failed to remove node from Go library");
            PG_RETURN_BOOL(false);
        }
    }
    
    elog(INFO, "pgraft: Node removed successfully");
    PG_RETURN_BOOL(true);
}


/*
 * Get cluster status as table with individual columns
 */
Datum
pgraft_get_cluster_status_table(PG_FUNCTION_ARGS)
{
    pgraft_cluster_t cluster;
    TupleDesc	tupdesc;
    Datum		values[8];
    bool		nulls[8];
    HeapTuple	tuple;
    
    if (pgraft_core_get_cluster_state(&cluster) != 0) {
        elog(ERROR, "pgraft: Failed to get cluster state");
        PG_RETURN_NULL();
    }
    
    /* Build tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "pgraft: Return type must be a row type");
    
    /* Prepare values */
    values[0] = Int32GetDatum(cluster.node_id);
    values[1] = Int64GetDatum(cluster.current_term);
    values[2] = Int64GetDatum(cluster.leader_id);
    values[3] = CStringGetTextDatum(cluster.state);
    values[4] = Int32GetDatum(cluster.num_nodes);
    values[5] = Int64GetDatum(cluster.messages_processed);
    values[6] = Int64GetDatum(cluster.heartbeats_sent);
    values[7] = Int64GetDatum(cluster.elections_triggered);
    
    /* Set nulls */
    memset(nulls, 0, sizeof(nulls));
    
    /* Build and return tuple */
    tuple = heap_form_tuple(tupdesc, values, nulls);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

/*
 * Get nodes information as table
 */
Datum
pgraft_get_nodes_table(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	pgraft_cluster_t cluster_state;
	pgraft_worker_state_t *worker_state;
	
	/* Check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not allowed in this context")));
	
	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	
	/* Build tuplestore to hold the result rows */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);
	
	tupstore = tuplestore_begin_heap(rsinfo->allowedModes & SFRM_Materialize_Random, false, 1024);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;
	
	MemoryContextSwitchTo(oldcontext);
	
	/* Get cluster state and worker state from shared memory */
	if (pgraft_core_get_cluster_state(&cluster_state))
	{
		worker_state = pgraft_worker_get_state();
		
		if (worker_state)
		{
			/* For now, return the current node information */
			/* TODO: This should be expanded to return all nodes in the cluster */
			Datum		values[4];
			bool		nulls[4];
			HeapTuple	tuple;
			
			values[0] = Int32GetDatum(worker_state->node_id);
			values[1] = CStringGetTextDatum(worker_state->address);
			values[2] = Int32GetDatum(worker_state->port);
			values[3] = BoolGetDatum(cluster_state.leader_id == (int64_t)worker_state->node_id);
			
			/* Set nulls */
			memset(nulls, 0, sizeof(nulls));
			
			/* Build and return tuple */
			tuple = heap_form_tuple(tupdesc, values, nulls);
			tuplestore_puttuple(tupstore, tuple);
			heap_freetuple(tuple);
		}
	}
	
	/* clean up and return the tuplestore */
	PG_RETURN_NULL();
}

/*
 * Get current leader
 */
Datum
pgraft_get_leader(PG_FUNCTION_ARGS)
{
	pgraft_cluster_t cluster_state;
	int64_t leader_id = -1;
	
	elog(INFO, "pgraft: pgraft_get_leader() function called");
	
	/* Get cluster state from shared memory */
	if (pgraft_core_get_cluster_state(&cluster_state) == 0)
	{
		leader_id = cluster_state.leader_id;
		elog(INFO, "pgraft: Got leader ID from shared memory: %lld", (long long)leader_id);
	}
	else
	{
		elog(WARNING, "pgraft: Failed to get cluster state from shared memory");
	}
	
	PG_RETURN_INT64(leader_id);
}

/*
 * Get current term
 */
Datum
pgraft_get_term(PG_FUNCTION_ARGS)
{
	pgraft_cluster_t cluster_state;
	int32_t term = 0;
	
	elog(INFO, "pgraft: pgraft_get_term() function called");
	
	/* Get cluster state from shared memory */
	if (pgraft_core_get_cluster_state(&cluster_state) == 0)
	{
		term = (int32_t)cluster_state.current_term;
		elog(INFO, "pgraft: Got term from shared memory: %d", term);
	}
	else
	{
		elog(WARNING, "pgraft: Failed to get cluster state from shared memory");
	}
	
	PG_RETURN_INT32(term);
}

/*
 * Check if current node is leader
 */
Datum
pgraft_is_leader(PG_FUNCTION_ARGS)
{
	pgraft_cluster_t cluster_state;
	pgraft_worker_state_t *worker_state;
	bool is_leader = false;
	
	elog(INFO, "pgraft: pgraft_is_leader() function called");
	
	/* Get cluster state from shared memory */
	if (pgraft_core_get_cluster_state(&cluster_state) == 0)
	{
		worker_state = pgraft_worker_get_state();
		
		if (worker_state)
		{
			/* Check if current node is the leader */
			is_leader = (cluster_state.leader_id == (int64_t)worker_state->node_id);
			elog(INFO, "pgraft: Got leader status from shared memory: %s (leader_id=%lld, node_id=%d)", 
				 is_leader ? "true" : "false", (long long)cluster_state.leader_id, worker_state->node_id);
		}
		else
		{
			elog(WARNING, "pgraft: Failed to get worker state from shared memory");
		}
	}
	else
	{
		elog(WARNING, "pgraft: Failed to get cluster state from shared memory");
	}
	
	PG_RETURN_BOOL(is_leader);
}

/*
 * Get background worker state as simple text
 */
Datum
pgraft_get_worker_state(PG_FUNCTION_ARGS)
{
	pgraft_worker_state_t *state;
	
	elog(INFO, "pgraft: pgraft_get_worker_state() function called");
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		elog(WARNING, "pgraft: Failed to get worker state");
		PG_RETURN_TEXT_P(cstring_to_text("ERROR"));
	}
	
	switch (state->status) {
		case WORKER_STATUS_STOPPED:
			PG_RETURN_TEXT_P(cstring_to_text("STOPPED"));
		case WORKER_STATUS_INITIALIZING:
			PG_RETURN_TEXT_P(cstring_to_text("INITIALIZING"));
		case WORKER_STATUS_RUNNING:
			PG_RETURN_TEXT_P(cstring_to_text("RUNNING"));
		case WORKER_STATUS_STOPPING:
			PG_RETURN_TEXT_P(cstring_to_text("STOPPING"));
		default:
			PG_RETURN_TEXT_P(cstring_to_text("UNKNOWN"));
	}
}



/*
 * Get pgraft version
 */
Datum
pgraft_get_version(PG_FUNCTION_ARGS)
{
    if (pgraft_go_is_loaded()) {
        pgraft_go_version_func version_func = pgraft_go_get_version_func();
        if (version_func) {
            char *version = version_func();
            if (version) {
                text *result = cstring_to_text(version);
                pgraft_go_free_string_func free_func = pgraft_go_get_free_string_func();
                if (free_func) {
                    free_func(version);
                }
                PG_RETURN_TEXT_P(result);
            }
        }
    }
    
    PG_RETURN_TEXT_P(cstring_to_text("pgraft-1.0.0"));
}

/*
 * Test pgraft functionality
 */
Datum
pgraft_test(PG_FUNCTION_ARGS)
{
    if (pgraft_go_is_loaded()) {
        pgraft_go_test_func test_func = pgraft_go_get_test_func();
        if (test_func && test_func() == 0) {
            PG_RETURN_BOOL(true);
        }
    }
    
    PG_RETURN_BOOL(false);
}

/*
 * Set debug mode
 */
Datum
pgraft_set_debug(PG_FUNCTION_ARGS)
{
    bool debug_enabled = PG_GETARG_BOOL(0);
    
    if (pgraft_go_is_loaded()) {
        pgraft_go_set_debug_func set_debug_func = pgraft_go_get_set_debug_func();
        if (set_debug_func) {
            set_debug_func(debug_enabled ? 1 : 0);
        }
    }
    
    elog(INFO, "pgraft: Debug mode %s", debug_enabled ? "enabled" : "disabled");
    PG_RETURN_BOOL(true);
}

/*
 * Log replication functions
 */

/*
 * Append log entry
 */
Datum
pgraft_log_append(PG_FUNCTION_ARGS)
{
    int64_t term = PG_GETARG_INT64(0);
    text *data_text = PG_GETARG_TEXT_PP(1);
    char *data = text_to_cstring(data_text);
    
    elog(INFO, "pgraft: Queuing LOG_APPEND command for term %lld", (long long)term);
    
    /* Queue LOG_APPEND command for worker to process */
    if (!pgraft_queue_log_command(COMMAND_LOG_APPEND, data, (int)term)) {
        elog(ERROR, "pgraft: Failed to queue LOG_APPEND command");
        PG_RETURN_BOOL(false);
    }
    
    elog(INFO, "pgraft: LOG_APPEND command queued successfully - background worker will process it");
    PG_RETURN_BOOL(true);
}

/*
 * Commit log entry
 */
Datum
pgraft_log_commit(PG_FUNCTION_ARGS)
{
    int64_t index = PG_GETARG_INT64(0);
    
    elog(INFO, "pgraft: Queuing LOG_COMMIT command for index %lld", (long long)index);
    
    /* Queue LOG_COMMIT command for worker to process */
    if (!pgraft_queue_log_command(COMMAND_LOG_COMMIT, NULL, (int)index)) {
        elog(ERROR, "pgraft: Failed to queue LOG_COMMIT command");
        PG_RETURN_BOOL(false);
    }
    
    elog(INFO, "pgraft: LOG_COMMIT command queued successfully - background worker will process it");
    PG_RETURN_BOOL(true);
}

/*
 * Apply log entry
 */
Datum
pgraft_log_apply(PG_FUNCTION_ARGS)
{
    int64_t index = PG_GETARG_INT64(0);
    
    elog(INFO, "pgraft: Queuing LOG_APPLY command for index %lld", (long long)index);
    
    /* Queue LOG_APPLY command for worker to process */
    if (!pgraft_queue_log_command(COMMAND_LOG_APPLY, NULL, (int)index)) {
        elog(ERROR, "pgraft: Failed to queue LOG_APPLY command");
        PG_RETURN_BOOL(false);
    }
    
    elog(INFO, "pgraft: LOG_APPLY command queued successfully - background worker will process it");
    PG_RETURN_BOOL(true);
}

/*
 * Get log entry
 */
Datum
pgraft_log_get_entry_sql(PG_FUNCTION_ARGS)
{
    int64_t index = PG_GETARG_INT64(0);
    pgraft_log_entry_t entry;
    StringInfoData result;
    
    if (pgraft_log_get_entry(index, &entry) != 0) {
        elog(ERROR, "pgraft: Failed to get log entry %lld", index);
        PG_RETURN_NULL();
    }
    initStringInfo(&result);
    
    appendStringInfo(&result, "Index: %lld, Term: %lld, Timestamp: %lld, Data: %s, Committed: %s, Applied: %s",
                    entry.index, entry.term, entry.timestamp, entry.data,
                    entry.committed ? "yes" : "no", entry.applied ? "yes" : "no");
    
    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}


/*
 * Get log statistics as table with individual columns
 */
Datum
pgraft_log_get_stats_table(PG_FUNCTION_ARGS)
{
    pgraft_log_state_t stats;
    TupleDesc	tupdesc;
    Datum		values[8];
    bool		nulls[8];
    HeapTuple	tuple;
    
    if (pgraft_log_get_statistics(&stats) != 0) {
        elog(ERROR, "pgraft: Failed to get log statistics");
        PG_RETURN_NULL();
    }
    
    /* Build tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "pgraft: Return type must be a row type");
    
    /* Prepare values */
    values[0] = Int64GetDatum(stats.log_size);
    values[1] = Int64GetDatum(stats.last_index);
    values[2] = Int64GetDatum(stats.commit_index);
    values[3] = Int64GetDatum(stats.last_applied);
    values[4] = Int64GetDatum(stats.entries_replicated);
    values[5] = Int64GetDatum(stats.entries_committed);
    values[6] = Int64GetDatum(stats.entries_applied);
    values[7] = Int64GetDatum(stats.replication_errors);
    
    /* Set nulls */
    memset(nulls, 0, sizeof(nulls));
    
    /* Build and return tuple */
    tuple = heap_form_tuple(tupdesc, values, nulls);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}


/*
 * Get replication status as table with individual columns
 */
Datum
pgraft_log_get_replication_status_table(PG_FUNCTION_ARGS)
{
    pgraft_log_state_t stats;
    TupleDesc	tupdesc;
    Datum		values[8];
    bool		nulls[8];
    HeapTuple	tuple;
    
    if (pgraft_log_get_statistics(&stats) != 0) {
        elog(ERROR, "pgraft: Failed to get replication status");
        PG_RETURN_NULL();
    }
    
    /* Build tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "pgraft: Return type must be a row type");
    
    /* Prepare values */
    values[0] = Int64GetDatum(stats.log_size);
    values[1] = Int64GetDatum(stats.last_index);
    values[2] = Int64GetDatum(stats.commit_index);
    values[3] = Int64GetDatum(stats.last_applied);
    values[4] = Int64GetDatum(stats.entries_replicated);
    values[5] = Int64GetDatum(stats.entries_committed);
    values[6] = Int64GetDatum(stats.entries_applied);
    values[7] = Int64GetDatum(stats.replication_errors);
    
    /* Set nulls */
    memset(nulls, 0, sizeof(nulls));
    
    /* Build and return tuple */
    tuple = heap_form_tuple(tupdesc, values, nulls);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}


/*
 * Get command queue status
 */
Datum
pgraft_get_queue_status(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_mcxt;
	MemoryContext oldcontext;
	pgraft_worker_state_t *state;

	/* Check to ensure we were called as a set-returning function */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));

	/* Switch into long-lived context to construct returned data structures */
	per_query_mcxt = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_mcxt);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	tupstore = tuplestore_begin_heap(true, false, 1024);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	/* Get worker state */
	state = pgraft_worker_get_state();
	if (state != NULL && state->status_count > 0)
	{
		int			position = 0;
		int			i;

		/* Iterate through status commands */
		for (i = 0; i < state->status_count; i++)
		{
			int index = (state->status_head + i) % MAX_COMMANDS;
			pgraft_command_t *cmd = &state->status_commands[index];
			Datum		values[6];
			bool		nulls[6];

			memset(nulls, 0, sizeof(nulls));

			values[0] = Int32GetDatum(position++);
			values[1] = Int32GetDatum((int32) cmd->type);
			values[2] = Int32GetDatum(cmd->node_id);
			values[3] = CStringGetTextDatum(cmd->address);
			values[4] = Int32GetDatum(cmd->port);
			values[5] = CStringGetTextDatum(cmd->log_data);

			tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		}
	}

	/* Clean up and return the tuplestore */
	/* tuplestore_donestoring not needed when using SFRM_Materialize */

	return (Datum) 0;
}


/*
 * Sync with leader
 */
Datum
pgraft_log_sync_with_leader_sql(PG_FUNCTION_ARGS)
{
    if (pgraft_log_sync_with_leader() != 0) {
        elog(ERROR, "pgraft: Failed to sync with leader");
        PG_RETURN_BOOL(false);
    }
    
    elog(INFO, "pgraft: Synced with leader successfully");
    PG_RETURN_BOOL(true);
}

/*
 * Replicate entry via Raft leader
 */
Datum
pgraft_replicate_entry_func(PG_FUNCTION_ARGS)
{
    text *data_text = PG_GETARG_TEXT_PP(0);
    char *data = text_to_cstring(data_text);
    int data_len = strlen(data);
    
    elog(INFO, "pgraft: Replicating entry via Raft: %s", data);
    
    if (pgraft_go_is_loaded()) {
        // Use the Go function to replicate the log entry
        pgraft_go_replicate_log_entry_func replicate_func = pgraft_go_get_replicate_log_entry_func();
        if (replicate_func) {
            int result = replicate_func(data, data_len);
            if (result == 1) {
                elog(INFO, "pgraft: Log entry replicated successfully");
                PG_RETURN_BOOL(true);
            } else {
                elog(WARNING, "pgraft: Failed to replicate log entry");
                PG_RETURN_BOOL(false);
            }
        } else {
            elog(ERROR, "pgraft: Replicate function not available");
            PG_RETURN_BOOL(false);
        }
    } else {
        elog(ERROR, "pgraft: Go library not loaded");
        PG_RETURN_BOOL(false);
    }
}

/* Network worker functions removed - handled automatically by background worker */
