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
	
	elog(LOG, "pgraft: pgraft_queue_command called with type=%d, node_id=%d, address=%s, port=%d", 
		 type, node_id, address ? address : "NULL", port);
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		elog(ERROR, "pgraft: failed to get worker state in pgraft_queue_command");
		return false;
	}
	
	/* Check if queue is full */
	if (state->command_count >= MAX_COMMANDS) {
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
	
	/* Initialize status tracking */
	cmd->status = COMMAND_STATUS_PENDING;
	cmd->error_message[0] = '\0';
	cmd->timestamp = time(NULL);
	
	/* Update circular buffer pointers */
	state->command_tail = (state->command_tail + 1) % MAX_COMMANDS;
	state->command_count++;
	
	elog(LOG, "pgraft: command %d queued for node %d at %s:%d (count=%d)", 
		 type, node_id, address, port, state->command_count);
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
	
	/* Check if queue is empty */
	if (state->command_count == 0) {
		return false;
	}
	
	/* Copy command from head of circular buffer */
	*cmd = state->commands[state->command_head];
	
	/* Update circular buffer pointers */
	state->command_head = (state->command_head + 1) % MAX_COMMANDS;
	state->command_count--;
	
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
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}
	
	/* Check if queue is full */
	if (state->command_count >= MAX_COMMANDS) {
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
	
	/* Initialize status tracking */
	cmd->status = COMMAND_STATUS_PENDING;
	cmd->error_message[0] = '\0';
	cmd->timestamp = time(NULL);
	
	/* Update circular buffer pointers */
	state->command_tail = (state->command_tail + 1) % MAX_COMMANDS;
	state->command_count++;
	
	elog(LOG, "pgraft: log command %d queued (index=%d, count=%d)", type, log_index, state->command_count);
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
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}
	
	/* Check if queue is full */
	if (state->command_count >= MAX_COMMANDS) {
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
	
	/* Initialize status tracking */
	cmd->status = COMMAND_STATUS_PENDING;
	cmd->error_message[0] = '\0';
	cmd->timestamp = time(NULL);
	
	/* Update circular buffer pointers */
	state->command_tail = (state->command_tail + 1) % MAX_COMMANDS;
	state->command_count++;
	
	elog(LOG, "pgraft: KV command %d queued (key=%s, count=%d)", type, key ? key : "NULL", state->command_count);
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
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}
	
	/* Check if status buffer is full */
	if (state->status_count >= MAX_COMMANDS) {
		elog(WARNING, "pgraft: status buffer is full, removing oldest entry");
		/* Remove oldest status entry */
		state->status_head = (state->status_head + 1) % MAX_COMMANDS;
		state->status_count--;
	}
	
	/* Get pointer to next slot in status circular buffer */
	status_cmd = &state->status_commands[state->status_tail];
	
	/* Copy command to status buffer */
	*status_cmd = *cmd;
	
	/* Update circular buffer pointers */
	state->status_tail = (state->status_tail + 1) % MAX_COMMANDS;
	state->status_count++;
	
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
	
	/* Search through status buffer */
	for (i = 0; i < state->status_count; i++) {
		int index = (state->status_head + i) % MAX_COMMANDS;
		if (state->status_commands[index].timestamp == timestamp) {
			*status_cmd = state->status_commands[index];
			return true;
		}
	}
	
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
			return true;
		}
	}
	
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
	
	if (removed > 0) {
		elog(LOG, "pgraft: removed %d completed commands from status buffer", removed);
	}
	
	return true;
}

/*
 * Enqueue a Raft log entry for application to PostgreSQL
 * Called from Go applyEntry() when a Raft entry is committed
 */
bool
pgraft_enqueue_apply_entry(uint64 raft_index, const char *data, size_t data_len)
{
	pgraft_worker_state_t *state;
	pgraft_apply_entry_t *entry;
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		elog(ERROR, "pgraft: failed to get worker state in pgraft_enqueue_apply_entry");
		return false;
	}
	
	/* Check if queue is full */
	if (state->apply_count >= MAX_APPLY_ENTRIES) {
		elog(WARNING, "pgraft: apply queue is full (%d entries), cannot enqueue index %lu",
			 MAX_APPLY_ENTRIES, (unsigned long) raft_index);
		return false;
	}
	
	/* Check data length */
	if (data_len > sizeof(entry->data)) {
		elog(WARNING, "pgraft: entry data too large (%zu bytes, max %zu), index %lu",
			 data_len, sizeof(entry->data), (unsigned long) raft_index);
		return false;
	}
	
	/* Get pointer to next slot in circular buffer */
	entry = &state->apply_queue[state->apply_tail];
	
	/* Initialize entry */
	entry->raft_index = raft_index;
	entry->data_len = data_len;
	if (data && data_len > 0) {
		memcpy(entry->data, data, data_len);
	}
	entry->applied = false;
	
	/* Update circular buffer pointers */
	state->apply_tail = (state->apply_tail + 1) % MAX_APPLY_ENTRIES;
	state->apply_count++;
	
	elog(DEBUG1, "pgraft: enqueued apply entry %lu (count=%d)",
		 (unsigned long) raft_index, state->apply_count);
	
	return true;
}

/*
 * Dequeue a Raft log entry for application to PostgreSQL
 * Called from background worker to apply entries
 */
bool
pgraft_dequeue_apply_entry(pgraft_apply_entry_t *entry)
{
	pgraft_worker_state_t *state;
	pgraft_apply_entry_t *queued_entry;
	
	if (entry == NULL) {
		return false;
	}
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return false;
	}
	
	/* Check if queue is empty */
	if (state->apply_count == 0) {
		return false;
	}
	
	/* Get pointer to next entry in circular buffer */
	queued_entry = &state->apply_queue[state->apply_head];
	
	/* Copy entry data */
	*entry = *queued_entry;
	
	/* Update circular buffer pointers */
	state->apply_head = (state->apply_head + 1) % MAX_APPLY_ENTRIES;
	state->apply_count--;
	
	elog(DEBUG2, "pgraft: dequeued apply entry %lu (count=%d)",
		 (unsigned long) entry->raft_index, state->apply_count);
	
	return true;
}

/*
 * Check if apply queue is empty
 */
bool
pgraft_apply_queue_is_empty(void)
{
	pgraft_worker_state_t *state;
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return true;
	}
	
	return (state->apply_count == 0);
}

/*
 * Get number of entries in apply queue
 */
int
pgraft_get_apply_queue_count(void)
{
	pgraft_worker_state_t *state;
	
	state = pgraft_worker_get_state();
	if (state == NULL) {
		return 0;
	}
	
	return state->apply_count;
}
