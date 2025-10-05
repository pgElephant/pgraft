#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/timestamp.h"
#include "miscadmin.h"
#include "storage/proc.h"
#include "access/tupdesc.h"
#include "funcapi.h"
#include "../include/pgraft_kv.h"
#include "../include/pgraft_core.h"
#include "../include/pgraft_json.h"

/* Prototypes for PostgreSQL functions */

/* Function information for PostgreSQL */
PG_FUNCTION_INFO_V1(pgraft_kv_put_sql);
PG_FUNCTION_INFO_V1(pgraft_kv_get_sql);
PG_FUNCTION_INFO_V1(pgraft_kv_delete_sql);
PG_FUNCTION_INFO_V1(pgraft_kv_exists_sql);
PG_FUNCTION_INFO_V1(pgraft_kv_list_keys_sql);
PG_FUNCTION_INFO_V1(pgraft_kv_stats_sql);
PG_FUNCTION_INFO_V1(pgraft_kv_get_stats_table);
PG_FUNCTION_INFO_V1(pgraft_kv_compact_sql);
PG_FUNCTION_INFO_V1(pgraft_kv_reset_sql);

/* Helper function to replicate KV operations through Raft */
static int
replicate_kv_operation(pgraft_kv_op_type_t op_type, const char *key, const char *value)
{
	char json_data[2048];
	int result;
	char *client_id;
	
	/* Create client ID */
	client_id = psprintf("pg_%d", MyProcPid);
	
	/* Create JSON using json-c library */
	if (pgraft_json_create_kv_operation(op_type, key, value, client_id, json_data, sizeof(json_data)) != 0) {
		elog(ERROR, "pgraft_kv: failed to create JSON for KV operation");
		pfree(client_id);
		return -1;
	}
	
	/* Replicate through Raft using the correct function */
	if (op_type == PGRAFT_KV_PUT) {
		result = pgraft_kv_replicate_put(key, value, client_id);
	} else if (op_type == PGRAFT_KV_DELETE) {
		result = pgraft_kv_replicate_delete(key, client_id);
	} else {
		elog(ERROR, "pgraft_kv: unsupported operation type: %d", op_type);
		pfree(client_id);
		return -1;
	}
	if (result != 0) {
		elog(WARNING, "pgraft_kv: failed to replicate operation (error: %d)", result);
		pfree(client_id);
		return -1;
	}
	
	pfree(client_id);
	return 0;
}

/*
 * PUT operation - store a key/value pair
 * Usage: SELECT pgraft_kv_put('mykey', 'myvalue');
 */
Datum
pgraft_kv_put_sql(PG_FUNCTION_ARGS)
{
	text *key_text;
	text *value_text;
	char *key;
	char *value;
	int result;
	
	/* Handle NULL values properly - check before getting arguments */
	if (PG_ARGISNULL(0)) {
		elog(WARNING, "pgraft_kv: key cannot be NULL");
		PG_RETURN_BOOL(false);
	}
	
	if (PG_ARGISNULL(1)) {
		elog(WARNING, "pgraft_kv: value cannot be NULL");
		PG_RETURN_BOOL(false);
	}
	
	/* Now safe to get the arguments */
	key_text = PG_GETARG_TEXT_PP(0);
	value_text = PG_GETARG_TEXT_PP(1);
	
	key = text_to_cstring(key_text);
	value = text_to_cstring(value_text);
	
	elog(INFO, "pgraft_kv: PUT operation: key='%s', value='%s'", key, value);
	
	/* Validate inputs with comprehensive error checking */
	if (strlen(key) == 0) {
		elog(WARNING, "pgraft_kv: key cannot be empty");
		PG_RETURN_BOOL(false);
	}
	
	if (strlen(key) >= 256) {
		elog(WARNING, "pgraft_kv: key too long (max 255 characters, got %zu)", strlen(key));
		PG_RETURN_BOOL(false);
	}
	
	if (strlen(value) >= 1024) {
		elog(WARNING, "pgraft_kv: value too long (max 1023 characters, got %zu)", strlen(value));
		PG_RETURN_BOOL(false);
	}
	
	/* Check for invalid characters in key */
	if (strpbrk(key, "\0\r\n\t")) {
		elog(WARNING, "pgraft_kv: key contains invalid characters (null, newline, tab, or carriage return)");
		PG_RETURN_BOOL(false);
	}
	
	/* Replicate the operation */
	result = replicate_kv_operation(PGRAFT_KV_PUT, key, value);
	
	pfree(key);
	pfree(value);
	
	PG_RETURN_BOOL(result == 0);
}

