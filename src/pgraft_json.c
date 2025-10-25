/*
 * pgraft_json.c
 * JSON parsing utilities using json-c library
 * Separate compilation unit to avoid naming conflicts
 */

#include "postgres.h"
#include <json-c/json.h>
#include "utils/timestamp.h"

#include "../include/pgraft_core.h"
#include "../include/pgraft_apply.h"
#include "../include/pgraft_kv.h"
#include "../include/pgraft_json.h"

/*
 * Parse nodes JSON from Go layer
 * Returns number of nodes parsed, -1 on error
 */
int
pgraft_parse_nodes_json(const char *nodes_json, int32_t *node_ids, char **addresses, int max_nodes)
{
	struct json_object *root;
	int node_count = 0;
	int i;
	int array_len;
	
	if (!nodes_json || !node_ids || !addresses || max_nodes <= 0)
		return -1;
	
	/* Parse JSON */
	root = json_tokener_parse(nodes_json);
	if (!root || !json_object_is_type(root, json_type_array))
	{
		if (root) json_object_put(root);
		return -1;
	}
	
	array_len = json_object_array_length(root);
	
	/* Parse each node in the array */
	for (i = 0; i < array_len && node_count < max_nodes; i++)
	{
		struct json_object *node_obj;
		struct json_object *id_obj;
		struct json_object *addr_obj;
		uint64_t node_id = 0;
		
		node_obj = json_object_array_get_idx(root, i);
		if (!node_obj || !json_object_is_type(node_obj, json_type_object))
			continue;
		
		/* Get id field */
		if (json_object_object_get_ex(node_obj, "id", &id_obj) && 
			json_object_is_type(id_obj, json_type_int))
		{
			node_id = json_object_get_int64(id_obj);
		}
		
		/* Get address field */
		if (json_object_object_get_ex(node_obj, "address", &addr_obj) && 
			json_object_is_type(addr_obj, json_type_string))
		{
			const char *address = json_object_get_string(addr_obj);
			if (address && node_id > 0)
			{
				node_ids[node_count] = (int32_t)node_id;
				strncpy(addresses[node_count], address, 255);
				addresses[node_count][255] = '\0';
				node_count++;
			}
		}
	}
	
	json_object_put(root);
	return node_count;
}

/*
 * Parse KV JSON entry from Raft log
 * Returns parsed entry or NULL on error
 */
