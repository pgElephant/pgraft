/*-------------------------------------------------------------------------
 *
 * pgraft_state.c
 *      State persistence management for pgraft
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "storage/shmem.h"
#include "storage/spin.h"
#include "utils/elog.h"

#include <string.h>

#include "../include/pgraft_state.h"

/* Global shared memory pointer */
static pgraft_go_state_t *g_go_state = NULL;

/*
 * Initialize shared memory for Go state persistence
 */
void
pgraft_state_init_shared_memory(void)
{
	bool		found;
	
	elog(INFO, "pgraft: initializing Go state shared memory");
	
	/* Allocate shared memory */
	g_go_state = (pgraft_go_state_t *) ShmemInitStruct("pgraft_go_state",
													   sizeof(pgraft_go_state_t),
													   &found);
	
	if (!found)
	{
		elog(INFO, "pgraft: creating new Go state shared memory");
		
		/* Initialize shared memory */
		memset(g_go_state, 0, sizeof(pgraft_go_state_t));
		
		/* Initialize mutex */
		SpinLockInit(&g_go_state->mutex);
		
		/* Initialize default values */
		g_go_state->go_lib_loaded = 0;
		g_go_state->go_initialized = 0;
		g_go_state->go_running = 0;
		g_go_state->go_node_id = -1;
		g_go_state->go_address[0] = '\0';
		g_go_state->go_port = -1;
		g_go_state->go_current_term = 0;
		g_go_state->go_voted_for = 0;
		g_go_state->go_commit_index = 0;
		g_go_state->go_last_applied = 0;
		g_go_state->go_last_index = 0;
		g_go_state->go_leader_id = 0;
		strncpy(g_go_state->go_raft_state, "unknown", sizeof(g_go_state->go_raft_state) - 1);
		g_go_state->go_raft_state[sizeof(g_go_state->go_raft_state) - 1] = '\0';
		g_go_state->num_nodes = 0;
		memset(g_go_state->node_ids, 0, sizeof(g_go_state->node_ids));
		memset(g_go_state->node_addresses, 0, sizeof(g_go_state->node_addresses));
		memset(g_go_state->node_ports, 0, sizeof(g_go_state->node_ports));
		g_go_state->go_messages_processed = 0;
		g_go_state->go_log_entries_committed = 0;
		g_go_state->go_heartbeats_sent = 0;
		g_go_state->go_elections_triggered = 0;
		
		elog(INFO, "pgraft: go state shared memory initialized");
	}
	else
	{
		elog(INFO, "pgraft: go state shared memory already exists");
	}
}

/*
 * Get shared memory pointer
 */
pgraft_go_state_t *
pgraft_state_get_shared_memory(void)
{
	if (g_go_state == NULL)
	{
		/* Initialize shared memory when first accessed */
		pgraft_state_init_shared_memory();
	}
	return g_go_state;
}

/*
 * Save Go library state to shared memory
 */
void
pgraft_state_save_go_library_state(void)
{
	pgraft_go_state_t *state;
	
	state = pgraft_state_get_shared_memory();
	if (!state)
	{
		elog(ERROR, "pgraft: failed to get shared memory");
		return;
	}
	
	SpinLockAcquire(&state->mutex);
	
	/* Save Go library state flags */
	state->go_lib_loaded = 1;	/* Assume loaded if we're saving state */
	state->go_initialized = 1;	/* Assume initialized if we're saving state */
	state->go_running = 1;		/* Assume running if we're saving state */
	
	SpinLockRelease(&state->mutex);
	
	elog(DEBUG1, "pgraft: Saved Go library state to shared memory");
}

/*
 * Restore Go library state from shared memory
 */
