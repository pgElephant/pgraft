/*-------------------------------------------------------------------------
 *
 * pgraft_log.c
 *      Log replication management for pgraft
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "storage/shmem.h"
#include "storage/spin.h"
#include "utils/elog.h"
#include "utils/timestamp.h"

#include <string.h>

#include "../include/pgraft_log.h"

/* Global shared memory pointer */
static pgraft_log_state_t *g_log_state = NULL;

/*
 * Initialize shared memory for log replication
 */
void
pgraft_log_init_shared_memory(void)
{
	bool		found;
	
	elog(INFO, "pgraft: Initializing log replication shared memory");
	
	/* Allocate shared memory */
	g_log_state = (pgraft_log_state_t *) ShmemInitStruct("pgraft_log_state",
														 sizeof(pgraft_log_state_t),
														 &found);
	
	if (!found)
	{
		elog(INFO, "pgraft: Creating new log replication shared memory");
		
		/* Initialize shared memory */
		memset(g_log_state, 0, sizeof(pgraft_log_state_t));
		
		/* Initialize mutex */
		SpinLockInit(&g_log_state->mutex);
		
		/* Initialize default values */
		g_log_state->log_size = 0;
		g_log_state->last_index = 0;
		g_log_state->commit_index = 0;
		g_log_state->last_applied = 0;
		g_log_state->entries_replicated = 0;
		g_log_state->entries_committed = 0;
		g_log_state->entries_applied = 0;
		g_log_state->replication_errors = 0;
		
		elog(INFO, "pgraft: Log replication shared memory initialized");
	}
	else
	{
		elog(INFO, "pgraft: Log replication shared memory already exists");
	}
}

/*
 * Get shared memory pointer
 */
pgraft_log_state_t *
pgraft_log_get_shared_memory(void)
{
	if (g_log_state == NULL)
	{
		/* Initialize shared memory when first accessed */
		pgraft_log_init_shared_memory();
	}
	return g_log_state;
}

/*
 * Append entry to log
 */
int
pgraft_log_append_entry(int64_t term, const char *data, int32_t data_size)
{
	pgraft_log_state_t *state;
	pgraft_log_entry_t *entry;
	
	state = pgraft_log_get_shared_memory();
	if (!state)
	{
		elog(ERROR, "pgraft: Failed to get shared memory");
		return -1;
	}
	
	if (data_size > 1024)
	{
		elog(ERROR, "pgraft: Data size %d exceeds maximum 1024", data_size);
		return -1;
	}
	
	SpinLockAcquire(&state->mutex);
	
	if (state->log_size >= 1000)
	{
		SpinLockRelease(&state->mutex);
		elog(ERROR, "pgraft: Log is full (1000 entries)");
		return -1;
	}
	
	/* Add new entry */
	entry = &state->entries[state->log_size];
	entry->index = state->last_index + 1;
	entry->term = term;
	entry->timestamp = GetCurrentTimestamp();
	entry->data_size = data_size;
	entry->committed = 0;
	entry->applied = 0;
	
	if (data && data_size > 0)
		memcpy(entry->data, data, data_size);
	entry->data[data_size] = '\0';
	
	state->log_size++;
	state->last_index = entry->index;
	
	SpinLockRelease(&state->mutex);
	
	elog(DEBUG1, "pgraft: Appended entry %lld with term %lld", entry->index, term);
	return 0;
}

/*
 * Commit log entry
 */
int
pgraft_log_commit_entry(int64_t index)
{
	pgraft_log_state_t *state;
	int			i;
	
	state = pgraft_log_get_shared_memory();
	if (!state)
	{
		elog(ERROR, "pgraft: Failed to get shared memory");
		return -1;
	}
	
	SpinLockAcquire(&state->mutex);
	
	/* Find and commit entry */
	for (i = 0; i < state->log_size; i++)
	{
		if (state->entries[i].index == index)
		{
			state->entries[i].committed = 1;
			if (index > state->commit_index)
				state->commit_index = index;
			state->entries_committed++;
			SpinLockRelease(&state->mutex);
			elog(DEBUG1, "pgraft: Committed entry %lld", index);
			return 0;
		}
	}
	
	SpinLockRelease(&state->mutex);
	elog(WARNING, "pgraft: Entry %lld not found", index);
	return -1;
}

