/*-------------------------------------------------------------------------
 *
 * pgraft_core.c
 *      Core consensus logic for pgraft
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

#include "../include/pgraft_core.h"
#include "../include/pgraft_go.h"

/*
 * Initialize core consensus system
 */
int
pgraft_core_init(int32_t node_id, const char *address, int32_t port)
{
	pgraft_cluster_t *cluster;
	
	cluster = pgraft_core_get_shared_memory();
	if (!cluster)
	{
		elog(ERROR, "pgraft: core init failed to get shared memory");
		return -1;
	}
	
	SpinLockAcquire(&cluster->mutex);
	if (cluster->initialized)
	{
		SpinLockRelease(&cluster->mutex);
		elog(INFO, "pgraft: core system already initialized");
		return 0;
	}
	
	cluster->node_id = node_id;
	cluster->current_term = 0;
	cluster->leader_id = -1;
	strncpy(cluster->state, "follower", sizeof(cluster->state) - 1);
	cluster->state[sizeof(cluster->state) - 1] = '\0';
	cluster->num_nodes = 1;
	cluster->messages_processed = 0;
	cluster->heartbeats_sent = 0;
	cluster->elections_triggered = 0;
	
	cluster->nodes[0].id = node_id;
	strncpy(cluster->nodes[0].address, address, sizeof(cluster->nodes[0].address) - 1);
	cluster->nodes[0].address[sizeof(cluster->nodes[0].address) - 1] = '\0';
	cluster->nodes[0].port = port;
	cluster->nodes[0].is_leader = false;
	
	cluster->initialized = true;
	SpinLockRelease(&cluster->mutex);
	
	elog(INFO, "pgraft: core initialized node %d at %s:%d", node_id, address, port);
	elog(INFO, "pgraft: cluster state: term=%d, leader=%lld, state=%s",
		 cluster->current_term, cluster->leader_id, cluster->state);
	
	return 0;
}

/*
 * Add node to cluster
 */
int
pgraft_core_add_node(int32_t node_id, const char *address, int32_t port)
{
	pgraft_cluster_t *cluster;
	pgraft_node_t *node;
	
	cluster = pgraft_core_get_shared_memory();
	if (!cluster)
	{
		elog(ERROR, "pgraft: cannot add node %d - failed to get shared memory", node_id);
		return -1;
	}
	
	SpinLockAcquire(&cluster->mutex);
	if (!cluster->initialized)
	{
		SpinLockRelease(&cluster->mutex);
		elog(ERROR, "pgraft: cannot add node %d - core system not initialized", node_id);
		return -1;
	}
	
	if (cluster->num_nodes >= 16)
	{
		SpinLockRelease(&cluster->mutex);
		elog(ERROR, "pgraft: maximum number of nodes (16) reached");
		return -1;
	}
	
	node = &cluster->nodes[cluster->num_nodes];
	node->id = node_id;
	strncpy(node->address, address, sizeof(node->address) - 1);
	node->address[sizeof(node->address) - 1] = '\0';
	node->port = port;
	node->is_leader = false;
	
	cluster->num_nodes++;
	
	SpinLockRelease(&cluster->mutex);
	
	elog(INFO, "pgraft: added node %d at %s:%d", node_id, address, port);
	elog(INFO, "pgraft: total nodes in cluster: %d", cluster->num_nodes);
	return 0;
}

/*
 * Remove node from cluster
 */
int
pgraft_core_remove_node(int32_t node_id)
{
	pgraft_cluster_t *cluster;
	int			i;
	int			j;
	
	cluster = pgraft_core_get_shared_memory();
	if (!cluster)
	{
		elog(ERROR, "pgraft: cannot remove node %d - failed to get shared memory", node_id);
		return -1;
	}
	
	SpinLockAcquire(&cluster->mutex);
	if (!cluster->initialized)
	{
		SpinLockRelease(&cluster->mutex);
		elog(ERROR, "pgraft: cannot remove node %d - core system not initialized", node_id);
		return -1;
	}
	
	for (i = 0; i < cluster->num_nodes; i++)
	{
		if (cluster->nodes[i].id == node_id)
		{
			for (j = i; j < cluster->num_nodes - 1; j++)
				cluster->nodes[j] = cluster->nodes[j + 1];
			cluster->num_nodes--;
			SpinLockRelease(&cluster->mutex);
			elog(INFO, "pgraft: removed node %d", node_id);
			return 0;
		}
	}
	
	SpinLockRelease(&cluster->mutex);
	elog(WARNING, "pgraft: node %d not found", node_id);
	return -1;
}