void
pgraft_state_restore_go_library_state(void)
{
	pgraft_go_state_t *state;
	bool		was_loaded;
	bool		was_initialized;
	bool		was_running;
	
	state = pgraft_state_get_shared_memory();
	if (!state)
	{
		elog(ERROR, "pgraft: failed to get shared memory");
		return;
	}
	
	SpinLockAcquire(&state->mutex);
	
	/* Check if Go library was previously loaded and initialized */
	was_loaded = (state->go_lib_loaded != 0);
	was_initialized = (state->go_initialized != 0);
	was_running = (state->go_running != 0);
	
	SpinLockRelease(&state->mutex);
	
	if (was_loaded && was_initialized)
	{
		elog(INFO, "pgraft: restoring Go library state (was running: %s)", 
			 was_running ? "yes" : "no");
	}
	else
	{
		elog(INFO, "pgraft: no previous Go library state found");
	}
}

/*
 * Save Go Raft state to shared memory
 */
void
pgraft_state_save_go_raft_state(void)
{
    pgraft_go_state_t *state = pgraft_state_get_shared_memory();
    if (!state) {
        elog(ERROR, "pgraft: failed to get shared memory");
        return;
    }
    
    SpinLockAcquire(&state->mutex);
    
    /* Update Go library state flags */
    state->go_lib_loaded = 1;
    state->go_initialized = 1;
    state->go_running = 1;
    
    SpinLockRelease(&state->mutex);
    
    elog(DEBUG1, "pgraft: Saved Go Raft state to shared memory");
}

/*
 * Restore Go Raft state from shared memory
 */
void
pgraft_state_restore_go_raft_state(void)
{
    pgraft_go_state_t *state;
    bool was_initialized;
    bool was_running;
    
    state = pgraft_state_get_shared_memory();
    if (!state) {
        elog(ERROR, "pgraft: failed to get shared memory");
        return;
    }
    
    SpinLockAcquire(&state->mutex);
    
    /* Check if Go Raft state was previously saved */
    was_initialized = (state->go_initialized != 0);
    was_running = (state->go_running != 0);
    
    SpinLockRelease(&state->mutex);
    
    if (was_initialized) {
        elog(INFO, "pgraft: restoring Go Raft state (was running: %s)", 
             was_running ? "yes" : "no");
    } else {
        elog(INFO, "pgraft: no previous Go Raft state found");
    }
}

/*
 * Save node configuration to shared memory
 */
void
pgraft_state_save_node_config(int32_t node_id, const char* address, int32_t port)
{
    pgraft_go_state_t *state = pgraft_state_get_shared_memory();
    if (!state) {
        elog(ERROR, "pgraft: failed to get shared memory");
        return;
    }
    
    SpinLockAcquire(&state->mutex);
    
    state->go_node_id = node_id;
    strncpy(state->go_address, address ? address : "", sizeof(state->go_address) - 1);
    state->go_address[sizeof(state->go_address) - 1] = '\0';
    state->go_port = port;
    
    SpinLockRelease(&state->mutex);
    
    elog(DEBUG1, "pgraft: Saved node config %d at %s:%d", node_id, address, port);
}

/*
 * Restore node configuration from shared memory
 */
void
pgraft_state_restore_node_config(int32_t* node_id, char* address, int32_t* port)
{
    pgraft_go_state_t *state = pgraft_state_get_shared_memory();
    if (!state || !node_id || !address || !port) {
        elog(ERROR, "pgraft: invalid parameters");
        return;
    }
    
    SpinLockAcquire(&state->mutex);
    
    *node_id = state->go_node_id;
    strncpy(address, state->go_address, 255);
    address[255] = '\0';
    *port = state->go_port;
    
    SpinLockRelease(&state->mutex);
    
    elog(DEBUG1, "pgraft: Restored node config %d at %s:%d", *node_id, address, *port);
}

/*
 * Save cluster nodes configuration to shared memory
 */
