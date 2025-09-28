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
#include <unistd.h>

#include "../include/pgraft_go.h"
#include "../include/pgraft_state.h"
#include "../include/pgraft_guc.h"

/* Function declarations */
static int pgraft_go_load_symbols(void);
static int pgraft_go_check_version(void);

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
	const char *custom_path;
	const char *pg_libdir;
	
	/* Check if already loaded in this process */
	if (go_lib_handle != NULL)
	{
		elog(DEBUG1, "pgraft: Go library already loaded in this process");
		return 0; /* Already loaded */
	}
	
	/* Use custom path if provided via GUC */
	custom_path = pgraft_go_library_path;
	if (custom_path != NULL && strlen(custom_path) > 0)
	{
		/* Verify custom path exists */
		if (access(custom_path, R_OK) != 0)
		{
			elog(ERROR, "pgraft: Go library path does not exist or is not readable: %s", custom_path);
			return -1;
		}
		strlcpy(lib_path, custom_path, sizeof(lib_path));
		elog(LOG, "pgraft: Using custom Go library path: %s", lib_path);
	}
	else
	{
		/* Get PostgreSQL library directory */
		pg_libdir = pkglib_path;
		if (pg_libdir == NULL)
		{
			elog(ERROR, "pgraft: PostgreSQL library directory not found");
			return -1;
		}
		
		/* Build OS-specific library name */
#ifdef __APPLE__
		snprintf(lib_path, sizeof(lib_path), "%s/pgraft_go.dylib", pg_libdir);
#else
		snprintf(lib_path, sizeof(lib_path), "%s/pgraft_go.so", pg_libdir);
#endif
		elog(LOG, "pgraft: Using default Go library path: %s", lib_path);
	}
	
	/* Verify library exists before loading */
	if (access(lib_path, R_OK) != 0)
	{
		elog(ERROR, "pgraft: Go library does not exist or is not readable: %s", lib_path);
		return -1;
	}
	
	elog(LOG, "pgraft: Loading Go library from %s", lib_path);
	
	/* Load the library */
	go_lib_handle = dlopen(lib_path, RTLD_LAZY | RTLD_GLOBAL);
	if (go_lib_handle == NULL)
	{
		elog(ERROR, "pgraft: Failed to load Go library from %s: %s", lib_path, dlerror());
		return -1;
	}
	
	elog(LOG, "pgraft: Go library loaded successfully");
	
	/* Load function symbols with proper error handling */
	if (pgraft_go_load_symbols() != 0)
	{
		/* Don't dlclose - keep library for process lifetime */
		go_lib_handle = NULL;
		return -1;
	}
	
	/* Verify version compatibility */
	if (pgraft_go_check_version() != 0)
	{
		/* Don't dlclose - keep library for process lifetime */
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

/*
 * Initialize Go shared memory
 */
void
pgraft_go_init_shared_memory(void)
{
	/* Go library manages its own state, no shared memory needed */
	elog(LOG, "pgraft: Go shared memory initialization completed");
}

/*
 * Load Go library symbols with proper error handling
 */
static int
pgraft_go_load_symbols(void)
{
	char *error;
	
	/* Clear any existing error */
	dlerror();
	
	/* Load function pointers with error checking */
	pgraft_go_init_ptr = (pgraft_go_init_func) dlsym(go_lib_handle, "pgraft_go_init");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: Failed to load symbol 'pgraft_go_init': %s", error);
		return -1;
	}
	
	pgraft_go_start_ptr = (pgraft_go_start_func) dlsym(go_lib_handle, "pgraft_go_start");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: Failed to load symbol 'pgraft_go_start': %s", error);
		return -1;
	}
	
	pgraft_go_stop_ptr = (pgraft_go_stop_func) dlsym(go_lib_handle, "pgraft_go_stop");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: Failed to load symbol 'pgraft_go_stop': %s", error);
		return -1;
	}
	
	pgraft_go_add_peer_ptr = (pgraft_go_add_peer_func) dlsym(go_lib_handle, "pgraft_go_add_peer");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: Failed to load symbol 'pgraft_go_add_peer': %s", error);
		return -1;
	}
	
	pgraft_go_get_leader_ptr = (pgraft_go_get_leader_func) dlsym(go_lib_handle, "pgraft_go_get_leader");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: Failed to load symbol 'pgraft_go_get_leader': %s", error);
		return -1;
	}
	
	pgraft_go_get_term_ptr = (pgraft_go_get_term_func) dlsym(go_lib_handle, "pgraft_go_get_term");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: Failed to load symbol 'pgraft_go_get_term': %s", error);
		return -1;
	}
	
	pgraft_go_version_ptr = (pgraft_go_version_func) dlsym(go_lib_handle, "pgraft_go_version");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: Failed to load symbol 'pgraft_go_version': %s", error);
		return -1;
	}
	
	/* Load optional symbols */
	pgraft_go_remove_peer_ptr = (pgraft_go_remove_peer_func) dlsym(go_lib_handle, "pgraft_go_remove_peer");
	pgraft_go_is_leader_ptr = (pgraft_go_is_leader_func) dlsym(go_lib_handle, "pgraft_go_is_leader");
	pgraft_go_get_nodes_ptr = (pgraft_go_get_nodes_func) dlsym(go_lib_handle, "pgraft_go_get_nodes");
	pgraft_go_test_ptr = (pgraft_go_test_func) dlsym(go_lib_handle, "pgraft_go_test");
	pgraft_go_set_debug_ptr = (pgraft_go_set_debug_func) dlsym(go_lib_handle, "pgraft_go_set_debug");
	pgraft_go_start_network_server_ptr = (pgraft_go_start_network_server_func) dlsym(go_lib_handle, "pgraft_go_start_network_server");
	pgraft_go_free_string_ptr = (pgraft_go_free_string_func) dlsym(go_lib_handle, "pgraft_go_free_string");
	pgraft_go_update_cluster_state_ptr = (pgraft_go_update_cluster_state_func) dlsym(go_lib_handle, "pgraft_go_update_cluster_state");
	
	elog(LOG, "pgraft: All Go library symbols loaded successfully");
	return 0;
}

/*
 * Check Go library version compatibility
 */
static int
pgraft_go_check_version(void)
{
	const char *version;
	char *expected_version = "1.0.0";
	
	if (pgraft_go_version_ptr == NULL)
	{
		elog(WARNING, "pgraft: Version function not available, skipping version check");
		return 0;
	}
	
	version = pgraft_go_version_ptr();
	if (version == NULL)
	{
		elog(WARNING, "pgraft: Version function returned NULL, skipping version check");
		return 0;
	}
	
	if (strcmp(version, expected_version) != 0)
	{
		elog(WARNING, "pgraft: Version mismatch - expected %s, got %s", expected_version, version);
		/* Don't fail, just warn */
	}
	else
	{
		elog(LOG, "pgraft: Version check passed - %s", version);
	}
	
	return 0;
}
