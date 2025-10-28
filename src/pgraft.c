/*-------------------------------------------------------------------------
 *
 * pgraft.c
 *      Main pgraft extension file with clean modular architecture
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */

/* Use separate JSON parsing module to avoid naming conflicts */

#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "storage/shmem.h"
#include "storage/ipc.h"
#include "utils/elog.h"
#include "utils/builtins.h"
#include "postmaster/bgworker.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "storage/proc.h"
#include <time.h>
#include "utils/ps_status.h"

#include "../include/pgraft_core.h"
#include "../include/pgraft_go.h"
#include "../include/pgraft_state.h"
#include "../include/pgraft_log.h"
#include "../include/pgraft_kv.h"
#include "../include/pgraft_guc.h"
#include "../include/pgraft_sql.h"
#include "../include/pgraft_json.h"
#include "../include/pgraft_apply.h"

/* Function declarations */
/* Forward declarations */
static int pgraft_init_system(int node_id, const char *address, int port);
static int pgraft_add_node_system(int node_id, const char *address, int port);
static int pgraft_remove_node_system(int node_id);
static int pgraft_log_append_system(const char *log_data, int log_index);
static int pgraft_log_commit_system(int log_index);
static int pgraft_log_apply_system(int log_index);
/* Function declaration moved to header */

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
	/* Use ConsistentState instead of RecoveryFinished so worker starts on standbys too */
	worker.bgw_start_time = BgWorkerStart_ConsistentState;
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
	
	/* Wait for PostgreSQL to be fully ready before initializing Raft */
	/* On standbys, wait for them to reach consistent hot standby state */
	elog(LOG, "pgraft: waiting for PostgreSQL to be ready (primary or stable standby)...");
	
	/* Give PostgreSQL time to fully initialize */
	pg_usleep(3000000L); /* 3 seconds initial wait */
	
	/* Additional wait for standby to be stable (if in recovery) */
	if (RecoveryInProgress())
	{
		elog(LOG, "pgraft: node is in recovery (standby mode), waiting for stable hot standby state...");
		/* Wait for standby to be ready for read queries */
		pg_usleep(5000000L); /* 5 seconds */
		elog(LOG, "pgraft: standby is now in stable hot standby mode");
	}
	else
	{
		elog(LOG, "pgraft: node is primary (not in recovery)");
	}
	
	elog(LOG, "pgraft: PostgreSQL is ready, proceeding with Raft initialization");
	
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
	
	/* Start the Go Raft goroutines */
	elog(LOG, "pgraft: background worker starting Go Raft goroutines");
	if (pgraft_go_start() != 0)
	{
		elog(ERROR, "pgraft: background worker failed to start Go Raft goroutines");
		return;
	}
	elog(LOG, "pgraft: background worker Go Raft goroutines started successfully");
	
	/* Start the background ticker and processing loops */
	elog(LOG, "pgraft: background worker starting Raft ticker and processing loops");
	if (pgraft_go_start_background() != 0)
	{
		elog(ERROR, "pgraft: background worker failed to start ticker");
		return;
	}
	elog(LOG, "pgraft: background worker Raft ticker started successfully");
	
	/* Establish initial connections to all cluster peers */
	elog(LOG, "pgraft: background worker connecting to cluster peers");
	if (pgraft_go_connect_to_peers() != 0)
	{
		elog(WARNING, "pgraft: background worker failed to connect to some peers");
	}
	elog(LOG, "pgraft: background worker peer connections initiated");
	
	BackgroundWorkerUnblockSignals();
	elog(LOG, "pgraft: background worker signal handling set up");
	
	state = pgraft_worker_get_state();
	if (state == NULL)
	{
		elog(ERROR, "pgraft: failed to get worker state in background worker");
		return;
	}
	elog(LOG, "pgraft: worker state obtained successfully");

	/* Populate worker state with node information from GUC settings */
	state->node_id = 0; /* Will be set by pgraft_update_shared_memory_from_go */
	strncpy(state->address, name ? name : "unknown", sizeof(state->address) - 1);
	state->address[sizeof(state->address) - 1] = '\0';
	state->port = 0; /* Will be populated from configuration */
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
					
				case COMMAND_KV_PUT:
					{
						char json_data[2048];
						int result;
						
						elog(LOG, "pgraft: processing COMMAND_KV_PUT for key=%s", cmd.kv_key);
						
						if (pgraft_go_is_loaded())
						{
							/* Create JSON data for Raft replication using json-c */
							if (pgraft_json_create_kv_operation(PGRAFT_KV_PUT, cmd.kv_key, cmd.kv_value, cmd.kv_client_id, json_data, sizeof(json_data)) != 0) {
								elog(ERROR, "pgraft: failed to create JSON for KV PUT operation");
								continue;
							}
							
							elog(LOG, "pgraft: calling pgraft_go_append_log with data=%s", json_data);
							
							/* Replicate through Raft */
							result = pgraft_go_append_log(json_data, strlen(json_data));
							
							elog(LOG, "pgraft: pgraft_go_append_log returned result=%d", result);
							
							if (result < 0)
							{
								cmd.status = COMMAND_STATUS_FAILED;
								snprintf(cmd.error_message, sizeof(cmd.error_message), 
										"Failed to replicate KV PUT operation through Raft (result=%d)", result);
								elog(WARNING, "pgraft: %s", cmd.error_message);
							}
							else
							{
								cmd.status = COMMAND_STATUS_COMPLETED;
								elog(LOG, "pgraft: KV PUT operation successfully replicated");
							}
						}
						else
						{
							cmd.status = COMMAND_STATUS_FAILED;
							snprintf(cmd.error_message, sizeof(cmd.error_message), 
									"Go layer not loaded, cannot replicate KV operation");
							elog(WARNING, "pgraft: %s", cmd.error_message);
						}
						pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					}
					break;
					
				case COMMAND_KV_DELETE:
					{
						char json_data[2048];
						int result;
						
						elog(LOG, "pgraft: processing COMMAND_KV_DELETE for key=%s", cmd.kv_key);
						
						if (pgraft_go_is_loaded())
						{
							/* Create JSON data for Raft replication using json-c */
							if (pgraft_json_create_kv_operation(PGRAFT_KV_DELETE, cmd.kv_key, NULL, cmd.kv_client_id, json_data, sizeof(json_data)) != 0) {
								elog(ERROR, "pgraft: failed to create JSON for KV DELETE operation");
								continue;
							}
							
							elog(LOG, "pgraft: calling pgraft_go_append_log with data=%s", json_data);
							
							/* Replicate through Raft */
							result = pgraft_go_append_log(json_data, strlen(json_data));
							
							elog(LOG, "pgraft: pgraft_go_append_log returned result=%d", result);
							
							if (result < 0)
							{
								cmd.status = COMMAND_STATUS_FAILED;
								snprintf(cmd.error_message, sizeof(cmd.error_message), 
										"Failed to replicate KV DELETE operation through Raft (result=%d)", result);
								elog(WARNING, "pgraft: %s", cmd.error_message);
							}
							else
							{
								cmd.status = COMMAND_STATUS_COMPLETED;
								elog(LOG, "pgraft: KV DELETE operation successfully replicated");
							}
						}
						else
						{
							cmd.status = COMMAND_STATUS_FAILED;
							snprintf(cmd.error_message, sizeof(cmd.error_message), 
									"Go layer not loaded, cannot replicate KV operation");
							elog(WARNING, "pgraft: %s", cmd.error_message);
						}
						pgraft_update_command_status(cmd.timestamp, cmd.status, cmd.error_message);
					}
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
 * Write cluster state to persistence file with nodes list
 * This allows replicas and other processes to read the state without accessing Go library
 */
