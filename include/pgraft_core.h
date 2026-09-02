#ifndef PGRAFT_CORE_H
#define PGRAFT_CORE_H

#include "postgres.h"
#include "port/atomics.h"
#include "storage/shmem.h"
#include "storage/spin.h"

/* Worker status enum */
typedef enum
{
	WORKER_STATUS_IDLE = 0,
	WORKER_STATUS_INITIALIZING = 1,
	WORKER_STATUS_RUNNING = 2,
	WORKER_STATUS_STOPPING = 3,
	WORKER_STATUS_STOPPED = 4
}			WORKER_STATUS;

/* Command types for worker queue */
typedef enum
{
	COMMAND_INIT = 1,
	COMMAND_ADD_NODE = 2,
	COMMAND_REMOVE_NODE = 3,
	COMMAND_LOG_APPEND = 4,
	COMMAND_LOG_COMMIT = 5,
	COMMAND_LOG_APPLY = 6,
	COMMAND_SHUTDOWN = 7,
	COMMAND_KV_PUT = 8,
	COMMAND_KV_DELETE = 9
}			COMMAND_TYPE;

/* Command status enum */
typedef enum
{
	COMMAND_STATUS_PENDING = 0,
	COMMAND_STATUS_PROCESSING = 1,
	COMMAND_STATUS_COMPLETED = 2,
	COMMAND_STATUS_FAILED = 3
}			COMMAND_STATUS;

/* Command structure with status tracking */
typedef struct
{
	COMMAND_TYPE type;
	int			node_id;
	char		address[256];
	int			port;
	char		cluster_id[256];
	/* Log operation fields */
	char		log_data[1024];		/* For log append/commit data */
	int			log_index;			/* For log operations */
	/* KV operation fields */
	char		kv_key[256];		/* For KV operations */
	char		kv_value[1024];		/* For KV operations */
	char		kv_client_id[64];	/* For KV operations */
	/* Status tracking */
	COMMAND_STATUS status;
	char		error_message[512]; /* Error message if failed */
	int64_t		timestamp;			/* Command timestamp */
}			pgraft_command_t;

/* Command queue configuration */
#define MAX_COMMANDS 100
#define MAX_APPLY_ENTRIES 1000

/* Apply entry structure for Raft log application */
typedef struct
{
	uint64		raft_index;			/* Raft log index */
	char		data[4096];			/* Entry data */
	size_t		data_len;			/* Data length */
	bool		applied;			/* Whether applied to PostgreSQL */
}			pgraft_apply_entry_t;

/* Background worker state structure */
typedef struct
{
	int			node_id;
	char		address[256];
	int			port;
	WORKER_STATUS status;

	/*
	 * Protects the command and status circular buffers and their
	 * head/tail/count indices below.  Client backends (producers) and the
	 * background worker (consumer) mutate these concurrently, so every
	 * head/tail/count update must be done while holding this spinlock.
	 *
	 * NOTE: this lock deliberately does NOT cover the apply queue further
	 * down; see the comment there.
	 */
	slock_t		mutex;

	/* Monotonic id source used to uniquely identify queued commands
	 * (replaces the old 1-second-granularity time(NULL) key, which collided
	 * for commands queued within the same second). */
	int64_t		next_command_id;

	/* Fixed-size circular buffer for commands */
	pgraft_command_t commands[MAX_COMMANDS];
	int			command_head;		/* Index of next command to process */
	int			command_tail;		/* Index of next slot to write */
	int			command_count;		/* Number of commands in queue */

	/* Fixed-size circular buffer for command status */
	pgraft_command_t status_commands[MAX_COMMANDS];
	int			status_head;		/* Index of next status to read */
	int			status_tail;		/* Index of next slot to write */
	int			status_count;		/* Number of status entries */

	/*
	 * Single-producer/single-consumer ring of Raft entries awaiting local
	 * application.
	 *
	 * The producer is the Go raft goroutine, which reaches this queue through
	 * pgraft_enqueue_for_apply_from_go().  That thread is NOT a PostgreSQL
	 * backend thread: it must never acquire a PostgreSQL lock, call elog(),
	 * or run ShmemInitStruct().  So this queue is deliberately NOT covered by
	 * "mutex" above.  Instead apply_write/apply_read are monotonically
	 * increasing atomic counters (slot = counter % MAX_APPLY_ENTRIES), written
	 * only by the producer and the consumer respectively, which makes the
	 * whole enqueue path lock-free and elog-free.
	 *
	 * Entries the producer had to discard are counted rather than logged; the
	 * background worker reports the counters from its own (safe) context.
	 */
	pgraft_apply_entry_t apply_queue[MAX_APPLY_ENTRIES];
	pg_atomic_uint64 apply_write;	/* entries ever enqueued (producer only) */
	pg_atomic_uint64 apply_read;	/* entries ever dequeued (consumer only) */
	pg_atomic_uint64 apply_dropped_full;	/* discarded: ring was full */
	pg_atomic_uint64 apply_dropped_size;	/* discarded: payload too large */
	pg_atomic_uint64 apply_dropped_detached;	/* discarded: no consumer yet */
	pg_atomic_uint64 last_applied_index;	/* last Raft index applied here */
}			pgraft_worker_state_t;

