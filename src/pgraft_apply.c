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
#include "pgraft_apply.h"
#include "pgraft_core.h"

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

/* Simple JSON-like parsing for log entries */
static PgRaftLogEntry *parse_json_entry(const char *data, size_t len);
static char *serialize_json_entry(PgRaftLogEntry *entry, size_t *out_len);

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

	/* Create a memory context for this operation */
	apply_context = AllocSetContextCreate(CurrentMemoryContext,
										  "PgRaft Apply Context",
										  ALLOCSET_DEFAULT_SIZES);
	oldcontext = MemoryContextSwitchTo(apply_context);

	/* Parse Raft entry */
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
	char	   *saveptr;
	int			field = 0;

	if (data == NULL || len == 0)
	{
		elog(WARNING, "pgraft: empty log entry data");
		return NULL;
	}

	/* Try JSON format first */
	if (data[0] == '{')
	{
		return parse_json_entry(data, len);
	}

	/* Allocate entry structure */
	entry = (PgRaftLogEntry *) palloc0(sizeof(PgRaftLogEntry));

	/* Make a copy of the data for parsing */
	copy = palloc(len + 1);
	memcpy(copy, data, len);
	copy[len] = '\0';

	/* Parse fields separated by '|' */
	token = strtok_r(copy, "|", &saveptr);
	while (token != NULL && field < 6)
	{
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
				strncpy(entry->database, token, sizeof(entry->database) - 1);
				break;
			case 4:				/* schema */
				strncpy(entry->schema, token, sizeof(entry->schema) - 1);
				break;
			case 5:				/* sql */
				strncpy(entry->sql, token, sizeof(entry->sql) - 1);
				break;
		}
		field++;
		token = strtok_r(NULL, "|", &saveptr);
	}

	pfree(copy);

	if (field < 6)
	{
		elog(WARNING, "pgraft: incomplete log entry (only %d fields)", field);
		pfree(entry);
		return NULL;
	}

	return entry;
}

/*
 * Simple JSON parser for log entries
 * Format: {"index":1,"term":1,"op":1,"db":"postgres","schema":"public","sql":"..."}
 */
static PgRaftLogEntry *
parse_json_entry(const char *data, size_t len)
{
	/* Very simple JSON parsing (for now, we'll use pipe-separated format) */
	/* TODO: Use proper JSON parsing library */

	elog(WARNING, "pgraft: JSON parsing not yet implemented, use pipe-separated format");
	return NULL;
}

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
	result = (char *) palloc(8192);

	/* Format: index|term|op|database|schema|sql */
	len = snprintf(result, 8192, "%lu|%lu|%d|%s|%s|%s",
				   (unsigned long) entry->index,
				   (unsigned long) entry->term,
				   entry->op,
				   entry->database,
				   entry->schema,
				   entry->sql);

	if (out_len)
		*out_len = len;

	return result;
}

/*
 * Record applied index in shared memory (for crash recovery)
 */
void
pgraft_record_applied_index(uint64 index)
{
	/* For now, just log it - we'll add proper shared memory tracking later */
	elog(DEBUG2, "pgraft: recorded applied index %lu", index);
}

/*
 * Get last applied index
 */
uint64
pgraft_get_applied_index(void)
{
	/* For now, return 0 - we'll add proper shared memory tracking later */
	return 0;
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
 * Note: KV operations (put/get/delete) are implemented in pgraft_kv.c
 * This module focuses on applying Raft log entries to PostgreSQL
 */

