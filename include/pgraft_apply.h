/*
 * pgraft_apply.h
 * Header for Raft log application to PostgreSQL
 *
 * This module handles applying committed Raft log entries to the local
 * PostgreSQL database on all nodes (leader and followers).
 */

#ifndef PGRAFT_APPLY_H
#define PGRAFT_APPLY_H

#include "postgres.h"
#include "c.h"

/* Operation types for Raft log entries */
typedef enum
{
	PGRAFT_OP_UNKNOWN = 0,
	PGRAFT_OP_INSERT = 1,
	PGRAFT_OP_UPDATE = 2,
	PGRAFT_OP_DELETE = 3,
	PGRAFT_OP_DDL = 4,
	PGRAFT_OP_KV_PUT = 5,		/* Key-value put (etcd-style) */
	PGRAFT_OP_KV_DELETE = 6		/* Key-value delete (etcd-style) */
}			PgRaftOperationType;

/* Raft log entry structure */
typedef struct PgRaftLogEntry
{
	uint64		index;			/* Raft log index */
	uint64		term;			/* Raft term */
	PgRaftOperationType op;		/* Operation type */
	char		database[64];	/* Target database */
	char		schema[64];		/* Target schema */
	char		sql[4096];		/* SQL statement or serialized data */
}			PgRaftLogEntry;

/* Key-value entry (etcd-compatible) */
typedef struct PgRaftKVEntry
{
	char		key[256];
	char		value[4096];
	uint64		version;		/* For MVCC */
}			PgRaftKVEntry;

/* Function declarations */

/*
 * Apply a committed Raft entry to local PostgreSQL
 * Called on BOTH leader and followers
 */
extern int	pgraft_apply_entry_to_postgres(uint64 raft_index, const char *data, size_t len);

/*
 * Parse Raft log entry from serialized data
 */
extern PgRaftLogEntry *pgraft_parse_log_entry(const char *data, size_t len);

/*
 * Serialize Raft log entry for transmission
 */
extern char *pgraft_serialize_log_entry(PgRaftLogEntry *entry, size_t *out_len);

/*
 * Record applied index in shared memory (for crash recovery)
 */
extern void pgraft_record_applied_index(uint64 index);

/*
 * Get last applied index from shared memory
 */
extern uint64 pgraft_get_applied_index(void);

/*
 * Initialize application layer
 */
extern void pgraft_apply_init(void);

/*
 * Cleanup application layer
 */
extern void pgraft_apply_shutdown(void);

/*
 * Apply KV operation from JSON data
 */
extern int pgraft_apply_kv_operation(uint64 raft_index, const char *json_data, size_t len);

/*
 * Callback from Go layer to enqueue entry for application
 * This is called by the Go Raft implementation when an entry is committed
 */
extern int pgraft_enqueue_for_apply_from_go(unsigned long long raft_index, const char *data, unsigned long data_len);

/*
 * Note: Key-value operations (put/get/delete) are declared in pgraft_kv.h
 */

#endif							/* PGRAFT_APPLY_H */

