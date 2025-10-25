/*-------------------------------------------------------------------------
 *
 * pgraft_kv.c
 *      Key/Value storage engine for pgraft with etcd-like interface
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "fmgr.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "../include/pgraft_go.h"
#include "storage/shmem.h"
#include "storage/spin.h"
#include "utils/elog.h"
#include "utils/timestamp.h"
#include "port/pg_crc32c.h"

#include "../include/pgraft_kv.h"
#include "../include/pgraft_core.h"
#include "../include/pgraft_json.h"

/*
 * Replicate PUT operation through Raft
 */
int
pgraft_kv_replicate_put(const char *key, const char *value, const char *client_id)
{
	char json_data[2048];
	int result;
	
	/* Create JSON using json-c library */
	if (pgraft_json_create_kv_operation(PGRAFT_KV_PUT, key, value, client_id, json_data, sizeof(json_data)) != 0) {
		elog(ERROR, "pgraft_kv: failed to create JSON for PUT operation");
		return -1;
	}
	
	elog(INFO, "pgraft_kv: replicating PUT operation: %s", json_data);
	
	/* Queue the operation for the background worker to process through Raft */
	result = pgraft_kv_queue_operation(PGRAFT_KV_PUT, key, value, client_id);
	if (result < 0)
	{
		elog(ERROR, "pgraft_kv: failed to queue operation for replication. Operation rejected to prevent split-brain.");
		return -1;
	}
	
	return result;
}

/*
 * Replicate DELETE operation through Raft
 */
int
pgraft_kv_replicate_delete(const char *key, const char *client_id)
{
	char json_data[2048];
	int result;
	
	/* Create JSON using json-c library */
	if (pgraft_json_create_kv_operation(PGRAFT_KV_DELETE, key, NULL, client_id, json_data, sizeof(json_data)) != 0) {
		elog(ERROR, "pgraft_kv: failed to create JSON for DELETE operation");
		return -1;
	}
	
	elog(INFO, "pgraft_kv: replicating DELETE operation: %s", json_data);
	
	/* Queue the operation for the background worker to process through Raft */
	result = pgraft_kv_queue_operation(PGRAFT_KV_DELETE, key, NULL, client_id);
	if (result < 0)
	{
		elog(ERROR, "pgraft_kv: failed to queue DELETE operation for replication. Operation rejected to prevent split-brain.");
		return -1;
	}
	
	return result;
}

/*
 * Apply log entry to key/value store
 */
int
pgraft_kv_apply_log_entry(const pgraft_kv_log_entry_t *log_entry, int64_t log_index)
{
	int result = 0;
	
	if (!log_entry) {
		elog(WARNING, "pgraft_kv: null log entry provided");
		return -1;
	}
	
	switch (log_entry->op_type) {
		case PGRAFT_KV_PUT:
			result = pgraft_kv_put(log_entry->key, log_entry->value, log_index);
			break;
		case PGRAFT_KV_DELETE:
			result = pgraft_kv_delete(log_entry->key, log_index);
			break;
		default:
			elog(WARNING, "pgraft_kv: unknown operation type: %d", log_entry->op_type);
			result = -1;
			break;
	}
	
	return result;
}

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "../include/pgraft_kv.h"

/* Global shared memory pointer */
static pgraft_kv_store_t *g_kv_store = NULL;

/* Persistence file path */
#define PGRAFT_KV_PERSIST_FILE "/tmp/pgraft_kv_store.dat"

/*
 * Initialize shared memory for key/value store
 */
void
pgraft_kv_init_shared_memory(void)
{
	bool		found;
	
	elog(INFO, "pgraft: initializing key/value store shared memory");
	
	/* Allocate shared memory */
	g_kv_store = (pgraft_kv_store_t *) ShmemInitStruct("pgraft_kv_store",
														sizeof(pgraft_kv_store_t),
														&found);
	
	if (!found)
	{
		elog(INFO, "pgraft: creating new key/value store shared memory");
		
		/* Initialize shared memory */
		memset(g_kv_store, 0, sizeof(pgraft_kv_store_t));
		
		/* Initialize mutex */
		SpinLockInit(&g_kv_store->mutex);
		
		/* Initialize default values */
		g_kv_store->num_entries = 0;
		g_kv_store->total_operations = 0;
		g_kv_store->last_applied_index = 0;
		g_kv_store->puts = 0;
		g_kv_store->deletes = 0;
		g_kv_store->gets = 0;
		
		elog(INFO, "pgraft: key/value store initialized");
		
		/* Try to load existing data from disk */
		if (pgraft_kv_load_from_disk(PGRAFT_KV_PERSIST_FILE) == 0)
		{
			elog(INFO, "pgraft: loaded existing key/value data from disk");
		}
	}
	else
	{
		elog(INFO, "pgraft: using existing key/value store shared memory");
	}
}

