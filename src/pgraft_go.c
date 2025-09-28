/*-------------------------------------------------------------------------
 *
 * pgraft_go.c
 *      Go library interface for pgraft
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "miscadmin.h"
#include "utils/elog.h"

#include <dlfcn.h>

#include "../include/pgraft_go.h"
#include "../include/pgraft_state.h"

/* Global variables */
static void *go_lib_handle = NULL;

/* Function pointers */
static pgraft_go_init_func pgraft_go_init_ptr = NULL;
static pgraft_go_start_func pgraft_go_start_ptr = NULL;
static pgraft_go_stop_func pgraft_go_stop_ptr = NULL;
static pgraft_go_add_peer_func pgraft_go_add_peer_ptr = NULL;
static pgraft_go_remove_peer_func pgraft_go_remove_peer_ptr = NULL;
static pgraft_go_get_leader_func pgraft_go_get_leader_ptr = NULL;
static pgraft_go_get_term_func pgraft_go_get_term_ptr = NULL;
static pgraft_go_is_leader_func pgraft_go_is_leader_ptr = NULL;
static pgraft_go_get_nodes_func pgraft_go_get_nodes_ptr = NULL;
static pgraft_go_version_func pgraft_go_version_ptr = NULL;
static pgraft_go_test_func pgraft_go_test_ptr = NULL;
static pgraft_go_set_debug_func pgraft_go_set_debug_ptr = NULL;
static pgraft_go_start_network_server_func pgraft_go_start_network_server_ptr = NULL;
static pgraft_go_free_string_func pgraft_go_free_string_ptr = NULL;
static pgraft_go_update_cluster_state_func pgraft_go_update_cluster_state_ptr = NULL;

/*
 * Load Go Raft library dynamically
 */
int
pgraft_go_load_library(void)
{
	char		lib_path[MAXPGPATH];
	const char *pg_libdir;
	
	/* Check if already loaded in this process */
	if (go_lib_handle != NULL)
		return 0;		/* Already loaded in this process */
	
	/* Check if library should be loaded (from shared memory) */
	if (pgraft_state_is_go_lib_loaded())
	{
		/* Library should be loaded, but not in this process - load it */
		elog(INFO, "pgraft: Go library marked as loaded in shared memory, loading in this process");
	}
	else
	{
		/* Library not marked as loaded in shared memory */
		elog(INFO, "pgraft: Go library not marked as loaded in shared memory");
	}
	
	/* Always proceed to load the library in this process */
	
	/* Get PostgreSQL library directory */
	pg_libdir = (char *) pkglib_path;
	snprintf(lib_path, sizeof(lib_path), "%s/pgraft_go.dylib", pg_libdir);
	
	elog(INFO, "pgraft: Loading Go library from %s", lib_path);
	
	/* Load the Go library */
	go_lib_handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);
	if (!go_lib_handle)
	{
		elog(ERROR, "pgraft: Failed to load Go library: %s", dlerror());
		return -1;
	}
	
	/* Load function pointers */
	pgraft_go_init_ptr = (pgraft_go_init_func) dlsym(go_lib_handle, "pgraft_go_init");
	pgraft_go_start_ptr = (pgraft_go_start_func) dlsym(go_lib_handle, "pgraft_go_start");
	pgraft_go_stop_ptr = (pgraft_go_stop_func) dlsym(go_lib_handle, "pgraft_go_stop");
	pgraft_go_add_peer_ptr = (pgraft_go_add_peer_func) dlsym(go_lib_handle, "pgraft_go_add_peer");
	pgraft_go_remove_peer_ptr = (pgraft_go_remove_peer_func) dlsym(go_lib_handle, "pgraft_go_remove_peer");
	pgraft_go_get_leader_ptr = (pgraft_go_get_leader_func) dlsym(go_lib_handle, "pgraft_go_get_leader");
	pgraft_go_get_term_ptr = (pgraft_go_get_term_func) dlsym(go_lib_handle, "pgraft_go_get_term");
	pgraft_go_is_leader_ptr = (pgraft_go_is_leader_func) dlsym(go_lib_handle, "pgraft_go_is_leader");
	pgraft_go_get_nodes_ptr = (pgraft_go_get_nodes_func) dlsym(go_lib_handle, "pgraft_go_get_nodes");
	pgraft_go_version_ptr = (pgraft_go_version_func) dlsym(go_lib_handle, "pgraft_go_version");
	pgraft_go_test_ptr = (pgraft_go_test_func) dlsym(go_lib_handle, "pgraft_go_test");
	pgraft_go_set_debug_ptr = (pgraft_go_set_debug_func) dlsym(go_lib_handle, "pgraft_go_set_debug");
	pgraft_go_start_network_server_ptr = (pgraft_go_start_network_server_func) dlsym(go_lib_handle, "pgraft_go_start_network_server");
	pgraft_go_free_string_ptr = (pgraft_go_free_string_func) dlsym(go_lib_handle, "pgraft_go_free_string");
	pgraft_go_update_cluster_state_ptr = (pgraft_go_update_cluster_state_func) dlsym(go_lib_handle, "pgraft_go_update_cluster_state");
	
	/* Check if all critical functions were loaded */
	if (!pgraft_go_init_ptr || !pgraft_go_start_ptr || !pgraft_go_stop_ptr)
	{
		elog(ERROR, "pgraft: Failed to load critical Go functions");
		dlclose(go_lib_handle);
		go_lib_handle = NULL;
		return -1;
	}
	
	/* Update shared memory state */
	pgraft_state_set_go_lib_loaded(true);
	
	elog(INFO, "pgraft: Go library loaded successfully");
	
	return 0;
}

