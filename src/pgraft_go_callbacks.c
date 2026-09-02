/*
 * pgraft_go_callbacks.c
 * C callbacks that can be called from Go library
 *
 * These functions are exported so the Go library can call them to interact
 * with the pgraft extension.
 *
 * Both are declared PGDLLEXPORT.  The extension is linked with
 * -fvisibility=hidden, so without it these symbols are NOT exported from the
 * module at all: the Go library is built with -Wl,-undefined,dynamic_lookup on
 * macOS and binds them lazily from the global namespace, so the call resolves
 * to address 0 and segfaults on first use.
 *
 * IMPORTANT: everything reachable from here may run on a Go-managed thread
 * rather than on the PostgreSQL background worker's main thread.  That means
 * NO elog()/ereport() at any level, no palloc, no LWLocks, and no
 * ShmemInitStruct().  Keep these shims thin and keep the functions they call
 * (see pgraft_enqueue_apply_entry) free of all of the above.
 */

#include "postgres.h"
#include "../include/pgraft_core.h"
#include "../include/pgraft_apply.h"
#include "../include/pgraft_kv.h"

/*
 * Callback from Go when a Raft entry is committed.
 * Enqueues the entry for application by the background worker.
 *
 * EXPORTED FOR GO. Runs on a Go thread: must not log or allocate.
 */
PGDLLEXPORT int
pgraft_enqueue_for_apply_from_go(unsigned long long raft_index, const char *data, unsigned long data_len)
{
	if (pgraft_enqueue_apply_entry((uint64) raft_index, data, (size_t) data_len))
		return 0;				/* Success */

	return -1;					/* Failure (counted, not logged, by the callee) */
}

/*
 * Highest Raft index already applied to this node's state machine, read from
 * the persisted key/value store.  The Go layer uses this to set raft's
 * Config.Applied so that a restart does not redeliver - and re-apply - the
 * entire committed history.
 *
 * EXPORTED FOR GO.  Called once during initialization, from the same thread
 * that invoked pgraft_go_init_config(), so the spinlock read inside
 * pgraft_kv_get_last_applied_index() is safe here.
 */
PGDLLEXPORT unsigned long long
pgraft_applied_index_from_go(void)
{
	int64_t		applied = pgraft_kv_get_last_applied_index();

	if (applied < 0)
		return 0;

	return (unsigned long long) applied;
}