/*
 * Get key/value store shared memory
 */
pgraft_kv_store_t *
pgraft_kv_get_store(void)
{
	return g_kv_store;
}

/*
 * Find entry index by key
 */
static int
pgraft_kv_find_entry_index(const char *key)
{
	int i;
	
	if (!g_kv_store || !key)
		return -1;
	
	for (i = 0; i < g_kv_store->num_entries; i++)
	{
		if (!g_kv_store->entries[i].deleted && 
			strcmp(g_kv_store->entries[i].key, key) == 0)
		{
			return i;
		}
	}
	
	return -1;
}

/*
 * PUT operation - store or update a key/value pair
 */
int
pgraft_kv_put(const char *key, const char *value, int64_t log_index)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	pgraft_kv_entry_t *entry;
	int entry_index;
	int64_t timestamp = GetCurrentTimestamp();
	
	if (!store || !key || !value)
	{
		elog(ERROR, "pgraft_kv: invalid parameters for PUT operation (store=%p, key=%p, value=%p)", store, key, value);
		return -1;
	}
	
	if (strlen(key) >= 256)
	{
		elog(ERROR, "pgraft_kv: key too long (max 255 characters, got %zu)", strlen(key));
		return -1;
	}
	
	if (strlen(value) >= 1024)
	{
		elog(ERROR, "pgraft_kv: value too long (max 1023 characters, got %zu)", strlen(value));
		return -1;
	}
	
	SpinLockAcquire(&store->mutex);
	
	/* Check if key already exists */
	entry_index = pgraft_kv_find_entry_index(key);
	
	if (entry_index >= 0)
	{
		/* Update existing entry */
		entry = &store->entries[entry_index];
		strncpy(entry->value, value, sizeof(entry->value) - 1);
		entry->value[sizeof(entry->value) - 1] = '\0';
		entry->version++;
		entry->updated_at = timestamp;
		entry->log_index = log_index;
		entry->deleted = false;
		
		elog(DEBUG1, "pgraft_kv: Updated key '%s' (version %lld)", key, entry->version);
	}
	else
	{
		/* Create new entry */
		if (store->num_entries >= 1000)
		{
			SpinLockRelease(&store->mutex);
			elog(ERROR, "pgraft_kv: key/value store is full (1000 entries)");
			return -1;
		}
		
		entry = &store->entries[store->num_entries];
		strncpy(entry->key, key, sizeof(entry->key) - 1);
		entry->key[sizeof(entry->key) - 1] = '\0';
		strncpy(entry->value, value, sizeof(entry->value) - 1);
		entry->value[sizeof(entry->value) - 1] = '\0';
		entry->version = 1;
		entry->created_at = timestamp;
		entry->updated_at = timestamp;
		entry->log_index = log_index;
		entry->deleted = false;
		
		store->num_entries++;
		
		elog(DEBUG1, "pgraft_kv: Created new key '%s'", key);
	}
	
	store->puts++;
	store->total_operations++;
	store->last_applied_index = log_index;
	
	SpinLockRelease(&store->mutex);
	
	/* Persist to disk */
	pgraft_kv_save_to_disk(PGRAFT_KV_PERSIST_FILE);
	
	return 0;
}

/*
 * GET operation - retrieve value for a key
 */
int
pgraft_kv_get(const char *key, char *value, size_t value_size, int64_t *version)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	pgraft_kv_entry_t *entry;
	int entry_index;
	
	if (!store || !key || !value)
	{
		elog(ERROR, "pgraft_kv: invalid parameters for GET operation");
		return -1;
	}
	
	SpinLockAcquire(&store->mutex);
	
	entry_index = pgraft_kv_find_entry_index(key);
	
	if (entry_index < 0)
	{
		SpinLockRelease(&store->mutex);
		elog(DEBUG1, "pgraft_kv: Key '%s' not found", key);
		return -1;  /* Key not found */
	}
	
	entry = &store->entries[entry_index];
	
	/* Copy value */
	strncpy(value, entry->value, value_size - 1);
	value[value_size - 1] = '\0';
	
	if (version)
		*version = entry->version;
	
	store->gets++;
	store->total_operations++;
	
	SpinLockRelease(&store->mutex);
	
	elog(DEBUG1, "pgraft_kv: Retrieved key '%s' (version %lld)", key, entry->version);
	
	return 0;  /* Success */
}

