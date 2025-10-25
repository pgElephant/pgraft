#ifndef PGRAFT_LOG_H
#define PGRAFT_LOG_H

#include "postgres.h"
#include "storage/shmem.h"
#include "storage/spin.h"

/* Log entry structure */
typedef struct pgraft_log_entry
{
	int64_t		index;			/* Log index */
	int64_t		term;			/* Term when entry was created */
	int64_t		timestamp;		/* Timestamp when entry was created */
	char		data[1024];		/* Log entry data */
	int32_t		data_size;		/* Size of data */
	int32_t		committed;		/* 1 if committed, 0 if not */
	int32_t		applied;		/* 1 if applied, 0 if not */
}			pgraft_log_entry_t;

/* Log replication state */
typedef struct pgraft_log_state
{
	/* Log entries array */
	pgraft_log_entry_t entries[1000];	/* Support up to 1000 log entries */
	int32_t		log_size;		/* Current number of log entries */
	int64_t		last_index;		/* Last log index */
	int64_t		commit_index;	/* Last committed index */
	int64_t		last_applied;	/* Last applied index */
	
	/* Replication metrics */
	int64_t		entries_replicated;	/* Number of entries replicated */
	int64_t		entries_committed;	/* Number of entries committed */
	int64_t		entries_applied;	/* Number of entries applied */
	int64_t		replication_errors;	/* Number of replication errors */
	
	/* Mutex for thread safety */
	slock_t		mutex;
}			pgraft_log_state_t;

/* Log replication functions */
void		pgraft_log_init_shared_memory(void);
pgraft_log_state_t *pgraft_log_get_shared_memory(void);

/* Log entry management */
int			pgraft_log_append_entry(int64_t term, const char *data, int32_t data_size);
int			pgraft_log_commit_entry(int64_t index);
int			pgraft_log_apply_entry(int64_t index);
int			pgraft_log_get_entry(int64_t index, pgraft_log_entry_t *entry);
int			pgraft_log_get_last_index(int64_t *last_index);
int			pgraft_log_get_commit_index(int64_t *commit_index);
int			pgraft_log_get_last_applied(int64_t *last_applied);

/* Log replication */
int			pgraft_log_replicate_to_node(int32_t node_id, int64_t from_index);
int			pgraft_log_replicate_from_leader(int32_t leader_id, int64_t from_index);
int			pgraft_log_sync_with_leader(void);

/* Log statistics */
int			pgraft_log_get_statistics(pgraft_log_state_t *stats);
int			pgraft_log_get_replication_status(char *status, size_t status_size);

/* Log cleanup */
void		pgraft_log_cleanup_old_entries(int64_t before_index);
void		pgraft_log_reset(void);

#endif
