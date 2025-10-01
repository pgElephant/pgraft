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
#include "storage/ipc.h"
#include "utils/elog.h"
#include "postmaster/bgworker.h"
#include "utils/ps_status.h"


#include "../include/pgraft_core.h"
#include "../include/pgraft_go.h"
#include "../include/pgraft_state.h"
#include "../include/pgraft_log.h"
#include "../include/pgraft_kv.h"
#include "../include/pgraft_guc.h"

/* Function declarations */
/* Forward declarations */
static int pgraft_init_system(int node_id, const char *address, int port);
static int pgraft_add_node_system(int node_id, const char *address, int port);
static int pgraft_remove_node_system(int node_id);
static int pgraft_log_append_system(const char *log_data, int log_index);
static int pgraft_log_commit_system(int log_index);
static int pgraft_log_apply_system(int log_index);
static void pgraft_update_shared_memory_from_go(void);


PG_MODULE_MAGIC;

/* Extension version */
#define PGRAFT_VERSION "1.0.0"

/* Shared memory request hook */
static shmem_request_hook_type prev_shmem_request_hook = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

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
	
	/* Request shared memory for key/value store */
	RequestAddinShmemSpace(sizeof(pgraft_kv_store_t));
	
	/* Request shared memory for background worker state */
	RequestAddinShmemSpace(sizeof(pgraft_worker_state_t));
	
	elog(LOG, "pgraft: Shared memory request hook completed");
}


/*
 * Shared memory startup hook
 */
static void
pgraft_shmem_startup_hook(void)
{
	elog(LOG, "pgraft: Shared memory startup hook called");
	
	/* Initialize shared memory structures */
	pgraft_core_init_shared_memory();
	pgraft_go_init_shared_memory();
	pgraft_log_init_shared_memory();
	pgraft_kv_init_shared_memory();
	pgraft_worker_init_shared_memory();
	
	elog(LOG, "pgraft: All shared memory structures initialized");
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

	/* Install shared memory startup hook */
	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = pgraft_shmem_startup_hook;
	elog(LOG, "pgraft: Shared memory startup hook installed");

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
		/* Register worker during preload but start it later to avoid multithreading issues */
		worker.bgw_start_time = BgWorkerStart_ConsistentState;
		worker.bgw_notify_pid = 0;
		RegisterBackgroundWorker(&worker);
		elog(LOG, "pgraft: background worker registered during preload (will start after consistent state)");
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

	/* Initialize worker state and set to RUNNING */
	state->status = WORKER_STATUS_RUNNING; // Set to RUNNING immediately to keep worker alive
	elog(LOG, "pgraft: Worker status set to RUNNING");

	/* Log startup */
	elog(LOG, "pgraft: Background worker started and running");

	/* Main worker loop - process command queue */
	while (state->status != WORKER_STATUS_STOPPED) {
		/* Debug: Log command count every loop iteration */
		if (sleep_count % 5 == 0) {
			elog(LOG, "pgraft: Worker loop - command_count=%d, head=%d, tail=%d", 
				 state->command_count, state->command_head, state->command_tail);
		}
		
		/* CRITICAL: Call Raft tick on every iteration (100ms) */
		/* This is the worker-driven model where C actively drives Raft progress */
		if (pgraft_go_is_loaded()) {
			int tick_result = pgraft_go_tick();
			if (sleep_count % 20 == 0) {
				elog(LOG, "pgraft: Calling tick... result=%d", tick_result);
			}
		}
		
		/* Update shared memory with current Go library state every 5 iterations */
		/* Only update if Go library is loaded */
		if (sleep_count % 5 == 0 && pgraft_go_is_loaded()) {
			pgraft_update_shared_memory_from_go();
		}
		
		/* Trigger heartbeat every 10 iterations to ensure heartbeats are sent */
		/* Only trigger if Go library is loaded */
		if (sleep_count % 10 == 0 && pgraft_go_is_loaded()) {
			/* Trigger heartbeat - ignore errors as function may not be available yet */
			(void) pgraft_go_trigger_heartbeat();
		}
		
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
						/* Add delay between node additions to prevent configuration conflicts */
						pg_usleep(2000000); /* 2 second delay */
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

	/* Start Go Raft goroutines */
	if (pgraft_go_start() != 0) {
		elog(WARNING, "pgraft: Failed to start Go Raft goroutines");
		return -1;
	}
	elog(LOG, "pgraft: Go Raft goroutines started successfully");

	/* Start network server */
	if (pgraft_go_start_network_server(port) != 0) {
		elog(WARNING, "pgraft: Failed to start network server");
		return -1;
	}
	elog(LOG, "pgraft: Network server started successfully");

	return 0;
}

/*
 * Update shared memory with current state from Go library
 * This function is called by the background worker to keep shared memory in sync
 */
static void
pgraft_update_shared_memory_from_go(void)
{
	pgraft_cluster_t *shm_cluster;
	pgraft_go_get_leader_func get_leader_func;
	pgraft_go_get_term_func get_term_func;
	int64_t current_leader;
	int32_t current_term;

	/* Only update if Go library is loaded */
	if (!pgraft_go_is_loaded()) {
		return;
	}
	
	/* Get function pointers */
	get_leader_func = pgraft_go_get_get_leader_func();
	get_term_func = pgraft_go_get_get_term_func();
	
	if (!get_leader_func || !get_term_func) {
		return;
	}
	
	/* Get shared memory */
	shm_cluster = pgraft_core_get_shared_memory();
	if (!shm_cluster) {
		return;
	}
	
	/* Get current state from Go library */
	current_leader = get_leader_func();
	current_term = get_term_func();
	
	/* Update shared memory with current Go state */
	SpinLockAcquire(&shm_cluster->mutex);
	shm_cluster->leader_id = current_leader;
	shm_cluster->current_term = current_term;
	
	/* Update state based on leader status */
	if (current_leader > 0) {
		/* Check if current node is the leader */
		pgraft_worker_state_t *worker_state = pgraft_worker_get_state();
		if (worker_state && current_leader == (int64_t)worker_state->node_id) {
			strncpy(shm_cluster->state, "leader", sizeof(shm_cluster->state) - 1);
			shm_cluster->state[sizeof(shm_cluster->state) - 1] = '\0';
		} else {
			strncpy(shm_cluster->state, "follower", sizeof(shm_cluster->state) - 1);
			shm_cluster->state[sizeof(shm_cluster->state) - 1] = '\0';
		}
	} else {
		strncpy(shm_cluster->state, "follower", sizeof(shm_cluster->state) - 1);
		shm_cluster->state[sizeof(shm_cluster->state) - 1] = '\0';
	}
	
	SpinLockRelease(&shm_cluster->mutex);
	
	elog(DEBUG1, "pgraft: Updated shared memory from Go library - leader=%lld, term=%d, state=%s", 
		 (long long)current_leader, current_term, shm_cluster->state);
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
			/* Retry adding to Go Raft library up to 3 times */
			int retry_count;
			for (retry_count = 0; retry_count < 3; retry_count++) {
				if (add_peer_func(node_id, (char *)address, port) == 0) {
					elog(LOG, "pgraft: Node %d added to Go Raft library", node_id);
					break;
				}
				if (retry_count < 2) {
					elog(WARNING, "pgraft: Failed to add node %d to Go Raft library (attempt %d), retrying...", 
						 node_id, retry_count + 1);
					pg_usleep(1000000); /* 1 second delay between retries */
				} else {
					elog(WARNING, "pgraft: Failed to add node %d to Go Raft library after 3 attempts", node_id);
					return -1;
				}
			}
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