/*
 * Unload Go Raft library
 */
void
pgraft_go_unload_library(void)
{
	if (go_lib_handle)
	{
		dlclose(go_lib_handle);
		go_lib_handle = NULL;
	}
	
	/* Reset function pointers */
	pgraft_go_init_ptr = NULL;
	pgraft_go_start_ptr = NULL;
	pgraft_go_stop_ptr = NULL;
	pgraft_go_add_peer_ptr = NULL;
	pgraft_go_remove_peer_ptr = NULL;
	pgraft_go_get_leader_ptr = NULL;
	pgraft_go_get_term_ptr = NULL;
	pgraft_go_is_leader_ptr = NULL;
	pgraft_go_get_nodes_ptr = NULL;
	pgraft_go_version_ptr = NULL;
	pgraft_go_test_ptr = NULL;
	pgraft_go_set_debug_ptr = NULL;
	pgraft_go_free_string_ptr = NULL;
	
	/* Update shared memory state */
	pgraft_state_set_go_lib_loaded(false);
	
	elog(INFO, "pgraft: Go library unloaded");
}

/*
 * Check if Go library is loaded
 */
bool
pgraft_go_is_loaded(void)
{
	/* Check if loaded in this process */
	if (go_lib_handle != NULL)
		return true;
	
	/* Check shared memory state */
	return pgraft_state_is_go_lib_loaded();
}

/* Function pointer accessors */
pgraft_go_init_func
pgraft_go_get_init_func(void)
{
	return pgraft_go_init_ptr;
}

pgraft_go_start_func
pgraft_go_get_start_func(void)
{
	elog(INFO, "pgraft: Returning pgraft_go_start_ptr");
	return pgraft_go_start_ptr;
}

pgraft_go_stop_func
pgraft_go_get_stop_func(void)
{
	return pgraft_go_stop_ptr;
}

pgraft_go_add_peer_func
pgraft_go_get_add_peer_func(void)
{
	return pgraft_go_add_peer_ptr;
}

pgraft_go_remove_peer_func
pgraft_go_get_remove_peer_func(void)
{
	return pgraft_go_remove_peer_ptr;
}

pgraft_go_get_leader_func
pgraft_go_get_get_leader_func(void)
{
	return pgraft_go_get_leader_ptr;
}

pgraft_go_get_term_func
pgraft_go_get_get_term_func(void)
{
	return pgraft_go_get_term_ptr;
}

pgraft_go_is_leader_func
pgraft_go_get_is_leader_func(void)
{
	return pgraft_go_is_leader_ptr;
}

pgraft_go_get_nodes_func
pgraft_go_get_get_nodes_func(void)
{
	return pgraft_go_get_nodes_ptr;
}

pgraft_go_version_func
pgraft_go_get_version_func(void)
{
	return pgraft_go_version_ptr;
}

pgraft_go_test_func
pgraft_go_get_test_func(void)
{
	return pgraft_go_test_ptr;
}

pgraft_go_set_debug_func
pgraft_go_get_set_debug_func(void)
{
	return pgraft_go_set_debug_ptr;
}

pgraft_go_start_network_server_func
pgraft_go_get_start_network_server_func(void)
{
	return pgraft_go_start_network_server_ptr;
}

pgraft_go_free_string_func
pgraft_go_get_free_string_func(void)
{
	return pgraft_go_free_string_ptr;
}

pgraft_go_update_cluster_state_func
pgraft_go_get_update_cluster_state_func(void)
{
	return pgraft_go_update_cluster_state_ptr;
}

/*
 * Initialize the Go library
 */
int
pgraft_go_init(int node_id, char *address, int port)
{
	pgraft_go_init_func init_func;
	
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: Go library not loaded");
		return -1;
	}
	
	init_func = pgraft_go_get_init_func();
	if (init_func == NULL) {
		elog(ERROR, "pgraft: Failed to get init function");
		return -1;
	}
	
	return init_func(node_id, address, port);
}

/*
 * Start the Go library
 */
int
pgraft_go_start(void)
{
	pgraft_go_start_func start_func;
	
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: Go library not loaded");
		return -1;
	}
	
	start_func = pgraft_go_get_start_func();
	if (start_func == NULL) {
		elog(ERROR, "pgraft: Failed to get start function");
		return -1;
	}
	
	return start_func();
}

/*
 * Start the Go network server
 */
int
pgraft_go_start_network_server(int port)
{
	pgraft_go_start_network_server_func start_network_server;
	
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: Go library not loaded");
		return -1;
	}
	
	start_network_server = pgraft_go_get_start_network_server_func();
	if (start_network_server == NULL) {
		elog(ERROR, "pgraft: Failed to get start_network_server function");
		return -1;
	}
	
	return start_network_server(port);
}
