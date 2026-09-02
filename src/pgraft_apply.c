/*
 * pgraft_apply.c
 * Raft log application to PostgreSQL
 *
 * This module applies committed Raft log entries to the local PostgreSQL
 * database, enabling 100% Raft-based replication (like etcd).
 *
 * Key features:
 * - Apply entries on ALL nodes (leader + followers)
 * - Use SPI to execute SQL on local PostgreSQL
 * - Track applied index for crash recovery
 * - No PostgreSQL streaming replication needed
 */

#include "postgres.h"
#include "../include/pgraft_apply.h"
#include "../include/pgraft_core.h"
#include "../include/pgraft_json.h"
#include "../include/pgraft_kv.h"

#include "executor/spi.h"
#include "utils/snapmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "storage/proc.h"
#include "access/xact.h"
#include "miscadmin.h"
#include "catalog/pg_type.h"

#include <string.h>
#include <stdlib.h>
/* Use separate JSON parsing module to avoid naming conflicts */

/* JSON parsing functions are now in pgraft_json.c */

/* Upper bound on a pipe-delimited serialized entry (see pgraft_serialize_log_entry). */
#define PGRAFT_SERIALIZED_ENTRY_MAX 8192

/*
 * Apply a committed Raft entry to local PostgreSQL
 * Called on BOTH leader and followers after Raft commits
 */
int
pgraft_apply_entry_to_postgres(uint64 raft_index, const char *data, size_t len)
{
	PgRaftLogEntry *entry;
	int			ret;
	bool		push_active_snap = false;
	MemoryContext oldcontext;
	MemoryContext apply_context;

	elog(LOG, "pgraft: applying raft entry %lu to PostgreSQL (len=%zu)", raft_index, len);

	/* Defensive: nothing to apply for empty/NULL payloads (e.g. Raft no-op
	 * entries committed on leader election).  Avoid dereferencing data[0]. */
	if (data == NULL || len == 0)
	{
		elog(DEBUG1, "pgraft: skipping empty raft entry %lu", raft_index);
		pgraft_record_applied_index(raft_index);
		return 0;
	}

	/* Create a memory context for this operation */
	apply_context = AllocSetContextCreate(CurrentMemoryContext,
										  "PgRaft Apply Context",
										  ALLOCSET_DEFAULT_SIZES);
	oldcontext = MemoryContextSwitchTo(apply_context);

	/* Check if this is a KV operation (JSON format) */
	if (data[0] == '{')
	{
		ret = pgraft_apply_kv_operation(raft_index, data, len);
		if (ret == 0)
		{
			/* Record that we applied this entry */
			pgraft_record_applied_index(raft_index);
		}
		
		/* Clean up memory context */
		MemoryContextSwitchTo(oldcontext);
		MemoryContextDelete(apply_context);
		return ret;
	}

	/* Parse Raft entry for SQL operations */
	entry = pgraft_parse_log_entry(data, len);
	if (entry == NULL)
	{
		elog(WARNING, "pgraft: failed to parse raft entry %lu", raft_index);
		MemoryContextSwitchTo(oldcontext);
		MemoryContextDelete(apply_context);
		return -1;
	}

	elog(DEBUG1, "pgraft: parsed entry %lu: op=%d, sql='%s'",
		 raft_index, entry->op, entry->sql);

	/* Set up snapshot if needed (for consistency) */
	if (!ActiveSnapshotSet())
	{
		PushActiveSnapshot(GetTransactionSnapshot());
		push_active_snap = true;
	}

	/* Connect to SPI */
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		elog(WARNING, "pgraft: SPI_connect failed for entry %lu", raft_index);
		if (push_active_snap)
			PopActiveSnapshot();
		MemoryContextSwitchTo(oldcontext);
		MemoryContextDelete(apply_context);
		return -1;
	}

	/* Execute the SQL from Raft log */
	ret = SPI_execute(entry->sql, false, 0);

	if (ret < 0)
	{
		elog(WARNING, "pgraft: SPI_execute failed for entry %lu: %s (ret=%d)",
			 raft_index, entry->sql, ret);
		SPI_finish();
		if (push_active_snap)
			PopActiveSnapshot();
		MemoryContextSwitchTo(oldcontext);
		MemoryContextDelete(apply_context);
		return -1;
	}

	/* Record that we applied this entry */
	pgraft_record_applied_index(raft_index);

	/* Disconnect SPI */
	SPI_finish();

	if (push_active_snap)
		PopActiveSnapshot();

	elog(LOG, "pgraft: successfully applied entry %lu: %s (rows=%lu)",
		 raft_index, entry->sql, (unsigned long) SPI_processed);

	/* Clean up memory context */
	MemoryContextSwitchTo(oldcontext);
	MemoryContextDelete(apply_context);

	return 0;
}