/*
 * DELETE operation - mark a key as deleted
 */
int
pgraft_kv_delete(const char *key, int64_t log_index)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	pgraft_kv_entry_t *entry;
	int entry_index;
	
	if (!store || !key)
	{
		elog(ERROR, "pgraft_kv: invalid parameters for DELETE operation");
		return -1;
	}
	
	SpinLockAcquire(&store->mutex);
	
	entry_index = pgraft_kv_find_entry_index(key);
	
	if (entry_index < 0)
	{
		SpinLockRelease(&store->mutex);
		elog(DEBUG1, "pgraft_kv: Key '%s' not found for deletion", key);
		return -1;  /* Key not found */
	}
	
	entry = &store->entries[entry_index];
	entry->deleted = true;
	entry->updated_at = GetCurrentTimestamp();
	entry->log_index = log_index;
	entry->version++;
	
	store->deletes++;
	store->total_operations++;
	store->last_applied_index = log_index;
	
	SpinLockRelease(&store->mutex);
	
	/* Persist to disk */
	pgraft_kv_save_to_disk(PGRAFT_KV_PERSIST_FILE);
	
	elog(DEBUG1, "pgraft_kv: Deleted key '%s'", key);
	
	return 0;
}

/*
 * Check if key exists
 */
bool
pgraft_kv_exists(const char *key)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	bool exists;
	
	if (!store || !key)
		return false;
	
	SpinLockAcquire(&store->mutex);
	exists = (pgraft_kv_find_entry_index(key) >= 0);
	SpinLockRelease(&store->mutex);
	
	return exists;
}

/*
 * Get statistics
 */
int
pgraft_kv_get_stats(pgraft_kv_store_t *stats)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	
	if (!store || !stats)
		return -1;
	
	SpinLockAcquire(&store->mutex);
	memcpy(stats, store, sizeof(pgraft_kv_store_t));
	SpinLockRelease(&store->mutex);
	
	return 0;
}

/*
 * Save key/value store to disk for persistence
 */
int
pgraft_kv_save_to_disk(const char *path)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	FILE *file;
	size_t written;
	
	if (!store || !path)
		return -1;
	
	file = fopen(path, "wb");
	if (!file)
	{
		elog(WARNING, "pgraft_kv: failed to open file for writing: %s", path);
		return -1;
	}
	
	SpinLockAcquire(&store->mutex);
	
	/* Write the entire store structure */
	written = fwrite(store, sizeof(pgraft_kv_store_t), 1, file);
	
	SpinLockRelease(&store->mutex);
	
	fclose(file);
	
	if (written != 1)
	{
		elog(WARNING, "pgraft_kv: failed to write store to disk");
		return -1;
	}
	
	elog(DEBUG1, "pgraft_kv: Saved store to disk (%d entries)", store->num_entries);
	return 0;
}

/*
 * Load key/value store from disk
 */
int
pgraft_kv_load_from_disk(const char *path)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	pgraft_kv_store_t temp_store;
	FILE *file;
	size_t read_size;
	
	if (!store || !path)
		return -1;
	
	/* Check if file exists */
	file = fopen(path, "rb");
	if (!file)
	{
		elog(DEBUG1, "pgraft_kv: No existing store file found at %s", path);
		return -1;
	}
	
	/* Read into temporary structure first */
	read_size = fread(&temp_store, sizeof(pgraft_kv_store_t), 1, file);
	fclose(file);
	
	if (read_size != 1)
	{
		elog(WARNING, "pgraft_kv: failed to read store from disk");
		return -1;
	}
	
	SpinLockAcquire(&store->mutex);
	
	/* Copy data except the mutex */
	memcpy(store->entries, temp_store.entries, sizeof(store->entries));
	store->num_entries = temp_store.num_entries;
	store->total_operations = temp_store.total_operations;
	store->last_applied_index = temp_store.last_applied_index;
	store->puts = temp_store.puts;
	store->deletes = temp_store.deletes;
	store->gets = temp_store.gets;
	
	SpinLockRelease(&store->mutex);
	
	elog(INFO, "pgraft_kv: loaded store from disk (%d entries)", store->num_entries);
	return 0;
}

