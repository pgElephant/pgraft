#ifndef PGRAFT_SQL_H
#define PGRAFT_SQL_H

#include "postgres.h"
#include "fmgr.h"

/* Internal initialization function (callable from worker) */
int			pgraft_init_from_gucs(void);

/* SQL function declarations */
Datum		pgraft_init(PG_FUNCTION_ARGS);
Datum		pgraft_init_guc(PG_FUNCTION_ARGS);
Datum		pgraft_add_node(PG_FUNCTION_ARGS);
Datum		pgraft_remove_node(PG_FUNCTION_ARGS);
Datum		pgraft_get_cluster_status_table(PG_FUNCTION_ARGS);
Datum		pgraft_get_leader(PG_FUNCTION_ARGS);
Datum		pgraft_get_term(PG_FUNCTION_ARGS);
Datum		pgraft_is_leader(PG_FUNCTION_ARGS);
Datum		pgraft_get_nodes_table(PG_FUNCTION_ARGS);
Datum		pgraft_get_version(PG_FUNCTION_ARGS);
Datum		pgraft_test(PG_FUNCTION_ARGS);
Datum		pgraft_set_debug(PG_FUNCTION_ARGS);
Datum		pgraft_get_worker_state(PG_FUNCTION_ARGS);
Datum		pgraft_get_queue_status(PG_FUNCTION_ARGS);

/* Log replication SQL functions */
Datum		pgraft_log_append(PG_FUNCTION_ARGS);
Datum		pgraft_log_commit(PG_FUNCTION_ARGS);
Datum		pgraft_log_apply(PG_FUNCTION_ARGS);
Datum		pgraft_log_get_entry_sql(PG_FUNCTION_ARGS);
Datum		pgraft_log_get_stats_table(PG_FUNCTION_ARGS);
Datum		pgraft_log_get_replication_status_table(PG_FUNCTION_ARGS);
Datum		pgraft_log_sync_with_leader_sql(PG_FUNCTION_ARGS);

#endif
