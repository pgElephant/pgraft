/*
 * PostgreSQL utility functions for pgraft
 * Copyright (c) 2024-2025, pgElephant, Inc.
 */

#include "postgres.h"
#include "fmgr.h"
#include "utils/palloc.h"
#include "lib/ilist.h"
#include "nodes/pg_list.h"
#include "../include/pgraft_core.h"

#include <time.h>

/*
 * Add command to queue (called by SQL functions)
 */
bool
pgraft_queue_command(COMMAND_TYPE type, int node_id, const char *address, int port, const char *cluster_id)
{
	pgraft_worker_state_t *state;
	pgraft_command_t *cmd;
	int			command_count;

	elog(LOG, "pgraft: pgraft_queue_command called with type=%d, node_id=%d, address=%s, port=%d",
		 type, node_id, address ? address : "NULL", port);
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		elog(ERROR, "pgraft: failed to get worker state in pgraft_queue_command");
		return false;
	}

	SpinLockAcquire(&state->mutex);

	/* Check if queue is full */
	if (state->command_count >= MAX_COMMANDS) {
		SpinLockRelease(&state->mutex);
		elog(WARNING, "pgraft: command queue is full, cannot queue new command");
		return false;
	}

	/* Get pointer to next slot in circular buffer */
	cmd = &state->commands[state->command_tail];

	/* Initialize command */
	cmd->type = type;
	cmd->node_id = node_id;
	if (address) {
		strncpy(cmd->address, address, sizeof(cmd->address) - 1);
		cmd->address[sizeof(cmd->address) - 1] = '\0';
	} else {
		cmd->address[0] = '\0';
	}
	cmd->port = port;

	if (cluster_id) {
		strncpy(cmd->cluster_id, cluster_id, sizeof(cmd->cluster_id) - 1);
		cmd->cluster_id[sizeof(cmd->cluster_id) - 1] = '\0';
	} else {
		cmd->cluster_id[0] = '\0';
	}

	/* Initialize status tracking (unique, collision-free command id) */
	cmd->status = COMMAND_STATUS_PENDING;
	cmd->error_message[0] = '\0';
	cmd->timestamp = state->next_command_id++;

	/* Update circular buffer pointers */
	state->command_tail = (state->command_tail + 1) % MAX_COMMANDS;
	command_count = ++state->command_count;

	SpinLockRelease(&state->mutex);

	elog(LOG, "pgraft: command %d queued for node %d at %s:%d (count=%d)",
		 type, node_id, address ? address : "NULL", port, command_count);
	return true;
}

/*
 * Remove command from queue (called by worker)
 * Returns true if command was dequeued, false if queue is empty
 */
bool
pgraft_dequeue_command(pgraft_command_t *cmd)
{
	pgraft_worker_state_t *state;
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}

	SpinLockAcquire(&state->mutex);

	/* Check if queue is empty */
	if (state->command_count == 0) {
		SpinLockRelease(&state->mutex);
		return false;
	}

	/* Copy command from head of circular buffer */
	*cmd = state->commands[state->command_head];

	/* Update circular buffer pointers */
	state->command_head = (state->command_head + 1) % MAX_COMMANDS;
	state->command_count--;

	SpinLockRelease(&state->mutex);

	return true;
}

/*
 * Check if command queue is empty
 */
bool
pgraft_queue_is_empty(void)
{
	pgraft_worker_state_t *state;
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return true;
	}
	
	return (state->command_count == 0);
}

/*
 * Add log command to queue (called by SQL log functions)
 */
bool
pgraft_queue_log_command(COMMAND_TYPE type, const char *log_data, int log_index)
{
	pgraft_worker_state_t *state;
	pgraft_command_t *cmd;
	int			command_count;

	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}

	SpinLockAcquire(&state->mutex);

	/* Check if queue is full */
	if (state->command_count >= MAX_COMMANDS) {
		SpinLockRelease(&state->mutex);
		elog(WARNING, "pgraft: command queue is full, cannot queue log command");
		return false;
	}

	/* Get pointer to next slot in circular buffer */
	cmd = &state->commands[state->command_tail];

	/* Initialize command */
	cmd->type = type;
	cmd->node_id = 0;  /* Not applicable for log commands */
	cmd->address[0] = '\0';  /* Not applicable for log commands */
	cmd->port = 0;  /* Not applicable for log commands */
	cmd->cluster_id[0] = '\0';  /* Not applicable for log commands */

	/* Set log-specific fields */
	if (log_data) {
		strncpy(cmd->log_data, log_data, sizeof(cmd->log_data) - 1);
		cmd->log_data[sizeof(cmd->log_data) - 1] = '\0';
	} else {
		cmd->log_data[0] = '\0';
	}
	cmd->log_index = log_index;

	/* Initialize status tracking (unique, collision-free command id) */
	cmd->status = COMMAND_STATUS_PENDING;
	cmd->error_message[0] = '\0';
	cmd->timestamp = state->next_command_id++;

	/* Update circular buffer pointers */
	state->command_tail = (state->command_tail + 1) % MAX_COMMANDS;
	command_count = ++state->command_count;

	SpinLockRelease(&state->mutex);

	elog(LOG, "pgraft: log command %d queued (index=%d, count=%d)", type, log_index, command_count);
	return true;
}