static void
pgraft_write_state_to_file_with_nodes(int64_t leader_id, int32_t term, int64_t node_id, const char *nodes_json)
{
	char filepath[MAXPGPATH];
	char temppath[MAXPGPATH];
	FILE *fp;
	const char *data_dir;
	
	/* Get pgraft data directory from GUC */
	data_dir = GetConfigOption("pgraft.data_dir", false, false);
	if (!data_dir)
		data_dir = "pgraft-data";
	
	/* Construct file path */
	snprintf(filepath, sizeof(filepath), "%s/cluster_state.json", data_dir);
	snprintf(temppath, sizeof(temppath), "%s/cluster_state.json.tmp", data_dir);
	
	/* Write to temporary file first (atomic update) */
	fp = fopen(temppath, "w");
	if (!fp)
	{
		elog(DEBUG1, "pgraft: Could not open state file for writing: %s", temppath);
		return;
	}
	
	fprintf(fp, "{\n");
	fprintf(fp, "  \"leader_id\": %lld,\n", (long long)leader_id);
	fprintf(fp, "  \"term\": %d,\n", term);
	fprintf(fp, "  \"node_id\": %lld,\n", (long long)node_id);
	fprintf(fp, "  \"nodes\": %s,\n", nodes_json);
	fprintf(fp, "  \"updated_at\": %ld\n", (long)time(NULL));
	fprintf(fp, "}\n");
	
	fclose(fp);
	
	/* Atomic rename */
	if (rename(temppath, filepath) != 0)
	{
		elog(DEBUG1, "pgraft: Could not rename state file: %s -> %s", temppath, filepath);
		unlink(temppath);
	}
}

