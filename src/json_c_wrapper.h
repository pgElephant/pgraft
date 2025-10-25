/*
 * json_c_wrapper.h
 * Wrapper to avoid naming conflicts between json-c and PostgreSQL
 */

#ifndef JSON_C_WRAPPER_H
#define JSON_C_WRAPPER_H

/* Include json-c headers */
#include <json-c/json.h>

/* Define type alias to avoid conflict with PostgreSQL's json_object function */
typedef struct json_object json_object_c;

/* Define convenience macros that use the original function names */
#define json_object_tokener_parse(s) json_tokener_parse(s)
#define json_object_is_type(obj, type) json_object_is_type(obj, type)
#define json_object_array_length(obj) json_object_array_length(obj)
#define json_object_array_get_idx(obj, idx) json_object_array_get_idx(obj, idx)
#define json_object_object_get_ex(obj, key, val) json_object_object_get_ex(obj, key, val)
#define json_object_get_int64(obj) json_object_get_int64(obj)
#define json_object_get_string(obj) json_object_get_string(obj)
#define json_object_put(obj) json_object_put(obj)

#endif /* JSON_C_WRAPPER_H */