/*
 * GET operation - retrieve value for a key
 * Usage: SELECT pgraft_kv_get('mykey');
 */
Datum
pgraft_kv_get_sql(PG_FUNCTION_ARGS)
{
	text *key_text;
	char *key;
	char value[1024];
	int64_t version;
	int result;
	
	/* Handle NULL values properly - check before getting arguments */
	if (PG_ARGISNULL(0)) {
		elog(WARNING, "pgraft_kv: key cannot be NULL");
		PG_RETURN_NULL();
	}
	
	/* Now safe to get the argument */
	key_text = PG_GETARG_TEXT_PP(0);
	key = text_to_cstring(key_text);
	
	elog(INFO, "pgraft_kv: GET operation: key='%s'", key);
	
	/* Validate inputs */
	if (strlen(key) == 0) {
		elog(WARNING, "pgraft_kv: key cannot be empty");
		PG_RETURN_NULL();
	}
	
	/* Get the value */
	result = pgraft_kv_get(key, value, sizeof(value), &version);
	
	pfree(key);
	
	if (result == 0) {
		PG_RETURN_TEXT_P(cstring_to_text(value));
	} else {
		PG_RETURN_NULL();
	}
}

/*
 * DELETE operation - delete a key
 * Usage: SELECT pgraft_kv_delete('mykey');
 */
Datum
pgraft_kv_delete_sql(PG_FUNCTION_ARGS)
{
	text *key_text;
	char *key;
	int result;
	
	/* Handle NULL values properly - check before getting arguments */
	if (PG_ARGISNULL(0)) {
		elog(WARNING, "pgraft_kv: key cannot be NULL");
		PG_RETURN_BOOL(false);
	}
	
	/* Now safe to get the argument */
	key_text = PG_GETARG_TEXT_PP(0);
	key = text_to_cstring(key_text);
	
	elog(INFO, "pgraft_kv: DELETE operation: key='%s'", key);
	
	/* Validate inputs with comprehensive error checking */
	if (strlen(key) == 0) {
		elog(WARNING, "pgraft_kv: key cannot be empty");
		PG_RETURN_BOOL(false);
	}
	
	if (strlen(key) >= 256) {
		elog(WARNING, "pgraft_kv: key too long (max 255 characters, got %zu)", strlen(key));
		PG_RETURN_BOOL(false);
	}
	
	/* Check for invalid characters in key */
	if (strpbrk(key, "\0\r\n\t")) {
		elog(WARNING, "pgraft_kv: key contains invalid characters (null, newline, tab, or carriage return)");
		PG_RETURN_BOOL(false);
	}
	
	/* Replicate the operation */
	result = replicate_kv_operation(PGRAFT_KV_DELETE, key, NULL);
	
	pfree(key);
	
	PG_RETURN_BOOL(result == 0);
}

/*
 * EXISTS operation - check if key exists
 * Usage: SELECT pgraft_kv_exists('mykey');
 */
Datum
pgraft_kv_exists_sql(PG_FUNCTION_ARGS)
{
	text *key_text;
	char *key;
	bool exists;
	
	/* Handle NULL values properly - check before getting arguments */
	if (PG_ARGISNULL(0)) {
		elog(WARNING, "pgraft_kv: key cannot be NULL");
		PG_RETURN_BOOL(false);
	}
	
	/* Now safe to get the argument */
	key_text = PG_GETARG_TEXT_PP(0);
	key = text_to_cstring(key_text);
	
	elog(INFO, "pgraft_kv: EXISTS operation: key='%s'", key);
	
	/* Validate inputs */
	if (strlen(key) == 0) {
		elog(WARNING, "pgraft_kv: key cannot be empty");
		PG_RETURN_BOOL(false);
	}
	
	/* Check existence */
	exists = pgraft_kv_exists(key);
	
	pfree(key);
	
	PG_RETURN_BOOL(exists);
}