/*
 * Get cluster state
 */
int
pgraft_core_get_cluster_state(pgraft_cluster_t *cluster)
{
	pgraft_cluster_t *shm_cluster;
	
	elog(LOG, "pgraft: pgraft_core_get_cluster_state called");
	
	if (!cluster)
	{
		elog(WARNING, "pgraft: cluster parameter is null");
		return -1;
	}
	
	shm_cluster = pgraft_core_get_shared_memory();
	if (!shm_cluster)
		return -1;
	
	SpinLockAcquire(&shm_cluster->mutex);
	if (!shm_cluster->initialized)
	{
		SpinLockRelease(&shm_cluster->mutex);
		elog(LOG, "pgraft: core system not initialized in shared memory.");
		memset(cluster, 0, sizeof(pgraft_cluster_t));
	}
	else
	{
		*cluster = *shm_cluster;
		SpinLockRelease(&shm_cluster->mutex);
		elog(LOG, "pgraft: got cluster state from shared memory: leader=%lld, term=%d", 
			 (long long)cluster->leader_id, cluster->current_term);
	}
	
	return 0;
}

/*
 * Check if current node is leader
 */
bool
pgraft_core_is_leader(void)
{
	pgraft_cluster_t *cluster;
	bool		is_leader;
	
	/* Get shared memory */
	cluster = pgraft_core_get_shared_memory();
	if (!cluster)
		return false;
	
	/* Check if core system is initialized */
	SpinLockAcquire(&cluster->mutex);
	if (!cluster->initialized)
	{
		SpinLockRelease(&cluster->mutex);
		return false;
	}
	
	is_leader = (cluster->node_id == cluster->leader_id);
	SpinLockRelease(&cluster->mutex);
	
	return is_leader;
}

/*
 * Update cluster state (called from Go code)
 */
int
pgraft_core_update_cluster_state(int64_t leader_id, int64_t current_term, const char *state)
{
	pgraft_cluster_t *cluster;
	
	/* Get shared memory */
	cluster = pgraft_core_get_shared_memory();
	if (!cluster)
		return -1;
	
	/* Check if core system is initialized */
	SpinLockAcquire(&cluster->mutex);
	if (!cluster->initialized)
	{
		SpinLockRelease(&cluster->mutex);
		return -1;
	}
	
	/* Update cluster state */
	cluster->leader_id = leader_id;
	cluster->current_term = current_term;
	if (state) {
		strncpy(cluster->state, state, sizeof(cluster->state) - 1);
		cluster->state[sizeof(cluster->state) - 1] = '\0';
	}
	
	SpinLockRelease(&cluster->mutex);
	
	return 0;
}

/*
 * Update nodes array in shared memory
 */
int
pgraft_core_update_nodes(int32_t num_nodes, int32_t *node_ids, char **addresses)
{
	pgraft_cluster_t *cluster;
	int i;
	
	/* Get shared memory */
	cluster = pgraft_core_get_shared_memory();
	if (!cluster)
		return -1;
	
	SpinLockAcquire(&cluster->mutex);
	if (!cluster->initialized)
	{
		SpinLockRelease(&cluster->mutex);
		return -1;
	}
	
	/* Clear existing nodes */
	memset(cluster->nodes, 0, sizeof(cluster->nodes));
	cluster->num_nodes = 0;
	
	/* Copy up to 16 nodes */
	for (i = 0; i < num_nodes && i < 16; i++)
	{
		cluster->nodes[i].id = node_ids[i];
		strncpy(cluster->nodes[i].address, addresses[i], sizeof(cluster->nodes[i].address) - 1);
		cluster->nodes[i].address[sizeof(cluster->nodes[i].address) - 1] = '\0';
		cluster->num_nodes++;
	}
	
	SpinLockRelease(&cluster->mutex);
	
	return 0;
}