/*
 * Add KV command to queue (called by SQL KV functions)
 */
bool
pgraft_queue_kv_command(COMMAND_TYPE type, const char *key, const char *value, const char *client_id)
{
	pgraft_worker_state_t *state;
	pgraft_command_t *cmd;
	int			command_count;

	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}

	SpinLockAcquire(&state->mutex);

	/* Check if queue is full */
	if (state->command_count >= MAX_COMMANDS) {
		SpinLockRelease(&state->mutex);
		elog(WARNING, "pgraft: command queue is full, cannot queue KV command");
		return false;
	}

	/* Get pointer to next slot in circular buffer */
	cmd = &state->commands[state->command_tail];

	/* Initialize command */
	cmd->type = type;
	cmd->node_id = 0;  /* Not applicable for KV commands */
	cmd->address[0] = '\0';  /* Not applicable for KV commands */
	cmd->port = 0;  /* Not applicable for KV commands */
	cmd->cluster_id[0] = '\0';  /* Not applicable for KV commands */

	/* Set KV-specific fields */
	if (key) {
		strncpy(cmd->kv_key, key, sizeof(cmd->kv_key) - 1);
		cmd->kv_key[sizeof(cmd->kv_key) - 1] = '\0';
	} else {
		cmd->kv_key[0] = '\0';
	}

	if (value) {
		strncpy(cmd->kv_value, value, sizeof(cmd->kv_value) - 1);
		cmd->kv_value[sizeof(cmd->kv_value) - 1] = '\0';
	} else {
		cmd->kv_value[0] = '\0';
	}

	if (client_id) {
		strncpy(cmd->kv_client_id, client_id, sizeof(cmd->kv_client_id) - 1);
		cmd->kv_client_id[sizeof(cmd->kv_client_id) - 1] = '\0';
	} else {
		cmd->kv_client_id[0] = '\0';
	}

	/* Initialize status tracking (unique, collision-free command id) */
	cmd->status = COMMAND_STATUS_PENDING;
	cmd->error_message[0] = '\0';
	cmd->timestamp = state->next_command_id++;

	/* Update circular buffer pointers */
	state->command_tail = (state->command_tail + 1) % MAX_COMMANDS;
	command_count = ++state->command_count;

	SpinLockRelease(&state->mutex);

	elog(LOG, "pgraft: KV command %d queued (key=%s, count=%d)", type, key ? key : "NULL", command_count);
	return true;
}

/*
 * Add command to status tracking buffer
 */
bool
pgraft_add_command_to_status(pgraft_command_t *cmd)
{
	pgraft_worker_state_t *state;
	pgraft_command_t *status_cmd;
	bool		dropped_oldest = false;

	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}

	SpinLockAcquire(&state->mutex);

	/* Check if status buffer is full */
	if (state->status_count >= MAX_COMMANDS) {
		/* Remove oldest status entry */
		state->status_head = (state->status_head + 1) % MAX_COMMANDS;
		state->status_count--;
		dropped_oldest = true;
	}

	/* Get pointer to next slot in status circular buffer */
	status_cmd = &state->status_commands[state->status_tail];

	/* Copy command to status buffer */
	*status_cmd = *cmd;

	/* Update circular buffer pointers */
	state->status_tail = (state->status_tail + 1) % MAX_COMMANDS;
	state->status_count++;

	SpinLockRelease(&state->mutex);

	if (dropped_oldest)
		elog(WARNING, "pgraft: status buffer is full, removed oldest entry");

	return true;
}

/*
 * Get command status by timestamp
 */