/*
 * LIST_KEYS operation - list all keys as JSON array
 * Usage: SELECT pgraft_kv_list_keys();
 */
Datum
pgraft_kv_list_keys_sql(PG_FUNCTION_ARGS)
{
	char keys_json[8192];
	
	elog(INFO, "pgraft_kv: LIST_KEYS operation");
	
	/* Get keys as JSON */
	pgraft_kv_list_keys(keys_json, sizeof(keys_json));
	
	PG_RETURN_TEXT_P(cstring_to_text(keys_json));
}

/*
 * Get key/value store statistics as JSON
 * Usage: SELECT pgraft_kv_stats();
 */
Datum
pgraft_kv_stats_sql(PG_FUNCTION_ARGS)
{
	pgraft_kv_store_t stats;
	char stats_json[2048];
	int result;
	
	elog(INFO, "pgraft_kv: STATS operation");
	
	/* Get statistics */
	result = pgraft_kv_get_stats(&stats);
	if (result != 0) {
		elog(ERROR, "pgraft_kv: failed to get statistics");
		PG_RETURN_NULL();
	}
	
	/* Build JSON response using json-c library */
	if (pgraft_json_create_kv_stats(&stats, stats_json, sizeof(stats_json)) != 0) {
		elog(ERROR, "pgraft_kv: failed to create JSON stats");
		PG_RETURN_NULL();
	}
	
	PG_RETURN_TEXT_P(cstring_to_text(stats_json));
}

/*
 * Get key/value store statistics as table
 * Usage: SELECT * FROM pgraft_kv_get_stats();
 */
Datum
pgraft_kv_get_stats_table(PG_FUNCTION_ARGS)
{
	pgraft_kv_store_t *store;
	TupleDesc tupdesc;
	Datum values[8];
	bool nulls[8] = {false};
	HeapTuple tuple;
	int active_entries = 0, deleted_entries = 0;
	
	/* Get the store */
	store = pgraft_kv_get_store();
	if (!store) {
		elog(ERROR, "pgraft_kv: key/value store not initialized");
		PG_RETURN_NULL();
	}
	
	/* Build tuple descriptor */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "pgraft_kv: return type must be a row type");
	
	/* Calculate active vs deleted entries */
	for (int i = 0; i < store->num_entries; i++) {
		if (store->entries[i].deleted)
			deleted_entries++;
		else
			active_entries++;
	}
	
	/* Prepare values */
	values[0] = Int32GetDatum(store->num_entries);
	values[1] = Int64GetDatum(store->total_operations);
	values[2] = Int64GetDatum(store->last_applied_index);
	values[3] = Int64GetDatum(store->puts);
	values[4] = Int64GetDatum(store->deletes);
	values[5] = Int64GetDatum(store->gets);
	values[6] = Int32GetDatum(active_entries);
	values[7] = Int32GetDatum(deleted_entries);
	
	/* Build and return tuple */
	tuple = heap_form_tuple(tupdesc, values, nulls);
	PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

/*
 * COMPACT operation - remove deleted entries and optimize storage
 * Usage: SELECT pgraft_kv_compact();
 */
Datum
pgraft_kv_compact_sql(PG_FUNCTION_ARGS)
{
	elog(INFO, "pgraft_kv: COMPACT operation");
	
	/* Perform compaction */
	pgraft_kv_compact();
	
	PG_RETURN_TEXT_P(cstring_to_text("Key/value store compacted successfully"));
}

/*
 * RESET operation - clear all key/value pairs
 * Usage: SELECT pgraft_kv_reset();
 */
Datum
pgraft_kv_reset_sql(PG_FUNCTION_ARGS)
{
	elog(INFO, "pgraft_kv: RESET operation");
	
	/* Perform reset */
	pgraft_kv_reset();
	
	PG_RETURN_TEXT_P(cstring_to_text("Key/value store reset successfully"));
}