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
#include "../include/pgraft_sql.h"

/* Function declarations */
/* Forward declarations */
static int pgraft_init_system(int node_id, const char *address, int port);
static int pgraft_add_node_system(int node_id, const char *address, int port);
static int pgraft_remove_node_system(int node_id);
static int pgraft_log_append_system(const char *log_data, int log_index);
static int pgraft_log_commit_system(int log_index);
static int pgraft_log_apply_system(int log_index);
static void pgraft_update_shared_memory_from_go(void);

/* Extension cleanup function */
void _PG_fini(void);

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
	
	elog(LOG, "pgraft: shared memory request hook completed");
}


/*
 * Shared memory startup hook
 */
static void
pgraft_shmem_startup_hook(void)
{
	elog(LOG, "pgraft: shared memory startup hook called");
	
	/* Initialize shared memory structures */
	pgraft_core_init_shared_memory();
	pgraft_go_init_shared_memory();
	pgraft_log_init_shared_memory();
	pgraft_kv_init_shared_memory();
	pgraft_worker_init_shared_memory();
	
	elog(LOG, "pgraft: all shared memory structures initialized");
}

/*
 * Extension initialization
 */
void
_PG_init(void)
{
	elog(INFO, "pgraft: initializing extension version %s", PGRAFT_VERSION);

	/* Install shared memory request hook */
	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = pgraft_shmem_request_hook;
	elog(LOG, "pgraft: shared memory request hook installed");

	/* Install shared memory startup hook */
	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = pgraft_shmem_startup_hook;
	elog(LOG, "pgraft: shared memory startup hook installed");

	/* Register GUC variables */
	pgraft_register_guc_variables();
	elog(LOG, "pgraft: guc variables registered");

	/* Register background worker */
	pgraft_register_worker();
	elog(LOG, "pgraft: background worker registration completed");

	elog(INFO, "pgraft: extension initialized successfully");
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
	elog(LOG, "pgraft: extension loaded dynamically - attempting to register background worker");
	
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
	pgraft_worker_state_t *state;
	pgraft_command_t cmd;
	int sleep_count;
	int tick_result;
	
	sleep_count = 0;
	
	elog(LOG, "pgraft: background worker main function started");
	
	/* Load Go library in the background worker process */
	if (!pgraft_go_is_loaded())
	{
		elog(LOG, "pgraft: background worker loading Go library");
		if (pgraft_go_load_library() != 0)
		{
			elog(ERROR, "pgraft: background worker failed to load Go library");
			return;
		}
		elog(LOG, "pgraft: background worker Go library loaded successfully");
	}
	else
	{
		elog(LOG, "pgraft: background worker Go library already loaded");
	}
	
	/* Initialize Raft in the background worker (not in client backend) */
	elog(LOG, "pgraft: background worker initializing raft consensus");
	if (pgraft_init_from_gucs() != 0)
	{
		elog(ERROR, "pgraft: background worker failed to initialize raft");
		return;
	}
	elog(LOG, "pgraft: background worker raft initialization successful");
	
	/* Establish initial connections to all cluster peers */
	/* TEMPORARILY DISABLED - will be re-enabled after verifying cluster starts */
	/* elog(LOG, "pgraft: background worker connecting to cluster peers");
	if (pgraft_go_connect_to_peers() != 0)
	{
		elog(WARNING, "pgraft: background worker failed to connect to some peers");
	}
	elog(LOG, "pgraft: background worker peer connections initiated"); */
	
	BackgroundWorkerUnblockSignals();
	elog(LOG, "pgraft: background worker signal handling set up");
	
	state = pgraft_worker_get_state();
	if (state == NULL)
	{
		elog(ERROR, "pgraft: failed to get worker state in background worker");
		return;
	}
	elog(LOG, "pgraft: worker state obtained successfully");

	state->status = WORKER_STATUS_RUNNING;
	elog(LOG, "pgraft: worker status set to RUNNING");
	elog(LOG, "pgraft: background worker started and running");

	while (state->status != WORKER_STATUS_STOPPED)
	{
		if (sleep_count % 5 == 0)
		{
			elog(LOG, "pgraft: worker loop - command_count=%d, head=%d, tail=%d", 
				 state->command_count, state->command_head, state->command_tail);
		}
		
		if (pgraft_go_is_loaded())
		{
			tick_result = pgraft_go_tick();
			if (sleep_count % 20 == 0)
			{
				elog(LOG, "pgraft: calling tick... result=%d", tick_result);
			}
		}
		
		/* Update shared memory with current Go library state every 5 iterations */
		/* Only update if Go library is loaded */
		if (sleep_count % 5 == 0 && pgraft_go_is_loaded())
		{
			pgraft_update_shared_memory_from_go();
		}
		
		if (sleep_count % 10 == 0 && pgraft_go_is_loaded())
		{
			(void) pgraft_go_trigger_heartbeat();
		}
		
		if (pgraft_dequeue_command(&cmd))
		{
			elog(LOG, "pgraft: worker processing command %d for node %d", cmd.type, cmd.node_id);
			
			pgraft_add_command_to_status(&cmd);
			cmd.status = COMMAND_STATUS_PROCESSING;
			
			switch (cmd.type)
			{
				case COMMAND_INIT:
					if (pgraft_init_system(cmd.node_id, cmd.address, cmd.port) != 0)
					{
						cmd.status = COMMAND_STATUS_FAILED;
						strncpy(cmd.error_message, "Failed to initialize pgraft system", 
								sizeof(cmd.error_message) - 1);
					}
					else
					{
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
					if (pgraft_add_node_system(cmd.node_id, cmd.address, cmd.port) != 0)
					{
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to add node %d to pgraft system", cmd.node_id);
					}
					else
					{
						cmd.status = COMMAND_STATUS_COMPLETED;
						pg_usleep(2000000);
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_REMOVE_NODE:
					if (pgraft_remove_node_system(cmd.node_id) != 0)
					{
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to remove node %d from pgraft system", cmd.node_id);
					}
					else
					{
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_LOG_APPEND:
					if (pgraft_log_append_system(cmd.log_data, cmd.log_index) != 0)
					{
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to append log entry at index %d", cmd.log_index);
					}
					else
					{
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_LOG_COMMIT:
					if (pgraft_log_commit_system(cmd.log_index) != 0)
					{
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to commit log entry at index %d", cmd.log_index);
					}
					else
					{
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_LOG_APPLY:
					if (pgraft_log_apply_system(cmd.log_index) != 0)
					{
						cmd.status = COMMAND_STATUS_FAILED;
						snprintf(cmd.error_message, sizeof(cmd.error_message), 
								"Failed to apply log entry at index %d", cmd.log_index);
					}
					else
					{
						cmd.status = COMMAND_STATUS_COMPLETED;
					}
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				case COMMAND_SHUTDOWN:
					elog(LOG, "pgraft: shutdown command received");
					state->status = WORKER_STATUS_STOPPED;
					cmd.status = COMMAND_STATUS_COMPLETED;
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
					
				default:
					elog(WARNING, "pgraft: unknown command type %d", cmd.type);
					cmd.status = COMMAND_STATUS_FAILED;
					snprintf(cmd.error_message, sizeof(cmd.error_message), 
							"Unknown command type %d", cmd.type);
					pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					break;
			}
		}

		pg_usleep(100000);
		sleep_count++;
		
		if (sleep_count >= 10)
		{
			elog(LOG, "pgraft: background worker running... (alive check)");
			sleep_count = 0;
		}
	}

	state->status = WORKER_STATUS_STOPPED;
	elog(LOG, "pgraft: background worker stopped");
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
	pgraft_go_init_func init_func;

	if (pgraft_core_init(node_id, (char *)address, port) != 0)
	{
		elog(WARNING, "pgraft: failed to initialize core system");
		return -1;
	}
	elog(LOG, "pgraft: core system initialized");

	if (pgraft_go_load_library() != 0)
	{
		elog(WARNING, "pgraft: failed to load Go library");
		return -1;
	}
	elog(LOG, "pgraft: go library loaded");

	init_func = pgraft_go_get_init_func();
	if (!init_func)
	{
		elog(WARNING, "pgraft: failed to get Go init function");
		return -1;
	}

	if (init_func(node_id, (char *)address, port) != 0)
	{
		elog(WARNING, "pgraft: failed to initialize Go Raft library");
		return -1;
	}
	elog(LOG, "pgraft: go raft library initialized");

	if (pgraft_go_start() != 0)
	{
		elog(WARNING, "pgraft: failed to start Go Raft goroutines");
		return -1;
	}
	elog(LOG, "pgraft: go raft goroutines started successfully");

	if (pgraft_go_start_network_server(port) != 0)
	{
		elog(WARNING, "pgraft: failed to start network server");
		return -1;
	}
	elog(LOG, "pgraft: network server started successfully");

	return 0;
}

/*
 * Update shared memory with current state from Go library
 */
static void
pgraft_update_shared_memory_from_go(void)
{
	pgraft_cluster_t *shm_cluster;
	pgraft_go_get_leader_func get_leader_func;
	pgraft_go_get_term_func get_term_func;
	int64_t current_leader;
	int32_t current_term;

	if (!pgraft_go_is_loaded())
		return;
	
	get_leader_func = pgraft_go_get_get_leader_func();
	get_term_func = pgraft_go_get_get_term_func();
	
	if (!get_leader_func || !get_term_func)
		return;
	
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
	pgraft_go_add_peer_func add_peer_func;
	int retry_count;

	if (pgraft_core_add_node(node_id, (char *)address, port) != 0)
	{
		elog(WARNING, "pgraft: failed to add node %d to core system", node_id);
		return -1;
	}
	elog(LOG, "pgraft: node %d added to core system", node_id);

	if (pgraft_go_is_loaded())
	{
		add_peer_func = pgraft_go_get_add_peer_func();
		if (add_peer_func)
		{
			for (retry_count = 0; retry_count < 3; retry_count++)
			{
				if (add_peer_func(node_id, (char *)address, port) == 0)
				{
					elog(LOG, "pgraft: node %d added to go raft library", node_id);
					break;
				}
				if (retry_count < 2)
				{
					elog(WARNING, "pgraft: failed to add node %d to Go Raft library (attempt %d), retrying...", 
						 node_id, retry_count + 1);
					pg_usleep(1000000);
				}
				else
				{
					elog(WARNING, "pgraft: failed to add node %d to Go Raft library after 3 attempts", node_id);
					return -1;
				}
			}
		}
	}

	elog(INFO, "pgraft: node %d successfully added to cluster", node_id);
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
				elog(WARNING, "pgraft: failed to remove node %d from Go Raft library", node_id);
				return -1;
			}
			elog(LOG, "pgraft: node %d removed from go raft library", node_id);
		}
	}

	/* Remove from core system */
	if (pgraft_core_remove_node(node_id) != 0) {
		elog(WARNING, "pgraft: failed to remove node %d from core system", node_id);
		return -1;
	}

	elog(INFO, "pgraft: node %d successfully removed from cluster", node_id);
	return 0;
}

/*
 * Append log entry to pgraft system
 */
static int
pgraft_log_append_system(const char *log_data, int log_index)
{
	if (pgraft_log_append_entry((int64_t)log_index, log_data, strlen(log_data)) != 0) {
		elog(WARNING, "pgraft: failed to append log entry at index %d", log_index);
		return -1;
	}
	elog(INFO, "pgraft: log entry at index %d appended", log_index);
	return 0;
}

/*
 * Commit log entry in pgraft system
 */
static int
pgraft_log_commit_system(int log_index)
{
	if (pgraft_log_commit_entry((int64_t)log_index) != 0) {
		elog(WARNING, "pgraft: failed to commit log entry at index %d", log_index);
		return -1;
	}
	elog(INFO, "pgraft: log entry at index %d committed", log_index);
	return 0;
}

/*
 * Apply log entry in pgraft system
 */
static int
pgraft_log_apply_system(int log_index)
{
	if (pgraft_log_apply_entry((int64_t)log_index) != 0) {
		elog(WARNING, "pgraft: failed to apply log entry at index %d", log_index);
		return -1;
	}
	elog(INFO, "pgraft: log entry at index %d applied", log_index);
	return 0;
}

/*
 * Extension cleanup
 */
void
_PG_fini(void)
{
	elog(INFO, "pgraft: extension cleanup completed");
}