/*
 * List all keys as JSON
 */
void
pgraft_kv_list_keys(char *keys_json, size_t json_size)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	
	if (!store || !keys_json || json_size < 3)
		return;
	
	/* Use json-c library to create JSON array of keys */
	if (pgraft_json_create_key_list(store, keys_json, json_size) != 0) {
		elog(WARNING, "pgraft_kv: failed to create JSON key list");
		strcpy(keys_json, "[]"); /* Fallback to empty array */
	}
}

/*
 * Compact the store by removing deleted entries
 */
void
pgraft_kv_compact(void)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	int i, j = 0;
	
	if (!store)
		return;
	
	SpinLockAcquire(&store->mutex);
	
	/* Move non-deleted entries to the beginning */
	for (i = 0; i < store->num_entries; i++)
	{
		if (!store->entries[i].deleted)
		{
			if (i != j)
			{
				memcpy(&store->entries[j], &store->entries[i], sizeof(pgraft_kv_entry_t));
			}
			j++;
		}
	}
	
	/* Clear the rest */
	for (i = j; i < store->num_entries; i++)
	{
		memset(&store->entries[i], 0, sizeof(pgraft_kv_entry_t));
	}
	
	store->num_entries = j;
	
	SpinLockRelease(&store->mutex);
	
	/* Persist the compacted store */
	pgraft_kv_save_to_disk(PGRAFT_KV_PERSIST_FILE);
	
	elog(INFO, "pgraft_kv: compacted store to %d active entries", j);
}

/*
 * Reset the store
 */
void
pgraft_kv_reset(void)
{
	pgraft_kv_store_t *store = pgraft_kv_get_store();
	
	if (!store)
		return;
	
	SpinLockAcquire(&store->mutex);
	
	memset(store->entries, 0, sizeof(store->entries));
	store->num_entries = 0;
	store->total_operations = 0;
	store->last_applied_index = 0;
	store->puts = 0;
	store->deletes = 0;
	store->gets = 0;
	
	SpinLockRelease(&store->mutex);
	
	/* Remove persistence file */
	unlink(PGRAFT_KV_PERSIST_FILE);
	
	elog(INFO, "pgraft_kv: store reset");
}

/*
 * Queue KV operation for background worker to process through Raft
 */
int
pgraft_kv_queue_operation(pgraft_kv_op_type_t op_type, const char *key, const char *value, const char *client_id)
{
	pgraft_cluster_t *cluster_state;
	bool is_leader = false;
	COMMAND_TYPE cmd_type;
	bool queued = false;
	
	/* Get cluster state from shared memory */
	cluster_state = pgraft_core_get_shared_memory();
	if (!cluster_state) {
		elog(ERROR, "pgraft_kv: cannot access cluster state");
		return -1;
	}
	
	/* Refresh cluster state from Go layer before checking leader status */
	pgraft_update_shared_memory_from_go();
	
	/* Check if current node is leader */
	is_leader = (cluster_state->node_id == cluster_state->leader_id);
	
	if (!is_leader) {
		elog(ERROR, "pgraft_kv: write operations only allowed on leader node (current leader: %lld)", (long long)cluster_state->leader_id);
		return -1;
	}
	
	/* Convert operation type to command type */
	if (op_type == PGRAFT_KV_PUT) {
		cmd_type = COMMAND_KV_PUT;
	} else if (op_type == PGRAFT_KV_DELETE) {
		cmd_type = COMMAND_KV_DELETE;
	} else {
		elog(ERROR, "pgraft_kv: unsupported operation type: %d", op_type);
		return -1;
	}
	
	/* Queue the operation for the background worker to process through Raft */
	queued = pgraft_queue_kv_command(cmd_type, key, value, client_id);
	if (!queued) {
		elog(ERROR, "pgraft_kv: failed to queue operation for Raft replication");
		return -1;
	}
	
	elog(INFO, "pgraft_kv: operation queued for Raft replication (type=%d, key=%s)", op_type, key);
	return 0;
}

/*
 * Local KV operations (called directly without Raft replication)
 * These are used by the apply callback to apply replicated operations
 */
int
pgraft_kv_put_local(const char *key, const char *value)
{
	return pgraft_kv_put(key, value, 0); /* Skip replication flag */
}

int
pgraft_kv_delete_local(const char *key)
{
	return pgraft_kv_delete(key, 0); /* Skip replication flag */
}