/*
 * Parse Raft log entry from serialized data
 * Simple format: index|term|op|database|schema|sql
 */
PgRaftLogEntry *
pgraft_parse_log_entry(const char *data, size_t len)
{
	PgRaftLogEntry *entry;
	char	   *copy;
	char	   *token;
	int			field;

	if (data == NULL || len == 0)
	{
		elog(WARNING, "pgraft: empty log entry data");
		return NULL;
	}

	/* Try JSON format first - use json-c library */
	if (data[0] == '{')
	{
		return pgraft_json_parse_log_entry(data, len);
	}

	/* Allocate entry structure */
	entry = (PgRaftLogEntry *) palloc0(sizeof(PgRaftLogEntry));

	/* Make a copy of the data for parsing */
	copy = palloc(len + 1);
	memcpy(copy, data, len);
	copy[len] = '\0';

	/*
	 * Split on '|' by hand rather than with strtok_r.
	 *
	 * strtok_r treats a run of delimiters as one, so an empty database or
	 * schema field silently shifted every later field left and the SQL ended
	 * up parsed as the schema.  It also stops at the first delimiter inside
	 * the SQL text; the last field therefore has to take the entire remainder
	 * of the buffer, separators included.
	 */
	token = copy;
	for (field = 0; field < 6; field++)
	{
		char	   *sep;

		if (field == 5)
		{
			/* Final field: everything that is left, '|' characters included. */
			strlcpy(entry->sql, token, sizeof(entry->sql));
			break;
		}

		sep = strchr(token, '|');
		if (sep == NULL)
		{
			pfree(copy);
			elog(WARNING, "pgraft: incomplete log entry (only %d of 6 fields)", field);
			pfree(entry);
			return NULL;
		}
		*sep = '\0';

		switch (field)
		{
			case 0:				/* index */
				entry->index = strtoull(token, NULL, 10);
				break;
			case 1:				/* term */
				entry->term = strtoull(token, NULL, 10);
				break;
			case 2:				/* op */
				entry->op = atoi(token);
				break;
			case 3:				/* database */
				strlcpy(entry->database, token, sizeof(entry->database));
				break;
			case 4:				/* schema */
				strlcpy(entry->schema, token, sizeof(entry->schema));
				break;
		}

		token = sep + 1;
	}

	pfree(copy);

	return entry;
}

/*
 * JSON parser for log entries using json-c library
 * Format: {"type": "kv_put", "key": "test", "value": "data", "timestamp": 123, "client_id": "pg_123"}
 */
/* Manual JSON parsing removed - using json-c library functions */

/*
 * Serialize Raft log entry for transmission
 * Format: index|term|op|database|schema|sql
 */
char *
pgraft_serialize_log_entry(PgRaftLogEntry *entry, size_t *out_len)
{
	char	   *result;
	int			len;

	if (entry == NULL)
	{
		if (out_len)
			*out_len = 0;
		return NULL;
	}

	/* Allocate buffer for serialized entry */
	result = (char *) palloc(PGRAFT_SERIALIZED_ENTRY_MAX);

	/* Format: index|term|op|database|schema|sql */
	len = snprintf(result, PGRAFT_SERIALIZED_ENTRY_MAX,
				   UINT64_FORMAT "|" UINT64_FORMAT "|%d|%s|%s|%s",
				   entry->index,
				   entry->term,
				   entry->op,
				   entry->database,
				   entry->schema,
				   entry->sql);

	/*
	 * snprintf reports what it *would* have written, so clamp before handing
	 * the length back; otherwise a truncated entry reports a length past the
	 * end of the buffer.
	 */
	if (len < 0)
	{
		pfree(result);
		if (out_len)
			*out_len = 0;
		return NULL;
	}
	if (len >= PGRAFT_SERIALIZED_ENTRY_MAX)
	{
		elog(WARNING, "pgraft: serialized log entry truncated (%d bytes needed, %d available)",
			 len, PGRAFT_SERIALIZED_ENTRY_MAX);
		len = PGRAFT_SERIALIZED_ENTRY_MAX - 1;
	}

	if (out_len)
		*out_len = (size_t) len;

	return result;
}