void
pgraft_state_save_cluster_nodes(int32_t num_nodes, int32_t* node_ids, 
                               char node_addresses[][256], int32_t* node_ports)
{
    pgraft_go_state_t *state = pgraft_state_get_shared_memory();
    if (!state) {
        elog(ERROR, "pgraft: failed to get shared memory");
        return;
    }
    
    SpinLockAcquire(&state->mutex);
    
    state->num_nodes = num_nodes;
    for (int i = 0; i < num_nodes && i < 16; i++) {
        state->node_ids[i] = node_ids[i];
        strncpy(state->node_addresses[i], node_addresses[i], sizeof(state->node_addresses[i]) - 1);
        state->node_addresses[i][sizeof(state->node_addresses[i]) - 1] = '\0';
        state->node_ports[i] = node_ports[i];
    }
    
    SpinLockRelease(&state->mutex);
    
    elog(DEBUG1, "pgraft: Saved %d cluster nodes", num_nodes);
}

/*
 * Restore cluster nodes configuration from shared memory
 */
void
pgraft_state_restore_cluster_nodes(int32_t* num_nodes, int32_t* node_ids, 
                                  char node_addresses[][256], int32_t* node_ports)
{
    pgraft_go_state_t *state = pgraft_state_get_shared_memory();
    if (!state || !num_nodes || !node_ids || !node_addresses || !node_ports) {
        elog(ERROR, "pgraft: invalid parameters");
        return;
    }
    
    SpinLockAcquire(&state->mutex);
    
    *num_nodes = state->num_nodes;
    for (int i = 0; i < state->num_nodes && i < 16; i++) {
        node_ids[i] = state->node_ids[i];
        strncpy(node_addresses[i], state->node_addresses[i], 255);
        node_addresses[i][255] = '\0';
        node_ports[i] = state->node_ports[i];
    }
    
    SpinLockRelease(&state->mutex);
    
    elog(DEBUG1, "pgraft: Restored %d cluster nodes", *num_nodes);
}

/* State validation functions */
bool pgraft_state_is_go_lib_loaded(void) {
    pgraft_go_state_t *state;
    bool loaded;
    
    state = pgraft_state_get_shared_memory();
    if (!state) return false;
    SpinLockAcquire(&state->mutex);
    loaded = (state->go_lib_loaded != 0);
    SpinLockRelease(&state->mutex);
    return loaded;
}

bool pgraft_state_is_go_initialized(void) {
    pgraft_go_state_t *state;
    bool initialized;
    
    state = pgraft_state_get_shared_memory();
    if (!state) return false;
    SpinLockAcquire(&state->mutex);
    initialized = (state->go_initialized != 0);
    SpinLockRelease(&state->mutex);
    return initialized;
}

bool pgraft_state_is_go_running(void) {
    pgraft_go_state_t *state;
    bool running;
    
    state = pgraft_state_get_shared_memory();
    if (!state) return false;
    SpinLockAcquire(&state->mutex);
    running = (state->go_running != 0);
    SpinLockRelease(&state->mutex);
    return running;
}

void pgraft_state_set_go_lib_loaded(bool loaded) {
    pgraft_go_state_t *state = pgraft_state_get_shared_memory();
    if (!state) return;
    SpinLockAcquire(&state->mutex);
    state->go_lib_loaded = loaded ? 1 : 0;
    SpinLockRelease(&state->mutex);
}

void pgraft_state_set_go_initialized(bool initialized) {
    pgraft_go_state_t *state = pgraft_state_get_shared_memory();
    if (!state) return;
    SpinLockAcquire(&state->mutex);
    state->go_initialized = initialized ? 1 : 0;
    SpinLockRelease(&state->mutex);
}

void pgraft_state_set_go_running(bool running) {
    pgraft_go_state_t *state = pgraft_state_get_shared_memory();
    if (!state) return;
    SpinLockAcquire(&state->mutex);
    state->go_running = running ? 1 : 0;
    SpinLockRelease(&state->mutex);
}

/*
 * Initialize shared memory for worker state
 */
void
pgraft_worker_init_shared_memory(void)
{
	/* Worker state is managed in pgraft_core.c */
	elog(LOG, "pgraft: worker shared memory initialization completed");
}
