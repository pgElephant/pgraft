/*
 * pgraft_json.c
 * JSON parsing utilities using json-c library
 * Separate compilation unit to avoid naming conflicts
 */

#include "postgres.h"
#include "/opt/homebrew/Cellar/json-c/0.18/include/json-c/json.h"

#include "../include/pgraft_core.h"
#include "../include/pgraft_apply.h"

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
	
	if (!nodes_json || !node_ids || !addresses || max_nodes <= 0)
		return -1;
	
	/* Parse JSON */
	root = json_tokener_parse(nodes_json);
	if (!root || !json_object_is_type(root, json_type_array))
	{
		if (root) json_object_put(root);
		return -1;
	}
	
	int array_len = json_object_array_length(root);
	
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