/*
 * Apply log entry
 */
int
pgraft_log_apply_entry(int64_t index)
{
    pgraft_log_state_t *state = pgraft_log_get_shared_memory();
    if (!state) {
        elog(ERROR, "pgraft: Failed to get shared memory");
        return -1;
    }
    
    SpinLockAcquire(&state->mutex);
    
    /* Find and apply entry */
    for (int i = 0; i < state->log_size; i++) {
        if (state->entries[i].index == index) {
            if (!state->entries[i].committed) {
                SpinLockRelease(&state->mutex);
			elog(WARNING, "pgraft: Cannot apply uncommitted entry %lld", index);
                return -1;
            }
            
            state->entries[i].applied = 1;
            if (index > state->last_applied) {
                state->last_applied = index;
            }
            state->entries_applied++;
            SpinLockRelease(&state->mutex);
			elog(DEBUG1, "pgraft: Applied entry %lld", index);
            return 0;
        }
    }
    
    SpinLockRelease(&state->mutex);
    elog(WARNING, "pgraft: Entry %lld not found", index);
    return -1;
}

/*
 * Get log entry by index
 */
int
pgraft_log_get_entry(int64_t index, pgraft_log_entry_t* entry)
{
    pgraft_log_state_t *state = pgraft_log_get_shared_memory();
    if (!state || !entry) {
        elog(ERROR, "pgraft: Invalid parameters");
        return -1;
    }
    
    SpinLockAcquire(&state->mutex);
    
    /* Find entry */
    for (int i = 0; i < state->log_size; i++) {
        if (state->entries[i].index == index) {
            *entry = state->entries[i];
            SpinLockRelease(&state->mutex);
            return 0;
        }
    }
    
    SpinLockRelease(&state->mutex);
    return -1;
}

/*
 * Get last log index
 */
int
pgraft_log_get_last_index(int64_t* last_index)
{
    pgraft_log_state_t *state = pgraft_log_get_shared_memory();
    if (!state || !last_index) {
        elog(ERROR, "pgraft: Invalid parameters");
        return -1;
    }
    
    SpinLockAcquire(&state->mutex);
    *last_index = state->last_index;
    SpinLockRelease(&state->mutex);
    
    return 0;
}

/*
 * Get commit index
 */
int
pgraft_log_get_commit_index(int64_t* commit_index)
{
    pgraft_log_state_t *state = pgraft_log_get_shared_memory();
    if (!state || !commit_index) {
        elog(ERROR, "pgraft: Invalid parameters");
        return -1;
    }
    
    SpinLockAcquire(&state->mutex);
    *commit_index = state->commit_index;
    SpinLockRelease(&state->mutex);
    
    return 0;
}

/*
 * Get last applied index
 */
int
pgraft_log_get_last_applied(int64_t* last_applied)
{
    pgraft_log_state_t *state = pgraft_log_get_shared_memory();
    if (!state || !last_applied) {
        elog(ERROR, "pgraft: Invalid parameters");
        return -1;
    }
    
    SpinLockAcquire(&state->mutex);
    *last_applied = state->last_applied;
    SpinLockRelease(&state->mutex);
    
    return 0;
}

/*
 * Replicate log to node
 */
int
pgraft_log_replicate_to_node(int32_t node_id, int64_t from_index)
{
    pgraft_log_state_t *state;
    int entries_to_replicate = 0;
    int i;
    
    state = pgraft_log_get_shared_memory();
    if (!state) {
        elog(ERROR, "pgraft: Failed to get shared memory");
        return -1;
    }
    
    elog(DEBUG1, "pgraft: Replicating to node %d from index %lld", node_id, from_index);
    
    SpinLockAcquire(&state->mutex);
    
    /* Find entries to replicate */
    
    for (i = 0; i < state->log_size; i++) {
        if (state->entries[i].index >= from_index) {
            entries_to_replicate++;
        }
    }
    
    state->entries_replicated += entries_to_replicate;
    
    SpinLockRelease(&state->mutex);
    
    elog(DEBUG1, "pgraft: Replicated %d entries to node %d", entries_to_replicate, node_id);
    return 0;
}