/*
 * Get current leader ID
 */
int64_t
pgraft_core_get_leader_id(void)
{
	pgraft_cluster_t *cluster;
	int64_t		leader_id;
	
	/* Get shared memory */
	cluster = pgraft_core_get_shared_memory();
	if (!cluster)
		return -1;
	
	/* Check if core system is initialized */
	SpinLockAcquire(&cluster->mutex);
	if (!cluster->initialized)
	{
		SpinLockRelease(&cluster->mutex);
		return -1;
	}
	
	leader_id = cluster->leader_id;
	SpinLockRelease(&cluster->mutex);
	
	return leader_id;
}

/*
 * Get current term
 */
int32_t
pgraft_core_get_current_term(void)
{
	pgraft_cluster_t *cluster;
	int32_t		term;
	
	/* Get shared memory */
	cluster = pgraft_core_get_shared_memory();
	if (!cluster)
		return 0;
	
	/* Check if core system is initialized */
	SpinLockAcquire(&cluster->mutex);
	if (!cluster->initialized)
	{
		SpinLockRelease(&cluster->mutex);
		return 0;
	}
	
	term = cluster->current_term;
	SpinLockRelease(&cluster->mutex);
	
	return term;
}

/*
 * Cleanup core system
 */
void
pgraft_core_cleanup(void)
{
	pgraft_cluster_t *cluster;
	
	/* Get shared memory */
	cluster = pgraft_core_get_shared_memory();
	if (cluster)
	{
		SpinLockAcquire(&cluster->mutex);
		if (cluster->initialized)
		{
			cluster->initialized = false;
			elog(INFO, "pgraft: core system cleaned up");
		}
		SpinLockRelease(&cluster->mutex);
	}
}

/*
 * Initialize shared memory
 */
void
pgraft_core_init_shared_memory(void)
{
	pgraft_cluster_t *cluster;
	bool		found;
	
	elog(INFO, "pgraft: initializing shared memory");
	
	/* Get the shared memory pointer (should already be allocated) */
	cluster = (pgraft_cluster_t *) ShmemInitStruct("pgraft_cluster",
												   sizeof(pgraft_cluster_t),
												   &found);
	
	if (cluster)
	{
		if (!found)
		{
			elog(INFO, "pgraft: creating new shared memory");
			
			/* Initialize shared memory */
			memset(cluster, 0, sizeof(pgraft_cluster_t));
			
			/* Initialize mutex */
			SpinLockInit(&cluster->mutex);
			
			/* Initialize default values */
			cluster->initialized = false;
			cluster->node_id = -1;
			cluster->current_term = 0;
			cluster->leader_id = -1;
			strncpy(cluster->state, "stopped", sizeof(cluster->state) - 1);
			cluster->state[sizeof(cluster->state) - 1] = '\0';
			cluster->num_nodes = 0;
			cluster->messages_processed = 0;
			cluster->heartbeats_sent = 0;
			cluster->elections_triggered = 0;
			
			elog(INFO, "pgraft: shared memory initialized");
		}
		else
		{
			elog(INFO, "pgraft: shared memory already exists");
		}
	}
	else
	{
		elog(ERROR, "pgraft: failed to get shared memory pointer");
	}
}

/*
 * Get shared memory pointer
 */
pgraft_cluster_t *
pgraft_core_get_shared_memory(void)
{
	static pgraft_cluster_t *cluster = NULL;
	bool		found;
	
	if (cluster == NULL)
	{
		/* Get the shared memory pointer */
		cluster = (pgraft_cluster_t *) ShmemInitStruct("pgraft_cluster",
													   sizeof(pgraft_cluster_t),
													   &found);
		if (!found)
		{
			/* Initialize shared memory when first accessed */
			pgraft_core_init_shared_memory();
		}
	}
	return cluster;
}
