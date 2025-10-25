/*
 * pgraft_go_apply.c
 * Callback bridge from Go to C for applying Raft entries
 *
 * This file provides the C-side callback that Go can call to apply
 * committed Raft entries to PostgreSQL.
 */

#include "postgres.h"
#include "../include/pgraft_apply.h"

/*
 * This function is called from Go (pgraft Go library) when a Raft entry is committed
 * It's exported so Go can find it via dlsym or direct linking
 */
int
pgraft_go_apply_callback(uint64_t raft_index, const char *data, size_t len)
{
	/* Call the application layer to apply to PostgreSQL */
	return pgraft_apply_entry_to_postgres(raft_index, data, len);
}

/*
 * Register this callback with the Go library
 * Called during extension initialization
 */
void
pgraft_register_apply_callback(void)
{
	/* The Go library will call pgraft_go_apply_callback directly */
	elog(LOG, "pgraft: apply callback registered");
}

