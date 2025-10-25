/*
 * pgraft_json.h
 * JSON parsing utilities using json-c library
 */

#ifndef PGRAFT_JSON_H
#define PGRAFT_JSON_H

#include "postgres.h"
#include "pgraft_kv.h"

/* Forward declarations */
typedef struct PgRaftLogEntry PgRaftLogEntry;

/* Parse nodes JSON from Go layer */
int pgraft_parse_nodes_json(const char *nodes_json, int32_t *node_ids, char **addresses, int max_nodes);

/* Parse KV JSON entry from Raft log */
PgRaftLogEntry *pgraft_parse_kv_json_entry(const char *data, size_t len);

/* Create KV operation JSON using json-c library */
int pgraft_json_create_kv_operation(pgraft_kv_op_type_t op_type, const char *key, const char *value, const char *client_id, char *json_buffer, size_t buffer_size);

/* Parse KV operation from JSON using json-c library */
int pgraft_json_parse_kv_operation(const char *json_data, size_t len, int *op_type, char **key, char **value);

/* Create KV stats JSON using json-c library */
int pgraft_json_create_kv_stats(pgraft_kv_store_t *stats, char *json_buffer, size_t buffer_size);

/* Create key list JSON array using json-c library */
int pgraft_json_create_key_list(pgraft_kv_store_t *store, char *json_buffer, size_t buffer_size);

/* Parse log entry from JSON using json-c library */
PgRaftLogEntry *pgraft_json_parse_log_entry(const char *json_data, size_t len);

#endif /* PGRAFT_JSON_H */