PgRaftLogEntry *
pgraft_parse_kv_json_entry(const char *data, size_t len)
{
	PgRaftLogEntry *entry;
	struct json_object *json_obj;
	struct json_object *type_obj, *key_obj, *value_obj;
	const char *type_str, *key_str, *value_str;
	char	   *sql_cmd;
	size_t		sql_len;
	
	elog(LOG, "pgraft: parsing JSON entry: %.*s", (int)len, data);
	
	/* Parse JSON using json-c library */
	json_obj = json_tokener_parse(data);
	if (json_obj == NULL) {
		elog(WARNING, "pgraft: failed to parse JSON entry");
		return NULL;
	}
	
	/* Check if it's a JSON object */
	if (!json_object_is_type(json_obj, json_type_object)) {
		elog(WARNING, "pgraft: JSON entry is not an object");
		json_object_put(json_obj);
		return NULL;
	}
	
	/* Get the type field */
	if (!json_object_object_get_ex(json_obj, "type", &type_obj)) {
		elog(WARNING, "pgraft: no 'type' field found in JSON entry");
		json_object_put(json_obj);
		return NULL;
	}
	
	if (!json_object_is_type(type_obj, json_type_string)) {
		elog(WARNING, "pgraft: 'type' field is not a string");
		json_object_put(json_obj);
		return NULL;
	}
	
	type_str = json_object_get_string(type_obj);
	
	/* Allocate entry structure */
	entry = (PgRaftLogEntry *) palloc0(sizeof(PgRaftLogEntry));
	
	/* Parse based on type */
	if (strcmp(type_str, "kv_put") == 0) {
		/* Parse key and value for PUT operation */
		if (!json_object_object_get_ex(json_obj, "key", &key_obj) ||
			!json_object_object_get_ex(json_obj, "value", &value_obj)) {
			elog(WARNING, "pgraft: missing key or value in kv_put JSON entry");
			json_object_put(json_obj);
			pfree(entry);
			return NULL;
		}
		
		if (!json_object_is_type(key_obj, json_type_string) ||
			!json_object_is_type(value_obj, json_type_string)) {
			elog(WARNING, "pgraft: key or value is not a string in kv_put JSON entry");
			json_object_put(json_obj);
			pfree(entry);
			return NULL;
		}
		
		key_str = json_object_get_string(key_obj);
		value_str = json_object_get_string(value_obj);
		
		/* Build SQL command */
		sql_len = strlen(key_str) + strlen(value_str) + 100;
		sql_cmd = palloc(sql_len);
		snprintf(sql_cmd, sql_len, "SELECT pgraft_kv_put_local('%s', '%s')", key_str, value_str);
		
		/* Set up entry */
		entry->op = 1; /* PUT operation */
		strncpy(entry->database, "postgres", sizeof(entry->database) - 1);
		strncpy(entry->schema, "public", sizeof(entry->schema) - 1);
		strncpy(entry->sql, sql_cmd, sizeof(entry->sql) - 1);
		
		elog(LOG, "pgraft: parsed kv_put operation: key=%s, value=%s, sql=%s", key_str, value_str, sql_cmd);
		
		pfree(sql_cmd);
		
	} else if (strcmp(type_str, "kv_delete") == 0) {
		/* Parse key for DELETE operation */
		if (!json_object_object_get_ex(json_obj, "key", &key_obj)) {
			elog(WARNING, "pgraft: missing key in kv_delete JSON entry");
			json_object_put(json_obj);
			pfree(entry);
			return NULL;
		}
		
		if (!json_object_is_type(key_obj, json_type_string)) {
			elog(WARNING, "pgraft: key is not a string in kv_delete JSON entry");
			json_object_put(json_obj);
			pfree(entry);
			return NULL;
		}
		
		key_str = json_object_get_string(key_obj);
		
		/* Build SQL command */
		sql_len = strlen(key_str) + 100;
		sql_cmd = palloc(sql_len);
		snprintf(sql_cmd, sql_len, "SELECT pgraft_kv_delete_local('%s')", key_str);
		
		/* Set up entry */
		entry->op = 2; /* DELETE operation */
		strncpy(entry->database, "postgres", sizeof(entry->database) - 1);
		strncpy(entry->schema, "public", sizeof(entry->schema) - 1);
		strncpy(entry->sql, sql_cmd, sizeof(entry->sql) - 1);
		
		elog(LOG, "pgraft: parsed kv_delete operation: key=%s, sql=%s", key_str, sql_cmd);
		
		pfree(sql_cmd);
		
	} else {
		elog(WARNING, "pgraft: unknown operation type in JSON entry: %s", type_str);
		json_object_put(json_obj);
		pfree(entry);
		return NULL;
	}
	
	/* Clean up JSON object */
	json_object_put(json_obj);
	
	return entry;
}

/*
 * Create KV operation JSON using json-c library
 */