bool
pgraft_get_command_status(int64_t timestamp, pgraft_command_t *status_cmd)
{
	pgraft_worker_state_t *state;
	int i;
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}

	SpinLockAcquire(&state->mutex);

	/* Search through status buffer */
	for (i = 0; i < state->status_count; i++) {
		int index = (state->status_head + i) % MAX_COMMANDS;
		if (state->status_commands[index].timestamp == timestamp) {
			*status_cmd = state->status_commands[index];
			SpinLockRelease(&state->mutex);
			return true;
		}
	}

	SpinLockRelease(&state->mutex);

	return false;
}

/*
 * Update command status in status buffer
 */
bool
pgraft_update_command_status(int64_t timestamp, COMMAND_STATUS status, const char *error_message)
{
	pgraft_worker_state_t *state;
	int i;
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}

	SpinLockAcquire(&state->mutex);

	/* Search through status buffer */
	for (i = 0; i < state->status_count; i++) {
		int index = (state->status_head + i) % MAX_COMMANDS;
		if (state->status_commands[index].timestamp == timestamp) {
			state->status_commands[index].status = status;
			if (error_message) {
				strncpy(state->status_commands[index].error_message, error_message,
						sizeof(state->status_commands[index].error_message) - 1);
				state->status_commands[index].error_message[sizeof(state->status_commands[index].error_message) - 1] = '\0';
			}
			SpinLockRelease(&state->mutex);
			return true;
		}
	}

	SpinLockRelease(&state->mutex);

	return false;
}

/*
 * Remove completed commands from status buffer
 */
bool
pgraft_remove_completed_commands(void)
{
	pgraft_worker_state_t *state;
	int removed = 0;
	int i;
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}

	SpinLockAcquire(&state->mutex);

	/* Remove completed and failed commands from status buffer */
	for (i = 0; i < state->status_count; i++) {
		int index = (state->status_head + i) % MAX_COMMANDS;
		COMMAND_STATUS status = state->status_commands[index].status;

		if (status == COMMAND_STATUS_COMPLETED || status == COMMAND_STATUS_FAILED) {
			removed++;
		} else {
			/* Move command to front if we removed some */
			if (removed > 0) {
				int new_index = (state->status_head + i - removed) % MAX_COMMANDS;
				state->status_commands[new_index] = state->status_commands[index];
			}
		}
	}

	/* Update status buffer pointers */
	state->status_head = (state->status_head + removed) % MAX_COMMANDS;
	state->status_count -= removed;

	SpinLockRelease(&state->mutex);

	if (removed > 0) {
		elog(LOG, "pgraft: removed %d completed commands from status buffer", removed);
	}

	return true;
}

/*
 * Enqueue a Raft log entry for application to PostgreSQL.
 *
 * CALLED FROM A GO THREAD.  This is the producer half of the single-producer/
 * single-consumer ring described in pgraft_core.h, and it runs on the Go raft
 * goroutine rather than on a PostgreSQL backend thread.  It therefore must
 * not do any of the following, all of which are unsafe off the main thread:
 *
 *   - call elog()/ereport() at any level (elog(ERROR) siglongjmps into the
 *     main thread's exception stack; even elog(LOG) touches process-global
 *     error state and the log pipe),
 *   - call ShmemInitStruct() or otherwise take an LWLock (they touch MyProc),
 *   - acquire the worker state spinlock (shared with backend producers).
 *
 * So it uses only the pointer published by pgraft_apply_queue_attach() and
 * plain atomics, and it reports failures through counters that the background
 * worker drains from its own context.  Keep it that way.
 */
static pgraft_worker_state_t *volatile apply_queue_state = NULL;

void
pgraft_apply_queue_attach(pgraft_worker_state_t *state)
{
	/*
	 * Publish the queue only once the struct is fully initialized: the
	 * barrier keeps the producer from observing the pointer before the
	 * counters it is about to read.
	 */
	pg_write_barrier();
	apply_queue_state = state;
}

