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

/* New function to get nodes from Go Raft layer (works on replicas) */
PG_FUNCTION_INFO_V1(pgraft_get_nodes_from_raft);



/*
 * Internal function to initialize raft from GUCs (callable from worker or SQL)
 */
int
pgraft_init_from_gucs(void)
{
	pgraft_go_config_t config;
	pgraft_cluster_member_t *cluster_members = NULL;
	pgraft_go_cluster_member_t *go_members = NULL;
	int			cluster_member_count = 0;
	char	   *cluster_id;
	int			result;
	int			i;
	
	elog(LOG, "pgraft_init_from_gucs: initializing raft with etcd-style configuration");
	
	/* Check if already initialized (background worker may have already done this) */
	if (pgraft_go_is_initialized())
	{
		elog(LOG, "pgraft_init_from_gucs: raft already initialized, skipping");
		return 0;
	}
	
	/* Initialize config structure */
	memset(&config, 0, sizeof(pgraft_go_config_t));
	
	/* Get basic configuration from etcd-compatible GUCs */
	config.name = name;
	config.initial_cluster_state = (strcmp(initial_cluster_state, "new") == 0) ? 1 : 0;
	
	/* Set node_id to 1 as default - the Go layer will assign proper Raft IDs based on initial_cluster order */
	config.node_id = 1;
	
	/* Parse initial_cluster into structured array */
	elog(INFO, "pgraft_init: parsing initial_cluster='%s', name='%s'", 
		 initial_cluster ? initial_cluster : "(null)", 
		 name ? name : "(null)");
	
	pgraft_parse_initial_cluster(initial_cluster, &cluster_members, &cluster_member_count);
	
	if (cluster_member_count == 0)
	{
		elog(ERROR, "pgraft: no cluster members found in initial_cluster");
		return -1;
	}
	
	/* Convert to Go cluster member structure with parsed host/port */
	go_members = (pgraft_go_cluster_member_t *) palloc(cluster_member_count * sizeof(pgraft_go_cluster_member_t));
	
	for (i = 0; i < cluster_member_count; i++)
	{
		go_members[i].name = cluster_members[i].name;
		
		/* Parse peer URL into host and port */
		if (!pgraft_parse_url(cluster_members[i].peer_url, &go_members[i].peer_host, &go_members[i].peer_port))
		{
			elog(ERROR, "pgraft: failed to parse peer URL for member '%s': %s", 
				 cluster_members[i].name, cluster_members[i].peer_url);
			pfree(go_members);
			pfree(cluster_members);
			return -1;
		}
		
		elog(INFO, "pgraft_init: cluster member %d: %s -> %s:%d", 
			 i + 1, go_members[i].name, go_members[i].peer_host, go_members[i].peer_port);
	}
	
	config.cluster_members = go_members;
	config.cluster_member_count = cluster_member_count;
	
	/* Parse listen_peer_urls into host/port */
	if (!pgraft_parse_url(listen_peer_urls, &config.listen_peer_host, &config.listen_peer_port))
	{
		elog(ERROR, "pgraft: failed to parse listen_peer_urls: %s", listen_peer_urls);
		return -1;
	}
	
	/* Parse listen_client_urls into host/port */
	if (listen_client_urls && strlen(listen_client_urls) > 0)
	{
		if (!pgraft_parse_url(listen_client_urls, &config.listen_client_host, &config.listen_client_port))
		{
			elog(WARNING, "pgraft: failed to parse listen_client_urls: %s", listen_client_urls);
		}
	}
	
	/* Parse advertise_client_urls into host/port */
	if (advertise_client_urls && strlen(advertise_client_urls) > 0)
	{
		if (!pgraft_parse_url(advertise_client_urls, &config.advertise_client_host, &config.advertise_client_port))
		{
			elog(WARNING, "pgraft: failed to parse advertise_client_urls: %s", advertise_client_urls);
		}
	}
	
	/* Parse initial_advertise_peer_urls into host/port */
	if (initial_advertise_peer_urls && strlen(initial_advertise_peer_urls) > 0)
	{
		if (!pgraft_parse_url(initial_advertise_peer_urls, &config.initial_advertise_peer_host, &config.initial_advertise_peer_port))
		{
			elog(WARNING, "pgraft: failed to parse initial_advertise_peer_urls: %s", initial_advertise_peer_urls);
		}
	}
	
	config.election_timeout = election_timeout;
	config.heartbeat_interval = heartbeat_interval;
	config.snapshot_interval = snapshot_count;
	config.quota_backend_bytes = quota_backend_bytes;
	config.max_request_bytes = max_request_bytes;
	config.max_snapshots = max_snapshots;
	config.max_wals = max_wals;
	config.auto_compaction_retention = atoi(auto_compaction_retention);
	config.auto_compaction_mode = (strcmp(auto_compaction_mode, "periodic") == 0) ? 1 : 0;
	config.compaction_batch_limit = compaction_batch_limit;
	
	config.log_level = log_level;
	config.log_outputs = log_outputs;
	config.log_package_levels = log_package_levels;
	
	config.client_cert_auth = client_cert_auth ? 1 : 0;
	config.trusted_ca_file = trusted_ca_file;
	config.cert_file = cert_file;
	config.key_file = key_file;
	config.client_cert_file = client_cert_file;
	config.client_key_file = client_key_file;
	config.peer_trusted_ca_file = peer_trusted_ca_file;
	config.peer_cert_file = peer_cert_file;
	config.peer_key_file = peer_key_file;
	config.peer_client_cert_auth = peer_client_cert_auth ? 1 : 0;
	config.peer_cert_allowed_cn = peer_cert_allowed_cn;
	config.peer_cert_allowed_hostname = peer_cert_allowed_hostname ? 1 : 0;
	config.cipher_suites = cipher_suites;
	config.cors = cors;
	config.host_whitelist = host_whitelist;
	config.listen_metrics_urls = listen_metrics_urls;
	config.metrics = metrics;
	
	
	config.max_log_entries = pgraft_max_log_entries;
	config.batch_size = pgraft_batch_size;
	config.max_batch_delay = pgraft_max_batch_delay;
	
	/* Use etcd-compatible GUC variables */
	cluster_id = initial_cluster_token;
	if (!cluster_id || strlen(cluster_id) == 0) {
		elog(ERROR, "pgraft: initial_cluster_token must be set");
		return -1;
	}
	config.cluster_id = cluster_id;
	
	/* Use etcd-compatible data directory */
	if (!data_dir || strlen(data_dir) == 0)
	{
		/* Use node name for default data directory (etcd-style) */
		config.data_dir = psprintf("/tmp/pgraft/%s", name ? name : "node");
	}
	else
	{
		config.data_dir = data_dir;
	}
	
	/* Validate that we have parsed peer URLs */
	if (!config.listen_peer_host || strlen(config.listen_peer_host) == 0)
	{
		elog(ERROR, "pgraft: listen_peer_urls must be set and valid");
		return -1;
	}
	
	if (config.listen_peer_port < 1024 || config.listen_peer_port > 65535)
	{
		elog(ERROR, "pgraft: listen_peer_urls port must be between 1024 and 65535 (got %d)", config.listen_peer_port);
		return -1;
	}
	
	/* For backward compatibility, also set address and port from parsed peer URL */
	config.address = config.listen_peer_host;
	config.port = config.listen_peer_port;
	
	pgraft_validate_configuration();
	
	if (!pgraft_go_is_loaded())
	{
		if (pgraft_go_load_library() != 0)
		{
			elog(ERROR, "pgraft: failed to load Go library");
			return -1;
		}
	}
	
	result = pgraft_go_init_with_config(&config);
	if (result != 0)
	{
		elog(ERROR, "pgraft: failed to initialize raft node");
		return -1;
	}
	
	result = pgraft_go_start();
	if (result != 0)
	{
		elog(ERROR, "pgraft: failed to start raft node");
		return -1;
	}
	
	result = pgraft_go_start_network_server(config.port);
	if (result != 0)
	{
		elog(WARNING, "pgraft: failed to start network server");
	}
	
    return 0;
}