int
pgraft_json_create_kv_operation(pgraft_kv_op_type_t op_type, const char *key, const char *value, const char *client_id, char *json_buffer, size_t buffer_size)
{
	json_object *json_obj;
	json_object *type_obj;
	json_object *key_obj;
	json_object *value_obj;
	json_object *timestamp_obj;
	json_object *client_id_obj;
	const char *json_string;
	int64_t timestamp;
	size_t json_len;
	
	/* Create JSON object */
	json_obj = json_object_new_object();
	if (!json_obj) {
		elog(ERROR, "pgraft_json: failed to create JSON object");
		return -1;
	}
	
	/* Set operation type */
	if (op_type == PGRAFT_KV_PUT) {
		type_obj = json_object_new_string("kv_put");
	} else if (op_type == PGRAFT_KV_DELETE) {
		type_obj = json_object_new_string("kv_delete");
	} else {
		elog(ERROR, "pgraft_json: unknown KV operation type: %d", op_type);
		json_object_put(json_obj);
		return -1;
	}
	json_object_object_add(json_obj, "type", type_obj);
	
	/* Set key */
	key_obj = json_object_new_string(key);
	json_object_object_add(json_obj, "key", key_obj);
	
	/* Set value (only for PUT operations) */
	if (op_type == PGRAFT_KV_PUT && value) {
		value_obj = json_object_new_string(value);
		json_object_object_add(json_obj, "value", value_obj);
	}
	
	/* Set timestamp */
	timestamp = GetCurrentTimestamp();
	timestamp_obj = json_object_new_int64(timestamp);
	json_object_object_add(json_obj, "timestamp", timestamp_obj);
	
	/* Set client ID */
	client_id_obj = json_object_new_string(client_id);
	json_object_object_add(json_obj, "client_id", client_id_obj);
	
	/* Convert to string */
	json_string = json_object_to_json_string(json_obj);
	
	if (!json_string) {
		elog(ERROR, "pgraft_json: failed to convert JSON to string");
		json_object_put(json_obj);
		return -1;
	}
	
	/* Copy to buffer with bounds checking */
	json_len = strlen(json_string);
	if (json_len >= buffer_size) {
		elog(ERROR, "pgraft_json: KV operation JSON string too long for buffer (len=%zu, max=%zu)", json_len, buffer_size - 1);
		json_object_put(json_obj);
		return -1;
	}
	strncpy(json_buffer, json_string, buffer_size - 1);
	json_buffer[buffer_size - 1] = '\0'; /* Ensure null termination */
	
	/* Clean up */
	json_object_put(json_obj);
	
	return 0;
}

/*
 * Parse KV operation from JSON using json-c library
 */
int
pgraft_json_parse_kv_operation(const char *json_data, size_t len, int *op_type, char **key, char **value)
{
	json_object *json_obj;
	json_object *type_obj;
	json_object *key_obj;
	json_object *value_obj;
	const char *type_str;
	const char *key_str;
	const char *value_str;
	
	/* Parse JSON */
	json_obj = json_tokener_parse(json_data);
	if (!json_obj) {
		elog(ERROR, "pgraft_json: failed to parse JSON");
		return -1;
	}
	
	/* Extract type */
	if (!json_object_object_get_ex(json_obj, "type", &type_obj)) {
		elog(ERROR, "pgraft_json: missing 'type' field in JSON");
		json_object_put(json_obj);
		return -1;
	}
	type_str = json_object_get_string(type_obj);
	
	/* Extract key */
	if (!json_object_object_get_ex(json_obj, "key", &key_obj)) {
		elog(ERROR, "pgraft_json: missing 'key' field in JSON");
		json_object_put(json_obj);
		return -1;
	}
	key_str = json_object_get_string(key_obj);
	
	/* Determine operation type */
	if (strcmp(type_str, "kv_put") == 0) {
		*op_type = PGRAFT_KV_PUT;
		
		/* Extract value for PUT operations */
		if (!json_object_object_get_ex(json_obj, "value", &value_obj)) {
			elog(ERROR, "pgraft_json: missing 'value' field in PUT operation");
			json_object_put(json_obj);
			return -1;
		}
		value_str = json_object_get_string(value_obj);
		*value = pstrdup(value_str);
	} else if (strcmp(type_str, "kv_delete") == 0) {
		*op_type = PGRAFT_KV_DELETE;
		*value = NULL; /* DELETE operations don't have values */
	} else {
		elog(ERROR, "pgraft_json: unknown operation type: %s", type_str);
		json_object_put(json_obj);
		return -1;
	}
	
	/* Copy key */
	*key = pstrdup(key_str);
	
	/* Clean up */
	json_object_put(json_obj);
	
	return 0;
}