/*
 * Replicate log from leader
 */
int
pgraft_log_replicate_from_leader(int32_t leader_id, int64_t from_index)
{
    elog(DEBUG1, "pgraft: Replicating from leader %d from index %lld", leader_id, from_index);
    
    /* This would typically involve network communication with the leader */
    /* For now, we'll just log the operation */
    
    return 0;
}

/*
 * Sync log with leader
 */
int
pgraft_log_sync_with_leader(void)
{
    elog(DEBUG1, "pgraft: Syncing log with leader");
    
    /* This would typically involve:
     * 1. Getting the leader's last index
     * 2. Comparing with our last index
     * 3. Replicating missing entries
     * 4. Committing entries that are committed on the leader
     */
    
    return 0;
}

/*
 * Get log statistics
 */
int
pgraft_log_get_statistics(pgraft_log_state_t* stats)
{
    pgraft_log_state_t *state = pgraft_log_get_shared_memory();
    if (!state || !stats) {
        elog(ERROR, "pgraft: Invalid parameters");
        return -1;
    }
    
    SpinLockAcquire(&state->mutex);
    *stats = *state;
    SpinLockRelease(&state->mutex);
    
    return 0;
}

/*
 * Get replication status
 */
int
pgraft_log_get_replication_status(char* status, size_t status_size)
{
    pgraft_log_state_t *state = pgraft_log_get_shared_memory();
    if (!state || !status || status_size == 0) {
        elog(ERROR, "pgraft: Invalid parameters");
        return -1;
    }
    
    SpinLockAcquire(&state->mutex);
    
    snprintf(status, status_size, 
             "Log Size: %d, Last Index: %lld, Commit Index: %lld, Last Applied: %lld, "
             "Replicated: %lld, Committed: %lld, Applied: %lld, Errors: %lld",
             state->log_size, state->last_index, state->commit_index, state->last_applied,
             state->entries_replicated, state->entries_committed, 
             state->entries_applied, state->replication_errors);
    
    SpinLockRelease(&state->mutex);
    
    return 0;
}

/*
 * Cleanup old log entries
 */
void
pgraft_log_cleanup_old_entries(int64_t before_index)
{
    pgraft_log_state_t *state;
    int removed = 0;
    int i;
    int j;
    
    state = pgraft_log_get_shared_memory();
    if (!state) {
        return;
    }
    
    SpinLockAcquire(&state->mutex);
    
    for (i = 0; i < state->log_size; i++) {
        if (state->entries[i].index < before_index) {
            /* Shift remaining entries */
            for (j = i; j < state->log_size - 1; j++) {
                state->entries[j] = state->entries[j + 1];
            }
            state->log_size--;
            removed++;
            i--; /* Check the same index again */
        }
    }
    
    SpinLockRelease(&state->mutex);
    
    if (removed > 0) {
		elog(INFO, "pgraft: Removed %d old entries before index %lld", removed, before_index);
    }
}

/*
 * Reset log
 */
void
pgraft_log_reset(void)
{
    pgraft_log_state_t *state = pgraft_log_get_shared_memory();
    if (!state) {
        return;
    }
    
    SpinLockAcquire(&state->mutex);
    
    state->log_size = 0;
    state->last_index = 0;
    state->commit_index = 0;
    state->last_applied = 0;
    state->entries_replicated = 0;
    state->entries_committed = 0;
    state->entries_applied = 0;
    state->replication_errors = 0;
    
    SpinLockRelease(&state->mutex);
    
    elog(INFO, "pgraft: Log reset completed");
}
