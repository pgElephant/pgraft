/*-------------------------------------------------------------------------
 *
 * pgraft.c
 *      Main pgraft extension file with clean modular architecture
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "storage/shmem.h"
#include "utils/elog.h"
#include "postmaster/bgworker.h"
#include "utils/ps_status.h"


#include "../include/pgraft_core.h"
#include "../include/pgraft_go.h"
#include "../include/pgraft_state.h"
#include "../include/pgraft_log.h"
#include "../include/pgraft_guc.h"
/* Forward declarations */
static int pgraft_init_system(int node_id, const char *address, int port);
static int pgraft_add_node_system(int node_id, const char *address, int port);
static int pgraft_remove_node_system(int node_id);
static int pgraft_log_append_system(const char *log_data, int log_index);
static int pgraft_log_commit_system(int log_index);
static int pgraft_log_apply_system(int log_index);


PG_MODULE_MAGIC;

/* Extension version */
#define PGRAFT_VERSION "1.0.0"

/* Shared memory request hook */
static shmem_request_hook_type prev_shmem_request_hook = NULL;

/*
 * Shared memory request hook
 */
static void
pgraft_shmem_request_hook(void)
{
	/* Call previous hook first */
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();
	
	/* Request shared memory for core system */
	RequestAddinShmemSpace(sizeof(pgraft_cluster_t));
	
	/* Request shared memory for Go state persistence */
	RequestAddinShmemSpace(sizeof(pgraft_go_state_t));
	
	/* Request shared memory for log replication */
	RequestAddinShmemSpace(sizeof(pgraft_log_state_t));
	
	/* Request shared memory for background worker state */
	RequestAddinShmemSpace(sizeof(pgraft_worker_state_t));
	
	elog(LOG, "pgraft: Shared memory request hook completed");
}


/*
 * Extension initialization
 */
void
_PG_init(void)
{
	elog(INFO, "pgraft: Initializing extension version %s", PGRAFT_VERSION);

	/* Install shared memory request hook */
	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = pgraft_shmem_request_hook;
	elog(LOG, "pgraft: Shared memory request hook installed");

	/* Register GUC variables */
	pgraft_register_guc_variables();
	elog(LOG, "pgraft: GUC variables registered");

	/* Register background worker */
	pgraft_register_worker();
	elog(LOG, "pgraft: Background worker registration completed");

	elog(INFO, "pgraft: Extension initialized successfully");
}

/*
 * Register the background worker
 */
void
pgraft_register_worker(void)
{
	BackgroundWorker worker;
	BackgroundWorkerHandle *handle = NULL;
	BgwHandleStatus status;
	pid_t pid;

	memset(&worker, 0, sizeof(BackgroundWorker));

	worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	worker.bgw_restart_time = BGW_DEFAULT_RESTART_INTERVAL;

	/* Entry via library + symbol */
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "pgraft");          /* .so name */
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "pgraft_main");    /* symbol */

	snprintf(worker.bgw_name, BGW_MAXLEN, "pgraft worker");
	snprintf(worker.bgw_type, BGW_MAXLEN, "pgraft");
	worker.bgw_main_arg = (Datum) 0;

	if (process_shared_preload_libraries_in_progress)
	{
		/* Static worker at postmaster start */
		worker.bgw_notify_pid = 0;
		RegisterBackgroundWorker(&worker);
		elog(LOG, "pgraft: background worker registered (preload)");
		return;
	}

	/* For dynamic loading, try to register a worker if none is running */
	elog(LOG, "pgraft: Extension loaded dynamically - attempting to register background worker");
	
	worker.bgw_notify_pid = MyProcPid;
	
	if (!RegisterDynamicBackgroundWorker(&worker, &handle))
	{
		elog(WARNING, "pgraft: could not register background worker dynamically - it may already be running");
		return;
	}
	
	status = WaitForBackgroundWorkerStartup(handle, &pid);
	if (status != BGWH_STARTED)
	{
		elog(WARNING, "pgraft: background worker failed to start dynamically - it may already be running");
		return;
	}
	
	elog(LOG, "pgraft: background worker started dynamically (pid %d)", (int) pid);
}

/*
 * Background worker main function
 * This function must be exported for PostgreSQL background workers
 */