/*
 * SQL wrapper for pgraft_init() - just calls the internal init function
 */
Datum
pgraft_init(PG_FUNCTION_ARGS)
{
	int result;
	
	elog(LOG, "pgraft_init: SQL function called");
	result = pgraft_init_from_gucs();
	
	if (result != 0)
	{
		elog(WARNING, "pgraft_init: initialization failed");
		PG_RETURN_BOOL(false);
	}
	
	elog(LOG, "pgraft_init: initialization successful");
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
		elog(ERROR, "pgraft: invalid node_id %d, must be between 1 and 1000", node_id);
		return -1;
	}
	
	if (!address || strlen(address) == 0) {
		elog(ERROR, "pgraft: address cannot be empty");
		return -1;
	}
	
	if (port < 1024 || port > 65535) {
		elog(ERROR, "pgraft: invalid port %d, must be between 1024 and 65535", port);
		return -1;
	}
	
	/* Check if Go library is loaded */
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: go library not loaded. Initialize cluster first with pgraft_init()");
		return -1;
	}
	
	leader_status = pgraft_go_is_leader();
	if (leader_status < 0)
	{
		elog(ERROR, "pgraft: cannot add node - raft consensus not ready");
		return -1;
	}
	if (leader_status == 0)
	{
		elog(ERROR, "pgraft: cannot add node - this node is not the leader");
		return -1;
	}
	
	add_peer_func = pgraft_go_get_add_peer_func();
	if (add_peer_func == NULL)
	{
		elog(ERROR, "pgraft: failed to get add_peer function");
		return -1;
	}
	
	result = add_peer_func(node_id, address, port);
	if (result != 0)
	{
		elog(ERROR, "pgraft: failed to add node %d", node_id);
		return -1;
	}
    return 0;
}