/* Maximum cluster members tracked in shared memory */
#define PGRAFT_MAX_NODES 16

/* Core consensus types */
typedef struct pgraft_node
{
	int32_t		id;
	char		address[256];
	int32_t		port;
	bool		is_leader;
}			pgraft_node_t;

typedef struct pgraft_cluster
{
	bool		initialized;	/* Whether the core system is initialized */
	int32_t		node_id;
	int32_t		current_term;
	int64_t		leader_id;
	char		state[32];		/* "leader", "follower", "candidate" */
	int32_t		num_nodes;
	pgraft_node_t nodes[16];
	
	/* Performance metrics */
	int64_t		messages_processed;
	int64_t		heartbeats_sent;
	int64_t		elections_triggered;
	
	/* Mutex for thread safety */
	slock_t		mutex;
}			pgraft_cluster_t;

/* Core consensus functions */
int			pgraft_core_init(int32_t node_id, const char *address, int32_t port);
int			pgraft_core_add_node(int32_t node_id, const char *address, int32_t port);
int			pgraft_core_remove_node(int32_t node_id);
int			pgraft_core_get_cluster_state(pgraft_cluster_t *cluster);
int			pgraft_core_update_cluster_state(int64_t leader_id, int64_t current_term, const char *state);
int			pgraft_core_update_nodes(int32_t num_nodes, int32_t *node_ids, char **addresses);
bool		pgraft_core_is_leader(void);
int64_t		pgraft_core_get_leader_id(void);
int32_t		pgraft_core_get_current_term(void);
void		pgraft_core_cleanup(void);

/* Shared memory functions */
void		pgraft_core_init_shared_memory(void);
pgraft_cluster_t *pgraft_core_get_shared_memory(void);

/* Background worker functions */
void		pgraft_worker_main(Datum main_arg);
void		pgraft_main(Datum main_arg);
void		pgraft_register_worker(void);

/* Worker control functions */
pgraft_worker_state_t *pgraft_worker_get_state(void);

/* Command queue functions */
bool		pgraft_queue_command(COMMAND_TYPE type, int node_id, const char *address, int port, const char *cluster_id);
bool		pgraft_queue_log_command(COMMAND_TYPE type, const char *log_data, int log_index);
bool		pgraft_queue_kv_command(COMMAND_TYPE type, const char *key, const char *value, const char *client_id);
bool		pgraft_dequeue_command(pgraft_command_t *cmd);
bool		pgraft_queue_is_empty(void);

/* Command status functions */
bool		pgraft_add_command_to_status(pgraft_command_t *cmd);
bool		pgraft_get_command_status(int64_t timestamp, pgraft_command_t *status_cmd);
bool		pgraft_update_command_status(int64_t timestamp, COMMAND_STATUS status, const char *error_message);
bool		pgraft_remove_completed_commands(void);

/* Apply queue functions (for Raft log replication) */
bool		pgraft_enqueue_apply_entry(uint64 raft_index, const char *data, size_t data_len);
bool		pgraft_dequeue_apply_entry(pgraft_apply_entry_t *entry);
bool		pgraft_apply_queue_is_empty(void);
int			pgraft_get_apply_queue_count(void);

/*
 * Publish the worker state to the lock-free producer path.  MUST be called by
 * the background worker (from its own process, on its own thread) before the
 * Go library is started, because pgraft_enqueue_apply_entry() may then be
 * called from a Go thread that cannot safely resolve shared memory itself.
 */
void		pgraft_apply_queue_attach(pgraft_worker_state_t *state);

/* Drain and report producer-side drop counters (worker context only). */
void		pgraft_apply_queue_report_drops(void);

/* Update cluster state from Go layer */
void		pgraft_update_shared_memory_from_go(void);

#endif
