#ifndef PGRAFT_KV_H
#define PGRAFT_KV_H

#include "postgres.h"
#include "storage/shmem.h"
#include "storage/spin.h"

/* Key/Value operation types */
typedef enum pgraft_kv_op_type
{
	PGRAFT_KV_PUT = 1,
	PGRAFT_KV_DELETE = 2,
	PGRAFT_KV_GET = 3
} pgraft_kv_op_type_t;

/* Key/Value entry structure */
typedef struct pgraft_kv_entry
{
	char		key[256];		/* Key (max 255 chars + null terminator) */
	char		value[1024];	/* Value (max 1023 chars + null terminator) */
	int64_t		version;		/* Version number for this key */
	int64_t		created_at;		/* Creation timestamp */
	int64_t		updated_at;		/* Last update timestamp */
	int64_t		log_index;		/* Raft log index that created/modified this entry */
	bool		deleted;		/* True if this entry is deleted */
} pgraft_kv_entry_t;

/* Key/Value store state */
typedef struct pgraft_kv_store
{
	/* Key/Value entries hash table (simple array for now) */
	pgraft_kv_entry_t entries[1000];	/* Support up to 1000 key/value pairs */
	int32_t		num_entries;			/* Current number of active entries */
	int64_t		total_operations;		/* Total number of operations performed */
	int64_t		last_applied_index;		/* Last Raft log index applied to store */
	
	/* Statistics */
	int64_t		puts;				/* Number of PUT operations */
	int64_t		deletes;			/* Number of DELETE operations */
	int64_t		gets;				/* Number of GET operations */
	
	/* Mutex for thread safety */
	slock_t		mutex;
} pgraft_kv_store_t;

/* Log entry for key/value operations */
typedef struct pgraft_kv_log_entry
{
	pgraft_kv_op_type_t op_type;	/* Operation type (PUT/DELETE) */
	char		key[256];			/* Key */
	char		value[1024];		/* Value (empty for DELETE) */
	int64_t		timestamp;			/* Operation timestamp */
	char		client_id[64];		/* Client identifier */
} pgraft_kv_log_entry_t;

/* Key/Value store functions */
void		pgraft_kv_init_shared_memory(void);
pgraft_kv_store_t *pgraft_kv_get_store(void);

/* Key/Value operations */
int			pgraft_kv_put(const char *key, const char *value, int64_t log_index);
int			pgraft_kv_get(const char *key, char *value, size_t value_size, int64_t *version);
int			pgraft_kv_delete(const char *key, int64_t log_index);
bool		pgraft_kv_exists(const char *key);

/* Key/Value replication (through Raft) */
int			pgraft_kv_replicate_put(const char *key, const char *value, const char *client_id);
int			pgraft_kv_replicate_delete(const char *key, const char *client_id);

/* Key/Value log application */
int			pgraft_kv_apply_log_entry(const pgraft_kv_log_entry_t *log_entry, int64_t log_index);

/* Key/Value persistence */
int			pgraft_kv_save_to_disk(const char *path);
int			pgraft_kv_load_from_disk(const char *path);

/* Key/Value statistics and monitoring */
int			pgraft_kv_get_stats(pgraft_kv_store_t *stats);
void		pgraft_kv_list_keys(char *keys_json, size_t json_size);

/* Key/Value cleanup and maintenance */
void		pgraft_kv_compact(void);
void		pgraft_kv_reset(void);

/* Key/Value replication operations */
int			pgraft_kv_replicate_put(const char *key, const char *value, const char *client_id);
int			pgraft_kv_replicate_delete(const char *key, const char *client_id);
int			pgraft_kv_queue_operation(pgraft_kv_op_type_t op_type, const char *key, const char *value, const char *client_id);

/* Local KV operations (for apply callback) */
int			pgraft_kv_put_local(const char *key, const char *value);
int			pgraft_kv_delete_local(const char *key);

#endif