/*
 * Remove node from cluster
 */
Datum
pgraft_remove_node(PG_FUNCTION_ARGS)
{
	int32_t node_id;
	pgraft_go_remove_peer_func remove_peer_func;
	
	node_id = PG_GETARG_INT32(0);
	
	if (pgraft_core_remove_node(node_id) != 0)
	{
		elog(ERROR, "pgraft: failed to remove node from core system");
        return -1;
    }
    
	if (pgraft_go_is_loaded())
	{
		remove_peer_func = pgraft_go_get_remove_peer_func();
		if (remove_peer_func && remove_peer_func(node_id) != 0)
		{
			elog(ERROR, "pgraft: failed to remove node from Go library");
            return -1;
        }
    }
    
    return 0;
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
        elog(ERROR, "pgraft: failed to get cluster state");
        PG_RETURN_NULL();
    }
    
    /* Build tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "pgraft: return type must be a row type");
    
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
	
	if (pgraft_core_get_cluster_state(&cluster_state) != 0)
	{
		PG_RETURN_NULL();
	}
	
	/* Return all nodes from shared memory */
	for (int i = 0; i < cluster_state.num_nodes && i < 16; i++)
	{
		pgraft_node_t *node = &cluster_state.nodes[i];
		Datum		values[4];
		bool		nulls[4];
		HeapTuple	tuple;
		
		/* Extract host and port from address "host:port" */
		char *address_copy = pstrdup(node->address);
		char *colon = strchr(address_copy, ':');
		int port = 0;
		if (colon)
		{
			*colon = '\0';
			port = atoi(colon + 1);
		}
		
		values[0] = Int32GetDatum(node->id);
		values[1] = CStringGetTextDatum(address_copy);
		values[2] = Int32GetDatum(port);
		values[3] = BoolGetDatum(cluster_state.leader_id == (int64_t)node->id);
		
		memset(nulls, 0, sizeof(nulls));
		
		tuple = heap_form_tuple(tupdesc, values, nulls);
		tuplestore_puttuple(tupstore, tuple);
		heap_freetuple(tuple);
		
		pfree(address_copy);
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
	int64_t leader_id;
	
	leader_id = -1;
	
	if (pgraft_core_get_cluster_state(&cluster_state) == 0)
	{
		leader_id = cluster_state.leader_id;
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
	int32_t term;
	
	term = 0;
	
	if (pgraft_core_get_cluster_state(&cluster_state) == 0)
	{
		term = (int32_t)cluster_state.current_term;
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
	bool is_leader;
	
	is_leader = false;
	
	if (pgraft_core_get_cluster_state(&cluster_state) == 0)
	{
		worker_state = pgraft_worker_get_state();
		if (worker_state)
		{
			is_leader = (cluster_state.leader_id == (int64_t)worker_state->node_id);
		}
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
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		elog(WARNING, "pgraft: failed to get worker state");
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
            return 0;
        }
    }
    
    return -1;
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
    
    return 0;
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
    
    
    /* Queue LOG_APPEND command for worker to process */
    if (!pgraft_queue_log_command(COMMAND_LOG_APPEND, data, (int)term)) {
        elog(ERROR, "pgraft: failed to queue LOG_APPEND command");
        return -1;
    }
    
    return 0;
}

/*
 * Commit log entry
 */
Datum
pgraft_log_commit(PG_FUNCTION_ARGS)
{
    int64_t index = PG_GETARG_INT64(0);
    
    
    /* Queue LOG_COMMIT command for worker to process */
    if (!pgraft_queue_log_command(COMMAND_LOG_COMMIT, NULL, (int)index)) {
        elog(ERROR, "pgraft: failed to queue LOG_COMMIT command");
        return -1;
    }
    
    return 0;
}

/*
 * Apply log entry
 */
Datum
pgraft_log_apply(PG_FUNCTION_ARGS)
{
    int64_t index = PG_GETARG_INT64(0);
    
    
    /* Queue LOG_APPLY command for worker to process */
    if (!pgraft_queue_log_command(COMMAND_LOG_APPLY, NULL, (int)index)) {
        elog(ERROR, "pgraft: failed to queue LOG_APPLY command");
        return -1;
    }
    
    return 0;
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
        elog(ERROR, "pgraft: failed to get log entry %lld", (long long)index);
        PG_RETURN_NULL();
    }
    initStringInfo(&result);
    
    appendStringInfo(&result, "Index: %lld, Term: %lld, Timestamp: %lld, Data: %s, Committed: %s, Applied: %s",
                    (long long)entry.index, (long long)entry.term, (long long)entry.timestamp, entry.data,
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
        elog(ERROR, "pgraft: failed to get log statistics");
        PG_RETURN_NULL();
    }
    
    /* Build tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "pgraft: return type must be a row type");
    
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
        elog(ERROR, "pgraft: failed to get replication status");
        PG_RETURN_NULL();
    }
    
    /* Build tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "pgraft: return type must be a row type");
    
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
        elog(ERROR, "pgraft: failed to sync with leader");
        return -1;
    }
    
    return 0;
}

/*
 * Replicate entry via Raft leader
 */
Datum
pgraft_replicate_entry_func(PG_FUNCTION_ARGS)
{
	text *data_text;
	char *data;
	int data_len;
	pgraft_go_replicate_log_entry_func replicate_func;
	int result;
	
	data_text = PG_GETARG_TEXT_PP(0);
	data = text_to_cstring(data_text);
	data_len = strlen(data);
	
	
	if (pgraft_go_is_loaded())
	{
		replicate_func = pgraft_go_get_replicate_log_entry_func();
		if (replicate_func)
		{
			result = replicate_func(data, data_len);
			if (result == 1)
			{
				return 0;
			}
			else
			{
				elog(WARNING, "pgraft: failed to replicate log entry");
				return -1;
			}
		}
		else
		{
			elog(ERROR, "pgraft: replicate function not available");
			return -1;
		}
	}
	else
	{
		elog(ERROR, "pgraft: go library not loaded");
		return -1;
	}
}

/* Network worker functions removed - handled automatically by background worker */

/*
 * Get nodes directly from Go Raft layer
 * This function works on replicas because it queries the Raft cluster directly
 * instead of reading from PostgreSQL shared memory
 */
Datum
pgraft_get_nodes_from_raft(PG_FUNCTION_ARGS)
{
	char *result;
	text *result_text;
	pgraft_go_get_nodes_func get_nodes_func;
	
	/* Get the function pointer for pgraft_go_get_nodes */
	get_nodes_func = pgraft_go_get_get_nodes_func();
	if (get_nodes_func == NULL)
	{
		elog(DEBUG1, "pgraft_go_get_nodes function not available");
		/* Return empty JSON array */
		PG_RETURN_TEXT_P(cstring_to_text("[]"));
	}
	
	/* Call Go function to get nodes from Raft cluster state */
	result = get_nodes_func();
	
	if (result == NULL)
	{
		elog(DEBUG1, "pgraft_go_get_nodes returned NULL");
		/* Return empty JSON array */
		PG_RETURN_TEXT_P(cstring_to_text("[]"));
	}
	
	/* Convert C string to PostgreSQL text */
	result_text = cstring_to_text(result);
	
	PG_RETURN_TEXT_P(result_text);
}
