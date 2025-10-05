/*
 * pgraft_json.h
 * JSON parsing utilities using json-c library
 */

#ifndef PGRAFT_JSON_H
#define PGRAFT_JSON_H

#include "postgres.h"

/* Forward declaration */
typedef struct PgRaftLogEntry PgRaftLogEntry;

/* Parse nodes JSON from Go layer */
int pgraft_parse_nodes_json(const char *nodes_json, int32_t *node_ids, char **addresses, int max_nodes);

/* Parse KV JSON entry from Raft log */
PgRaftLogEntry *pgraft_parse_kv_json_entry(const char *data, size_t len);

#endif /* PGRAFT_JSON_H */