/*
 * Write cluster state to persistence file (without nodes list)
 * This allows replicas and other processes to read the state without accessing Go library
 */
static void
pgraft_write_state_to_file(int64_t leader_id, int32_t term, int64_t node_id)
{
	char filepath[MAXPGPATH];
	char temppath[MAXPGPATH];
	FILE *fp;
	const char *data_dir;
	
	/* Get pgraft data directory from GUC */
	data_dir = GetConfigOption("pgraft.data_dir", false, false);
	if (!data_dir)
		data_dir = "pgraft-data";
	
	/* Construct file path */
	snprintf(filepath, sizeof(filepath), "%s/cluster_state.json", data_dir);
	snprintf(temppath, sizeof(temppath), "%s/cluster_state.json.tmp", data_dir);
	
	/* Write to temporary file first (atomic update) */
	fp = fopen(temppath, "w");
	if (!fp)
	{
		elog(DEBUG1, "pgraft: Could not open state file for writing: %s", temppath);
		return;
	}
	
	fprintf(fp, "{\n");
	fprintf(fp, "  \"leader_id\": %lld,\n", (long long)leader_id);
	fprintf(fp, "  \"term\": %d,\n", term);
	fprintf(fp, "  \"node_id\": %lld,\n", (long long)node_id);
	fprintf(fp, "  \"updated_at\": %ld\n", (long)time(NULL));
	fprintf(fp, "}\n");
	
	fclose(fp);
	
	/* Atomic rename */
	if (rename(temppath, filepath) != 0)
	{
		elog(DEBUG1, "pgraft: Could not rename state file: %s -> %s", temppath, filepath);
		unlink(temppath);
	}
}

/*
 * Read cluster state from persistence file
 * Returns 0 on success, -1 on failure
 */
int
pgraft_read_state_from_file(int64_t *leader_id, int32_t *term, int64_t *node_id)
{
	char filepath[MAXPGPATH];
	FILE *fp;
	const char *data_dir;
	char line[256];
	int fields_read = 0;
	
	/* Get pgraft data directory from GUC */
	data_dir = GetConfigOption("pgraft.data_dir", false, false);
	if (!data_dir)
		data_dir = "pgraft-data";
	
	/* Construct file path */
	snprintf(filepath, sizeof(filepath), "%s/cluster_state.json", data_dir);
	
	/* Open file */
	fp = fopen(filepath, "r");
	if (!fp)
	{
		return -1;  /* File doesn't exist yet */
	}
	
	/* Parse simple JSON */
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		if (sscanf(line, "  \"leader_id\": %lld,", (long long*)leader_id) == 1)
			fields_read++;
		else if (sscanf(line, "  \"term\": %d,", term) == 1)
			fields_read++;
		else if (sscanf(line, "  \"node_id\": %lld,", (long long*)node_id) == 1)
			fields_read++;
	}
	
	fclose(fp);
	
	return (fields_read >= 3) ? 0 : -1;
}

/*
 * Update shared memory with current state from Go library
 */
