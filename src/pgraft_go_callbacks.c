/*
 * pgraft_go_callbacks.c
 * C callbacks that can be called from Go library
 *
 * These functions are exported so the Go library can call them
 * to interact with PostgreSQL (e.g., apply Raft entries)
 */

#include "postgres.h"
#include "../include/pgraft_core.h"
#include "../include/pgraft_apply.h"

/*
 * Callback from Go when a Raft entry is committed
 * This enqueues the entry for application by the background worker
 * 
 * EXPORTED FOR GO: This function is called from pgraft Go library
 */
int
pgraft_enqueue_for_apply_from_go(unsigned long long raft_index, const char *data, unsigned long data_len)
{
	/* Call our C function to enqueue the entry */
	if (pgraft_enqueue_apply_entry((uint64) raft_index, data, (size_t) data_len)) {
		return 0; /* Success */
	}
	return -1; /* Failure */
}