/*
 * Create KV stats JSON using json-c library
 */
int
pgraft_json_create_kv_stats(pgraft_kv_store_t *stats, char *json_buffer, size_t buffer_size)
{
	json_object *json_obj;
	json_object *num_entries_obj;
	json_object *total_ops_obj;
	json_object *last_applied_obj;
	json_object *puts_obj;
	json_object *deletes_obj;
	json_object *gets_obj;
	const char *json_string;
	
	/* Create JSON object */
	json_obj = json_object_new_object();
	if (!json_obj) {
		elog(ERROR, "pgraft_json: failed to create JSON object");
		return -1;
	}
	
	/* Add all fields */
	num_entries_obj = json_object_new_int(stats->num_entries);
	json_object_object_add(json_obj, "num_entries", num_entries_obj);
	
	total_ops_obj = json_object_new_int64(stats->total_operations);
	json_object_object_add(json_obj, "total_operations", total_ops_obj);
	
	last_applied_obj = json_object_new_int64(stats->last_applied_index);
	json_object_object_add(json_obj, "last_applied_index", last_applied_obj);
	
	puts_obj = json_object_new_int64(stats->puts);
	json_object_object_add(json_obj, "puts", puts_obj);
	
	deletes_obj = json_object_new_int64(stats->deletes);
	json_object_object_add(json_obj, "deletes", deletes_obj);
	
	gets_obj = json_object_new_int64(stats->gets);
	json_object_object_add(json_obj, "gets", gets_obj);
	
	/* Convert to string */
	json_string = json_object_to_json_string(json_obj);
	if (!json_string) {
		elog(ERROR, "pgraft_json: failed to convert JSON to string");
		json_object_put(json_obj);
		return -1;
	}
	
	/* Copy to buffer */
	if (strlen(json_string) >= buffer_size) {
		elog(ERROR, "pgraft_json: JSON string too long for buffer");
		json_object_put(json_obj);
		return -1;
	}
	strcpy(json_buffer, json_string);
	
	/* Clean up */
	json_object_put(json_obj);
	
	return 0;
}

/*
 * Create key list JSON array using json-c library
 */
int
pgraft_json_create_key_list(pgraft_kv_store_t *store, char *json_buffer, size_t buffer_size)
{
	json_object *json_array;
	json_object *key_obj;
	int i;
	const char *json_string;
	
	/* Create JSON array */
	json_array = json_object_new_array();
	if (!json_array) {
		elog(ERROR, "pgraft_json: failed to create JSON array");
		return -1;
	}
	
	/* Add all non-deleted keys to array */
	for (i = 0; i < store->num_entries; i++) {
		if (!store->entries[i].deleted) {
			key_obj = json_object_new_string(store->entries[i].key);
			json_object_array_add(json_array, key_obj);
		}
	}
	
	/* Convert to string */
	json_string = json_object_to_json_string(json_array);
	if (!json_string) {
		elog(ERROR, "pgraft_json: failed to convert JSON array to string");
		json_object_put(json_array);
		return -1;
	}
	
	/* Copy to buffer */
	if (strlen(json_string) >= buffer_size) {
		elog(ERROR, "pgraft_json: JSON array too long for buffer");
		json_object_put(json_array);
		return -1;
	}
	strcpy(json_buffer, json_string);
	
	/* Clean up */
	json_object_put(json_array);
	
	return 0;
}

/*
 * Parse log entry from JSON using json-c library
 */
PgRaftLogEntry *
pgraft_json_parse_log_entry(const char *json_data, size_t len)
{
	/* For now, just return NULL since we're focusing on KV operations */
	/* This can be implemented later if needed for general SQL operations */
	return NULL;
}