void
pgraft_update_shared_memory_from_go(void)
{
	pgraft_cluster_t *shm_cluster;
	pgraft_go_get_leader_func get_leader_func;
	pgraft_go_get_term_func get_term_func;
	pgraft_go_get_node_id_func get_node_id_func;
	pgraft_worker_state_t *worker_state;
	int64_t current_leader;
	int32_t current_term;
	int64_t go_node_id;

	if (!pgraft_go_is_loaded())
		return;
	
	get_leader_func = pgraft_go_get_get_leader_func();
	get_term_func = pgraft_go_get_get_term_func();
	get_node_id_func = pgraft_go_get_get_node_id_func();
	
	if (!get_leader_func || !get_term_func || !get_node_id_func)
		return;
	
	/* Get shared memory */
	shm_cluster = pgraft_core_get_shared_memory();
	if (!shm_cluster) {
		return;
	}
	
	/* Get current state from Go library */
	current_leader = get_leader_func();
	current_term = get_term_func();
	
	/* Get worker state to populate node_id */
	worker_state = pgraft_worker_get_state();
	
	/* Get node_id from Go layer (auto-generated from name hash) */
	go_node_id = get_node_id_func();
	
	/* Get nodes list from Go library using json-c */
	/* Also save it for writing to persistence file */
	char *nodes_json_for_file = NULL;
	{
		pgraft_go_get_nodes_func get_nodes_func = pgraft_go_get_get_nodes_func();
		if (get_nodes_func)
		{
			char *nodes_json = get_nodes_func();
			elog(LOG, "pgraft: DEBUG - Got nodes JSON from Go: '%s'", nodes_json ? nodes_json : "NULL");
			if (nodes_json && strcmp(nodes_json, "[]") != 0)
			{
				/* Keep a copy for persistence file */
				nodes_json_for_file = pstrdup(nodes_json);
				
				/* Parse JSON using separate module */
				int node_count;
				int32_t node_ids[16];
				char *addresses[16];
				char address_buf[16][256];
				int i;
				
				/* Initialize */
				for (i = 0; i < 16; i++) {
					addresses[i] = address_buf[i];
					address_buf[i][0] = '\0';
				}
				
				node_count = pgraft_parse_nodes_json(nodes_json, node_ids, addresses, 16);
				
				if (node_count > 0)
				{
					elog(LOG, "pgraft: DEBUG - Parsed %d nodes, calling pgraft_core_update_nodes", node_count);
					pgraft_core_update_nodes(node_count, node_ids, addresses);
				}
				else if (node_count == -1)
				{
					elog(LOG, "pgraft: DEBUG - Failed to parse JSON");
				}
				else
				{
					elog(LOG, "pgraft: DEBUG - No valid nodes parsed from JSON");
				}
				
				pgraft_go_free_string(nodes_json);
			}
		}
	}
	
	/* Update shared memory with current Go state */
	SpinLockAcquire(&shm_cluster->mutex);
	shm_cluster->leader_id = current_leader;
	shm_cluster->current_term = current_term;
	shm_cluster->initialized = true;  /* Mark as initialized */
	
	/* Update node_id from Go layer */
	if (go_node_id > 0) {
		shm_cluster->node_id = go_node_id;
		/* Also update worker state for consistency */
		if (worker_state) {
			worker_state->node_id = go_node_id;
		}
	}
	
	/* Update state based on leader status */
	if (current_leader > 0) {
		/* Check if current node is the leader */
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
	
	/* Write state to persistence file for processes that can't access shared memory */
	if (nodes_json_for_file)
	{
		pgraft_write_state_to_file_with_nodes(current_leader, current_term, go_node_id, nodes_json_for_file);
		pfree(nodes_json_for_file);
	}
	else
	{
		pgraft_write_state_to_file(current_leader, current_term, go_node_id);
	}
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
 * SQL function wrapper for pgraft_kv_put_local
 */
PG_FUNCTION_INFO_V1(pgraft_kv_put_local_func);
Datum
pgraft_kv_put_local_func(PG_FUNCTION_ARGS)
{
	text	   *key_text = PG_GETARG_TEXT_PP(0);
	text	   *value_text = PG_GETARG_TEXT_PP(1);
	char	   *key;
	char	   *value;
	int			result;
	
	key = text_to_cstring(key_text);
	value = text_to_cstring(value_text);
	
	result = pgraft_kv_put_local(key, value);
	
	PG_RETURN_BOOL(result == 0);
}

/*
 * SQL function wrapper for pgraft_kv_delete_local
 */
PG_FUNCTION_INFO_V1(pgraft_kv_delete_local_func);
Datum
pgraft_kv_delete_local_func(PG_FUNCTION_ARGS)
{
	text	   *key_text = PG_GETARG_TEXT_PP(0);
	char	   *key;
	int			result;
	
	key = text_to_cstring(key_text);
	
	result = pgraft_kv_delete_local(key);
	
	PG_RETURN_BOOL(result == 0);
}

/*
 * Get last applied index
 */
PG_FUNCTION_INFO_V1(pgraft_get_applied_index_func);
Datum
pgraft_get_applied_index_func(PG_FUNCTION_ARGS)
{
	uint64		last_applied;
	
	last_applied = pgraft_get_applied_index();
	
	PG_RETURN_INT64((int64) last_applied);
}

/*
 * Record applied index
 */
PG_FUNCTION_INFO_V1(pgraft_record_applied_index_func);
Datum
pgraft_record_applied_index_func(PG_FUNCTION_ARGS)
{
	int64		index = PG_GETARG_INT64(0);
	
	pgraft_record_applied_index((uint64) index);
	
	PG_RETURN_VOID();
}

/*
 * Extension cleanup
 */
void
_PG_fini(void)
{
	elog(INFO, "pgraft: extension cleanup completed");
}