/*
 * Record applied index in shared memory (for crash recovery)
 */
void
pgraft_record_applied_index(uint64 index)
{
	pgraft_worker_state_t *worker_state;

	worker_state = pgraft_worker_get_state();
	if (!worker_state) {
		elog(WARNING, "pgraft: failed to get worker state for recording applied index");
		return;
	}

	/*
	 * Plain atomic store: the field is not covered by the worker-state
	 * spinlock (which guards the command/status queues), and a single 64-bit
	 * counter needs no lock of its own.
	 */
	pg_atomic_write_u64(&worker_state->last_applied_index, index);

	elog(DEBUG2, "pgraft: recorded applied index " UINT64_FORMAT " in shared memory", index);
}

/*
 * Get last applied index
 */
uint64
pgraft_get_applied_index(void)
{
	pgraft_worker_state_t *worker_state;
	uint64 last_applied;

	worker_state = pgraft_worker_get_state();
	if (!worker_state) {
		elog(WARNING, "pgraft: failed to get worker state for getting applied index");
		return 0;
	}

	last_applied = pg_atomic_read_u64(&worker_state->last_applied_index);

	return last_applied;
}

/*
 * Initialize application layer
 */
void
pgraft_apply_init(void)
{
	elog(LOG, "pgraft: initializing application layer");
	/* Nothing to do for now */
}

/*
 * Cleanup application layer
 */
void
pgraft_apply_shutdown(void)
{
	elog(LOG, "pgraft: shutting down application layer");
	/* Nothing to do for now */
}

/*
 * Apply KV operation from JSON data
 */
int
pgraft_apply_kv_operation(uint64 raft_index, const char *json_data, size_t len)
{
	char *key = NULL;
	char *value = NULL;
	int op_type = -1;
	int result = 0;
	
	elog(LOG, "pgraft: applying KV operation from JSON at index %lu", raft_index);
	
	/* Parse JSON to extract operation details using json-c */
	if (pgraft_json_parse_kv_operation(json_data, len, &op_type, &key, &value) != 0) {
		elog(WARNING, "pgraft: failed to parse KV operation JSON");
		return -1;
	}
	
	/*
	 * Apply the operation to the local KV store.
	 *
	 * Failures are reported as WARNING plus a -1 return, never as ERROR: this
	 * runs inside the background worker's apply loop, and an ERROR unwinds out
	 * of it.  (It also made the `result = -1; break;` lines below unreachable
	 * and leaked the key/value strings.)
	 */
	switch (op_type) {
		case PGRAFT_KV_PUT:
			if (!key || !value) {
				elog(WARNING, "pgraft: invalid PUT operation in raft entry " UINT64_FORMAT
					 " (key %s, value %s)", raft_index,
					 key ? "present" : "missing", value ? "present" : "missing");
				result = -1;
				break;
			}
			result = pgraft_kv_put_local(key, value, (int64_t) raft_index);
			if (result == 0)
				elog(DEBUG1, "pgraft: applied KV PUT for key '%s'", key);
			else
				elog(WARNING, "pgraft: failed to apply KV PUT for key '%s' (error %d)",
					 key, result);
			break;
		case PGRAFT_KV_DELETE:
			if (!key) {
				elog(WARNING, "pgraft: invalid DELETE operation in raft entry " UINT64_FORMAT
					 " (missing key)", raft_index);
				result = -1;
				break;
			}
			result = pgraft_kv_delete_local(key, (int64_t) raft_index);
			if (result == 0)
				elog(DEBUG1, "pgraft: applied KV DELETE for key '%s'", key);
			else
				elog(WARNING, "pgraft: failed to apply KV DELETE for key '%s' (error %d)",
					 key, result);
			break;
		default:
			elog(WARNING, "pgraft: unsupported KV operation type %d in raft entry "
				 UINT64_FORMAT, op_type, raft_index);
			result = -1;
			break;
	}
	
	/* Clean up allocated strings */
	if (key) pfree(key);
	if (value) pfree(value);
	
	return result;
}

/*
 * Note: KV operations (put/get/delete) are implemented in pgraft_kv.c
 * This module focuses on applying Raft log entries to PostgreSQL
 */