PGDLLEXPORT void
pgraft_main(Datum main_arg)
{
	/* Variable declarations at the top - PostgreSQL C standard */
	pgraft_worker_state_t *state;
	pgraft_command_t cmd;
	int sleep_count = 0;
	
	/* Debug logging */
	elog(LOG, "pgraft: Background worker main function started");
	
	/* Set up signal handling */
	BackgroundWorkerUnblockSignals();
	
	elog(LOG, "pgraft: Background worker signal handling set up");
	
	/* Get worker state */
	state = pgraft_worker_get_state();
	if (state == NULL) {
		elog(ERROR, "pgraft: Failed to get worker state in background worker");
		return;
	}
	elog(LOG, "pgraft: Worker state obtained successfully");

	/* Initialize worker state */
	state->status = WORKER_STATUS_RUNNING;
	elog(LOG, "pgraft: Worker status set to RUNNING");

	/* Log startup */
	elog(LOG, "pgraft: Background worker started and running");

	/* Main worker loop - process command queue */
	while (state->status != WORKER_STATUS_STOPPED) {
		/* Process commands from queue */
		if (pgraft_dequeue_command(&cmd)) {
			elog(LOG, "pgraft: Worker processing command %d for node %d", cmd.type, cmd.node_id);
			
			/* Add to status tracking */
			pgraft_add_command_to_status(&cmd);
			
			/* Mark as processing */
			cmd.status = COMMAND_STATUS_PROCESSING;
			
			switch (cmd.type) {
				case COMMAND_INIT:
					/* Call init function */
					if (pgraft_init_system(cmd.node_id, cmd.address, cmd.port) != 0) {
						cmd.status = COMMAND_STATUS_FAILED;
						strncpy(cmd.error_message, "Failed to initialize pgraft system", 
								sizeof(cmd.error_message) - 1);
					} else {
						/* Update worker state */
						state->node_id = cmd.node_id;
						strncpy(state->address, cmd.address, sizeof(state->address) - 1);
						state->address[sizeof(state->address) - 1] = '\0';
						state->port = cmd.port;
						state->status = WORKER_STATUS_RUNNING;
						
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_ADD_NODE:
					/* Call add node function */
					if (pgraft_add_node_system(cmd.node_id, cmd.address, cmd.port) != 0) {
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to add node %d to pgraft system", cmd.node_id);
					} else {
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_REMOVE_NODE:
					/* Call remove node function */
					if (pgraft_remove_node_system(cmd.node_id) != 0) {
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to remove node %d from pgraft system", cmd.node_id);
					} else {
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_LOG_APPEND:
					/* Call log append function */
					if (pgraft_log_append_system(cmd.log_data, cmd.log_index) != 0) {
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to append log entry at index %d", cmd.log_index);
					} else {
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_LOG_COMMIT:
					/* Call log commit function */
					if (pgraft_log_commit_system(cmd.log_index) != 0) {
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to commit log entry at index %d", cmd.log_index);
					} else {
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_LOG_APPLY:
					/* Call log apply function */
					if (pgraft_log_apply_system(cmd.log_index) != 0) {
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to apply log entry at index %d", cmd.log_index);
					} else {
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_SHUTDOWN:
					elog(LOG, "pgraft: SHUTDOWN command received");
					state->status = WORKER_STATUS_STOPPED;
					cmd.status = COMMAND_STATUS_COMPLETED;
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				default:
					elog(WARNING, "pgraft: Unknown command type %d", cmd.type);
					cmd.status = COMMAND_STATUS_FAILED;
					snprintf(cmd.error_message, sizeof(cmd.error_message), 
							"Unknown command type %d", cmd.type);
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
			}
		}

		/* Sleep for a short time to avoid busy waiting */
		pg_usleep(1000000); /* 1 second */
		
		/* Log every 10 seconds to show we're alive */
		sleep_count++;
		if (sleep_count >= 10) {
			elog(LOG, "pgraft: Background worker running... (alive check)");
			sleep_count = 0;
		}
	}

	/* Cleanup */
	state->status = WORKER_STATUS_STOPPED;
	elog(LOG, "pgraft: Background worker stopped");
}

/*
 * Get worker state from shared memory
 */
pgraft_worker_state_t *
pgraft_worker_get_state(void)
{
	static pgraft_worker_state_t *worker_state = NULL;
	bool found;
	
	if (worker_state == NULL) {
		worker_state = (pgraft_worker_state_t *) ShmemInitStruct("pgraft_worker_state",
																 sizeof(pgraft_worker_state_t),
																 &found);
		if (!found) {
			/* Initialize worker state */
			worker_state->node_id = 0;
			worker_state->port = 0;
			strlcpy(worker_state->address, "127.0.0.1", sizeof(worker_state->address));
			worker_state->status = WORKER_STATUS_STOPPED;
			
			/* Initialize circular buffers */
			worker_state->command_head = 0;
			worker_state->command_tail = 0;
			worker_state->command_count = 0;
			worker_state->status_head = 0;
			worker_state->status_tail = 0;
			worker_state->status_count = 0;
		}
	}
	return worker_state;
}

/*
 * Initialize pgraft system
 */
static int
pgraft_init_system(int node_id, const char *address, int port)
{
	/* Variable declarations at the top - PostgreSQL C standard */
	pgraft_go_init_func init_func;
	pgraft_go_start_network_server_func start_network_server;

	/* Initialize core system */
	if (pgraft_core_init(node_id, (char *)address, port) != 0) {
		elog(WARNING, "pgraft: Failed to initialize core system");
		return -1;
	}
	elog(LOG, "pgraft: Core system initialized");

	/* Load Go library */
	if (pgraft_go_load_library() != 0) {
		elog(WARNING, "pgraft: Failed to load Go library");
		return -1;
	}
	elog(LOG, "pgraft: Go library loaded");

	/* Initialize Go Raft library */
	init_func = pgraft_go_get_init_func();
	if (!init_func) {
		elog(WARNING, "pgraft: Failed to get Go init function");
		return -1;
	}

	if (init_func(node_id, (char *)address, port) != 0) {
		elog(WARNING, "pgraft: Failed to initialize Go Raft library");
		return -1;
	}
	elog(LOG, "pgraft: Go Raft library initialized");

	/* Start network server */
	start_network_server = pgraft_go_get_start_network_server_func();
	if (!start_network_server) {
		elog(WARNING, "pgraft: Failed to get Go network server function");
		return -1;
	}

	if (start_network_server(port) != 0) {
		elog(WARNING, "pgraft: Failed to start Go network server");
		return -1;
	}
	elog(LOG, "pgraft: Go network server started on port %d", port);

	return 0;
}

/*
 * Add node to pgraft system
 */
static int
pgraft_add_node_system(int node_id, const char *address, int port)
{
	/* Variable declarations at the top - PostgreSQL C standard */
	pgraft_go_add_peer_func add_peer_func;

	/* Add to core system */
	if (pgraft_core_add_node(node_id, (char *)address, port) != 0) {
		elog(WARNING, "pgraft: Failed to add node %d to core system", node_id);
		return -1;
	}
	elog(LOG, "pgraft: Node %d added to core system", node_id);

	/* Add to Go Raft library if loaded */
	if (pgraft_go_is_loaded()) {
		add_peer_func = pgraft_go_get_add_peer_func();
		if (add_peer_func) {
			if (add_peer_func(node_id, (char *)address, port) != 0) {
				elog(WARNING, "pgraft: Failed to add node %d to Go Raft library", node_id);
				return -1;
			}
			elog(LOG, "pgraft: Node %d added to Go Raft library", node_id);
		}
	}

	elog(INFO, "pgraft: Node %d successfully added to cluster", node_id);
	return 0;
}

/*
 * Remove node from pgraft system
 */
static int
pgraft_remove_node_system(int node_id)
{
	/* Variable declarations at the top - PostgreSQL C standard */
	pgraft_go_remove_peer_func remove_peer_func;

	/* Remove from Go Raft library if loaded */
	if (pgraft_go_is_loaded()) {
		remove_peer_func = pgraft_go_get_remove_peer_func();
		if (remove_peer_func) {
			if (remove_peer_func(node_id) != 0) {
				elog(WARNING, "pgraft: Failed to remove node %d from Go Raft library", node_id);
				return -1;
			}
			elog(LOG, "pgraft: Node %d removed from Go Raft library", node_id);
		}
	}

	/* Remove from core system */
	if (pgraft_core_remove_node(node_id) != 0) {
		elog(WARNING, "pgraft: Failed to remove node %d from core system", node_id);
		return -1;
	}

	elog(INFO, "pgraft: Node %d successfully removed from cluster", node_id);
	return 0;
}

/*
 * Append log entry to pgraft system
 */
static int
pgraft_log_append_system(const char *log_data, int log_index)
{
	if (pgraft_log_append_entry((int64_t)log_index, log_data, strlen(log_data)) != 0) {
		elog(WARNING, "pgraft: Failed to append log entry at index %d", log_index);
		return -1;
	}
	elog(INFO, "pgraft: Log entry at index %d appended", log_index);
	return 0;
}

/*
 * Commit log entry in pgraft system
 */
static int
pgraft_log_commit_system(int log_index)
{
	if (pgraft_log_commit_entry((int64_t)log_index) != 0) {
		elog(WARNING, "pgraft: Failed to commit log entry at index %d", log_index);
		return -1;
	}
	elog(INFO, "pgraft: Log entry at index %d committed", log_index);
	return 0;
}

/*
 * Apply log entry in pgraft system
 */
static int
pgraft_log_apply_system(int log_index)
{
	if (pgraft_log_apply_entry((int64_t)log_index) != 0) {
		elog(WARNING, "pgraft: Failed to apply log entry at index %d", log_index);
		return -1;
	}
	elog(INFO, "pgraft: Log entry at index %d applied", log_index);
	return 0;
}

/*
 * Extension cleanup
 */
void
_PG_fini(void)
{
	elog(INFO, "pgraft: Extension cleanup completed");
}

