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
PG_FUNCTION_INFO_V1(pgraft_get_leader);
PG_FUNCTION_INFO_V1(pgraft_get_term);
PG_FUNCTION_INFO_V1(pgraft_is_leader);
PG_FUNCTION_INFO_V1(pgraft_get_worker_state);
PG_FUNCTION_INFO_V1(pgraft_get_queue_status);
PG_FUNCTION_INFO_V1(pgraft_get_nodes_table);
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



/*
 * Initialize pgraft node with GUC variables
 */
Datum
pgraft_init(PG_FUNCTION_ARGS)
{
	int32_t		node_id;
	int32_t		port;
	char	   *address;
	char	   *cluster_id;
	
	/* Get configuration from GUC variables */
	node_id = pgraft_node_id;
	port = pgraft_port;
	address = pgraft_address;
	cluster_id = pgraft_cluster_name;
	
	elog(INFO, "pgraft: Queuing INIT command for node %d at %s:%d (cluster: %s)", 
		 node_id, address, port, cluster_id);
	
	/* Queue INIT command for worker to process */
	if (!pgraft_queue_command(COMMAND_INIT, node_id, address, port, cluster_id)) {
		elog(ERROR, "pgraft: Failed to queue INIT command");
		PG_RETURN_BOOL(false);
	}
	
	elog(INFO, "pgraft: INIT command queued successfully - background worker will process it");
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
 * Add node to cluster
 */
Datum
pgraft_add_node(PG_FUNCTION_ARGS)
{
	int32_t		node_id;
	text	   *address_text;
	int32_t		port;
	char	   *address;
	
	node_id = PG_GETARG_INT32(0);
	address_text = PG_GETARG_TEXT_PP(1);
	port = PG_GETARG_INT32(2);
	
	address = text_to_cstring(address_text);
	
	elog(INFO, "pgraft: Queuing ADD_NODE command for node %d at %s:%d", node_id, address, port);
	
	/* Queue ADD_NODE command for worker to process */
	if (!pgraft_queue_command(COMMAND_ADD_NODE, node_id, address, port, NULL)) {
		elog(ERROR, "pgraft: Failed to queue ADD_NODE command");
		PG_RETURN_BOOL(false);
	}
	
	elog(INFO, "pgraft: ADD_NODE command queued successfully - background worker will process it");
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
 * Get current leader
 */
Datum
pgraft_get_leader(PG_FUNCTION_ARGS)
{
	pgraft_go_get_leader_func get_leader_func;
	int64_t leader_id = -1;
	
	elog(INFO, "pgraft: pgraft_get_leader() function called");
	
	/* Try to get leader from Go library if available */
	if (pgraft_go_is_loaded())
	{
		get_leader_func = pgraft_go_get_get_leader_func();
		if (get_leader_func)
		{
			leader_id = get_leader_func();
			elog(INFO, "pgraft: Got leader ID from Go library: %lld", (long long)leader_id);
		}
		else
		{
			elog(WARNING, "pgraft: Failed to get Go leader function");
		}
	}
	else
	{
		elog(WARNING, "pgraft: Go library not loaded");
	}
	
	PG_RETURN_INT64(leader_id);
}

/*
 * Get current term
 */
Datum
pgraft_get_term(PG_FUNCTION_ARGS)
{
	pgraft_go_get_term_func get_term_func;
	int32_t term = 0;
	
	elog(INFO, "pgraft: pgraft_get_term() function called");
	
	/* Try to get term from Go library if available */
	if (pgraft_go_is_loaded())
	{
		get_term_func = pgraft_go_get_get_term_func();
		if (get_term_func)
		{
			term = get_term_func();
			elog(INFO, "pgraft: Got term from Go library: %d", term);
		}
		else
		{
			elog(WARNING, "pgraft: Failed to get Go term function");
		}
	}
	else
	{
		elog(WARNING, "pgraft: Go library not loaded");
	}
	
	PG_RETURN_INT32(term);
}

/*
 * Check if current node is leader
 */
Datum
pgraft_is_leader(PG_FUNCTION_ARGS)
{
	pgraft_go_is_leader_func is_leader_func;
	bool is_leader = false;
	
	elog(INFO, "pgraft: pgraft_is_leader() function called");
	
	/* Try to get leader status from Go library if available */
	if (pgraft_go_is_loaded())
	{
		is_leader_func = pgraft_go_get_is_leader_func();
		if (is_leader_func)
		{
			is_leader = (is_leader_func() != 0);
			elog(INFO, "pgraft: Got leader status from Go library: %s", is_leader ? "true" : "false");
		}
		else
		{
			elog(WARNING, "pgraft: Failed to get Go is_leader function");
		}
	}
	else
	{
		elog(WARNING, "pgraft: Go library not loaded");
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
 * Get cluster nodes as table with individual columns
 */
Datum
pgraft_get_nodes_table(PG_FUNCTION_ARGS)
{
    pgraft_cluster_t cluster;
    ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
    TupleDesc	tupdesc;
    Tuplestorestate *tupstore;
    MemoryContext per_query_ctx;
    MemoryContext oldcontext;
    
    if (pgraft_core_get_cluster_state(&cluster) != 0) {
        elog(ERROR, "pgraft: Failed to get cluster state");
        PG_RETURN_NULL();
    }
    
    /* Check if we're called in table context */
    if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
        elog(ERROR, "pgraft: pgraft_get_nodes_table must be called in table context");
    
    /* Build tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "pgraft: Return type must be a row type");
    
    /* Initialize tuplestore */
    per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
    oldcontext = MemoryContextSwitchTo(per_query_ctx);
    
    tupstore = tuplestore_begin_heap(true, false, 1024);
    rsinfo->returnMode = SFRM_Materialize;
    rsinfo->setResult = tupstore;
    rsinfo->setDesc = tupdesc;
    
    MemoryContextSwitchTo(oldcontext);
    
    /* Return one row for each node */
    for (int i = 0; i < cluster.num_nodes; i++) {
        Datum		values[4];
        bool		nulls[4];
        HeapTuple	tuple;
        
        values[0] = Int32GetDatum(cluster.nodes[i].id);
        values[1] = CStringGetTextDatum(cluster.nodes[i].address);
        values[2] = Int32GetDatum(cluster.nodes[i].port);
        values[3] = BoolGetDatum(cluster.nodes[i].is_leader);
        
        memset(nulls, 0, sizeof(nulls));
        
        tuple = heap_form_tuple(tupdesc, values, nulls);
        tuplestore_puttuple(tupstore, tuple);
    }
    
    PG_RETURN_NULL();
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


/* Network worker functions removed - handled automatically by background worker */