bool
pgraft_enqueue_apply_entry(uint64 raft_index, const char *data, size_t data_len)
{
	pgraft_worker_state_t *state = apply_queue_state;
	pgraft_apply_entry_t *entry;
	uint64		write_pos;
	uint64		read_pos;

	/*
	 * No consumer has attached yet (worker not started, or already exited).
	 * Count and drop; there is nobody to apply the entry locally.
	 */
	if (state == NULL)
		return false;

	/*
	 * Reject payloads that would not leave room for a NUL terminator.  The
	 * data is later treated as a C string (data[0] == '{' checks, json-c
	 * parsing), so it must be terminated; use '>=' against the buffer size.
	 */
	if (data_len >= sizeof(entry->data))
	{
		pg_atomic_fetch_add_u64(&state->apply_dropped_size, 1);
		return false;
	}

	/* Producer owns apply_write; it only needs to observe the consumer's
	 * progress in apply_read. */
	write_pos = pg_atomic_read_u64(&state->apply_write);
	read_pos = pg_atomic_read_u64(&state->apply_read);

	if (write_pos - read_pos >= (uint64) MAX_APPLY_ENTRIES)
	{
		pg_atomic_fetch_add_u64(&state->apply_dropped_full, 1);
		return false;
	}

	entry = &state->apply_queue[write_pos % MAX_APPLY_ENTRIES];
	entry->raft_index = raft_index;
	entry->data_len = data_len;
	if (data != NULL && data_len > 0)
		memcpy(entry->data, data, data_len);
	/* Always NUL-terminate so downstream C-string use is safe. */
	entry->data[data_len] = '\0';
	entry->applied = false;

	/*
	 * Publish the slot contents before publishing the new write position, so
	 * the consumer can never observe an index it is allowed to read while the
	 * payload is still being written.
	 */
	pg_write_barrier();
	pg_atomic_write_u64(&state->apply_write, write_pos + 1);

	return true;
}

/*
 * Dequeue a Raft log entry for application to PostgreSQL.
 *
 * Consumer half of the ring; called only from the background worker's main
 * thread, which is the sole owner of apply_read.
 */
bool
pgraft_dequeue_apply_entry(pgraft_apply_entry_t *entry)
{
	pgraft_worker_state_t *state;
	uint64		read_pos;
	uint64		write_pos;

	if (entry == NULL)
		return false;

	state = pgraft_worker_get_state();
	if (state == NULL)
		return false;

	read_pos = pg_atomic_read_u64(&state->apply_read);
	write_pos = pg_atomic_read_u64(&state->apply_write);

	if (read_pos == write_pos)
		return false;			/* empty */

	/* Pair with the producer's write barrier: do not read the slot before we
	 * have observed the published write position. */
	pg_read_barrier();

	*entry = state->apply_queue[read_pos % MAX_APPLY_ENTRIES];

	/* Release the slot only after the copy is complete. */
	pg_write_barrier();
	pg_atomic_write_u64(&state->apply_read, read_pos + 1);

	return true;
}

/*
 * Report and reset the producer-side drop counters.
 *
 * The producer cannot log (see pgraft_enqueue_apply_entry), so the worker
 * surfaces its counters here, from a context where elog() is safe.
 */
void
pgraft_apply_queue_report_drops(void)
{
	pgraft_worker_state_t *state;
	uint64		full;
	uint64		size;

	state = pgraft_worker_get_state();
	if (state == NULL)
		return;

	full = pg_atomic_exchange_u64(&state->apply_dropped_full, 0);
	size = pg_atomic_exchange_u64(&state->apply_dropped_size, 0);

	if (full > 0)
		elog(WARNING, "pgraft: apply queue full, dropped " UINT64_FORMAT
			 " committed entries (queue capacity %d)", full, MAX_APPLY_ENTRIES);
	if (size > 0)
		elog(WARNING, "pgraft: dropped " UINT64_FORMAT
			 " committed entries whose payload exceeded %zu bytes",
			 size, sizeof(state->apply_queue[0].data) - 1);
}

/*
 * Check if apply queue is empty
 */
bool
pgraft_apply_queue_is_empty(void)
{
	pgraft_worker_state_t *state;

	state = pgraft_worker_get_state();
	if (state == NULL)
		return true;

	return pg_atomic_read_u64(&state->apply_read) ==
		pg_atomic_read_u64(&state->apply_write);
}

/*
 * Get number of entries in apply queue
 */
int
pgraft_get_apply_queue_count(void)
{
	pgraft_worker_state_t *state;
	uint64		pending;

	state = pgraft_worker_get_state();
	if (state == NULL)
		return 0;

	/* Read write before read so a concurrent dequeue can only make this
	 * estimate smaller, never negative. */
	pending = pg_atomic_read_u64(&state->apply_write) -
		pg_atomic_read_u64(&state->apply_read);

	return (int) Min(pending, (uint64) MAX_APPLY_ENTRIES);
}
