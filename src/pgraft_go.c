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
#include "utils/guc.h"

#include <dlfcn.h>
#include <unistd.h>

#include "../include/pgraft_go.h"
#include "../include/pgraft_state.h"
#include "../include/pgraft_guc.h"

/* Platform-specific library extension */
#ifdef __APPLE__
#define GO_LIB_NAME "pgraft_go.dylib"
#else
#define GO_LIB_NAME "pgraft_go.so"
#endif

/* Function declarations */
static int pgraft_go_load_symbols(void);
static int pgraft_go_check_version(void);

/* Global variables */
static void *go_lib_handle = NULL;

/* Function pointers */
static pgraft_go_init_func pgraft_go_init_ptr = NULL;
static pgraft_go_init_config_func pgraft_go_init_config_ptr = NULL;
static pgraft_go_start_func pgraft_go_start_ptr = NULL;
static pgraft_go_start_func pgraft_go_start_background_ptr = NULL;  /* Same signature as start */
static pgraft_go_stop_func pgraft_go_stop_ptr = NULL;
static pgraft_go_add_peer_func pgraft_go_add_peer_ptr = NULL;
static pgraft_go_remove_peer_func pgraft_go_remove_peer_ptr = NULL;
static pgraft_go_get_leader_func pgraft_go_get_leader_ptr = NULL;
static pgraft_go_get_term_func pgraft_go_get_term_ptr = NULL;
static pgraft_go_get_node_id_func pgraft_go_get_node_id_ptr = NULL;
static pgraft_go_is_initialized_func pgraft_go_is_initialized_ptr = NULL;
static pgraft_go_is_leader_func pgraft_go_is_leader_ptr = NULL;
static pgraft_go_append_log_func pgraft_go_append_log_ptr = NULL;
static pgraft_go_get_nodes_func pgraft_go_get_nodes_ptr = NULL;
static pgraft_go_log_replicate_func pgraft_go_log_replicate_ptr = NULL;
static pgraft_go_version_func pgraft_go_version_ptr = NULL;
static pgraft_go_test_func pgraft_go_test_ptr = NULL;
static pgraft_go_set_debug_func pgraft_go_set_debug_ptr = NULL;
static pgraft_go_start_network_server_func pgraft_go_start_network_server_ptr = NULL;
static pgraft_go_trigger_heartbeat_func pgraft_go_trigger_heartbeat_ptr = NULL;
static pgraft_go_free_string_func pgraft_go_free_string_ptr = NULL;
static pgraft_go_update_cluster_state_func pgraft_go_update_cluster_state_ptr = NULL;
static pgraft_go_replicate_log_entry_func pgraft_go_replicate_log_entry_ptr = NULL;

/*
 * Load Go Raft library dynamically
 */
int
pgraft_go_load_library(void)
{
	char	   *lib_path_to_load;

	/* Check if already loaded in this process */
	if (go_lib_handle != NULL && pgraft_go_init_ptr != NULL)
	{
		elog(DEBUG1, "pgraft: go library already loaded in this process");
		return 0; /* Already loaded */
	}
	
	/* If handle exists but function pointers are NULL, we need to reload symbols */
	if (go_lib_handle != NULL && pgraft_go_init_ptr == NULL)
	{
		elog(LOG, "pgraft: go library handle exists but symbols not loaded, reloading symbols");
		if (pgraft_go_load_symbols() != 0)
		{
			return -1;
		}
		elog(LOG, "pgraft: go library symbols reloaded successfully");
		return 0;
	}

	/* Determine library path with multiple fallbacks for cross-platform compatibility */
	lib_path_to_load = go_library_path;
	if (lib_path_to_load == NULL || strlen(lib_path_to_load) == 0)
	{
		/* Fallback to pkglibdir if GUC is not set */
		static char default_path[MAXPGPATH];
		snprintf(default_path, sizeof(default_path), "%s/%s", PKGLIBDIR, GO_LIB_NAME);
		lib_path_to_load = default_path;
		elog(LOG, "pgraft: pgraft.go_library_path GUC is empty, using default path: %s", lib_path_to_load);
	}

	/* Verify library exists, with fallback search paths */
	if (access(lib_path_to_load, R_OK) != 0)
	{
		/* Try alternative paths for different platforms */
		static char alt_paths[5][MAXPGPATH];
		int alt_count = 0;
		bool found = false;
		int i;
		
		/* Path 1: Current directory (for development) */
		snprintf(alt_paths[alt_count++], MAXPGPATH, "./src/%s", GO_LIB_NAME);
		
		/* Path 2: PostgreSQL lib dir */
		snprintf(alt_paths[alt_count++], MAXPGPATH, "%s/%s", PKGLIBDIR, GO_LIB_NAME);
		
		/* Path 3: /usr/lib/postgresql/XX/lib (Debian/Ubuntu) */
		snprintf(alt_paths[alt_count++], MAXPGPATH, "/usr/lib/postgresql/%d/lib/%s", 
				 PG_VERSION_NUM / 10000, GO_LIB_NAME);
		
		/* Path 4: /usr/local/lib/postgresql (macOS/FreeBSD) */
		snprintf(alt_paths[alt_count++], MAXPGPATH, "/usr/local/lib/postgresql/%s", GO_LIB_NAME);
		
		/* Path 5: /usr/pgsql-XX/lib (RHEL/Rocky) */
		snprintf(alt_paths[alt_count++], MAXPGPATH, "/usr/pgsql-%d/lib/%s",
				 PG_VERSION_NUM / 10000, GO_LIB_NAME);
		
		/* Try each alternative path */
		for (i = 0; i < alt_count; i++)
		{
			if (access(alt_paths[i], R_OK) == 0)
			{
				lib_path_to_load = alt_paths[i];
				found = true;
				elog(LOG, "pgraft: found Go library at alternative path: %s", lib_path_to_load);
				break;
			}
		}
		
		if (!found)
		{
			elog(ERROR, "pgraft: go library does not exist or is not readable: %s (tried %d alternative paths)", 
				 lib_path_to_load, alt_count);
			return -1;
		}
	}

		elog(LOG, "pgraft: attempting to load Go library from %s", lib_path_to_load);

	/* Load the library */
	go_lib_handle = dlopen(lib_path_to_load, RTLD_LAZY | RTLD_GLOBAL);
	if (go_lib_handle == NULL)
	{
		elog(ERROR, "pgraft: failed to load Go library from %s: %s", lib_path_to_load, dlerror());
		return -1;
	}

	elog(LOG, "pgraft: go library loaded successfully");

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

	elog(INFO, "pgraft: go library loaded successfully");

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
	pgraft_go_init_config_ptr = NULL;
	pgraft_go_start_ptr = NULL;
	pgraft_go_stop_ptr = NULL;
	pgraft_go_add_peer_ptr = NULL;
	pgraft_go_remove_peer_ptr = NULL;
	pgraft_go_get_leader_ptr = NULL;
	pgraft_go_get_term_ptr = NULL;
	pgraft_go_get_node_id_ptr = NULL;
	pgraft_go_is_leader_ptr = NULL;
	pgraft_go_get_nodes_ptr = NULL;
	pgraft_go_version_ptr = NULL;
	pgraft_go_test_ptr = NULL;
	pgraft_go_set_debug_ptr = NULL;
	pgraft_go_free_string_ptr = NULL;
	
	/* Update shared memory state */
	pgraft_state_set_go_lib_loaded(false);
	
	elog(INFO, "pgraft: go library unloaded");
}

/*
 * Check if Go library is loaded
 */
bool
pgraft_go_is_loaded(void)
{
	/* Check if loaded in THIS process (process-local check only) */
	/* Each process must load its own copy of the shared library */
	return (go_lib_handle != NULL);
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
	elog(INFO, "pgraft: returning pgraft_go_start_ptr");
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

pgraft_go_get_node_id_func
pgraft_go_get_get_node_id_func(void)
{
	return pgraft_go_get_node_id_ptr;
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

pgraft_go_log_replicate_func
pgraft_go_get_log_replicate_func(void)
{
	return pgraft_go_log_replicate_ptr;
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

pgraft_go_replicate_log_entry_func
pgraft_go_get_replicate_log_entry_func(void)
{
	return pgraft_go_replicate_log_entry_ptr;
}

/*
 * Initialize the Go library (legacy version)
 */
int
pgraft_go_init(int node_id, char *address, int port)
{
	pgraft_go_init_func init_func;
	
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: go library not loaded");
		return -1;
	}
	
	init_func = pgraft_go_get_init_func();
	if (init_func == NULL) {
		elog(ERROR, "pgraft: failed to get init function");
		return -1;
	}
	
	return init_func(node_id, address, port);
}

/*
 * Initialize the Go library with etcd-style configuration
 */
int
pgraft_go_init_with_config(pgraft_go_config_t *config)
{
	pgraft_go_init_func init_func;
	
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: go library not loaded");
		return -1;
	}
	
	if (pgraft_go_init_config_ptr == NULL)
	{
		elog(WARNING, "pgraft: new configuration function not available, falling back to legacy init");
		init_func = pgraft_go_get_init_func();
		if (init_func != NULL)
		{
			return init_func(config->node_id, config->address, config->port);
		}
		else
		{
			elog(ERROR, "pgraft: no init function available");
			return -1;
		}
	}
	
	elog(INFO, "pgraft: initializing with configuration");
	elog(INFO, "pgraft: node_id=%d, cluster_id=%s, address=%s:%d",
		 config->node_id,
		 config->cluster_id ? config->cluster_id : "(null)",
		 config->address ? config->address : "(null)",
		 config->port);
	elog(DEBUG1, "pgraft: data_dir=%s, election_timeout=%dms, heartbeat_interval=%dms",
		 config->data_dir ? config->data_dir : "(null)",
		 config->election_timeout,
		 config->heartbeat_interval);
	
	return pgraft_go_init_config_ptr(config);
}

/*
 * Start the Go library
 */
int
pgraft_go_start(void)
{
	pgraft_go_start_func start_func;
	
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: go library not loaded");
		return -1;
	}
	
	start_func = pgraft_go_get_start_func();
	if (start_func == NULL) {
		elog(ERROR, "pgraft: failed to get start function");
		return -1;
	}
	
	return start_func();
}

/*
 * Start the Go Raft background ticker and processing loops
 */
int
pgraft_go_start_background(void)
{
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: go library not loaded");
		return -1;
	}
	
	if (pgraft_go_start_background_ptr == NULL) {
		elog(WARNING, "pgraft: start_background function not available");
		return -1;
	}
	
	return pgraft_go_start_background_ptr();
}

/*
 * Connect to all cluster peers
 */
int
pgraft_go_connect_to_peers(void)
{
	typedef int (*connect_peers_func)(void);
	connect_peers_func func;
	
	if (!pgraft_go_is_loaded()) {
		elog(WARNING, "pgraft: go library not loaded, cannot connect to peers");
		return -1;
	}
	
	dlerror();  /* Clear any existing error */
	func = (connect_peers_func) dlsym(go_lib_handle, "pgraft_go_connect_to_peers");
	if (func == NULL) {
		elog(WARNING, "pgraft: pgraft_go_connect_to_peers function not found: %s", dlerror());
		return -1;
	}
	
	return func();
}

/*
 * Start the Go network server
 */
int
pgraft_go_start_network_server(int port)
{
	pgraft_go_start_network_server_func start_network_server;
	
	if (!pgraft_go_is_loaded()) {
		elog(ERROR, "pgraft: go library not loaded");
		return -1;
	}
	
	start_network_server = pgraft_go_get_start_network_server_func();
	if (start_network_server == NULL) {
		elog(ERROR, "pgraft: failed to get start_network_server function");
		return -1;
	}
	
	return start_network_server(port);
}

/*
 * Check if this node is the leader
 */
int
pgraft_go_is_initialized(void)
{
	if (!pgraft_go_is_loaded() || pgraft_go_is_initialized_ptr == NULL) {
		return 0;
	}
	
	return pgraft_go_is_initialized_ptr();
}

int
pgraft_go_is_leader(void)
{
	pgraft_go_is_leader_func is_leader_func;
	
	if (!pgraft_go_is_loaded()) {
		elog(DEBUG1, "pgraft: go library not loaded, cannot check leader status");
		return -1; /* Not ready */
	}
	
	is_leader_func = pgraft_go_get_is_leader_func();
	if (is_leader_func == NULL) {
		elog(DEBUG1, "pgraft: is_leader function not available");
		return -1; /* Not ready */
	}
	
	return is_leader_func();
}

/*
 * Initialize Go shared memory
 */
void
pgraft_go_init_shared_memory(void)
{
	/* Go library manages its own state, no shared memory needed */
	elog(LOG, "pgraft: go shared memory initialization completed");
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
		elog(ERROR, "pgraft: failed to load symbol 'pgraft_go_init': %s", error);
		return -1;
	}
	
	/* Load new etcd-style init function (optional for backward compatibility) */
	pgraft_go_init_config_ptr = (pgraft_go_init_config_func) dlsym(go_lib_handle, "pgraft_go_init_config");
	dlerror(); /* Clear error since this is optional */
	if (pgraft_go_init_config_ptr != NULL)
	{
		elog(LOG, "pgraft: etcd-style init function 'pgraft_go_init_config' loaded");
	}
	else
	{
		elog(LOG, "pgraft: etcd-style init function not found, using legacy init");
	}
	
	pgraft_go_start_ptr = (pgraft_go_start_func) dlsym(go_lib_handle, "pgraft_go_start");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: failed to load symbol 'pgraft_go_start': %s", error);
		return -1;
	}
	
	pgraft_go_start_background_ptr = (pgraft_go_start_func) dlsym(go_lib_handle, "pgraft_go_start_background");
	if ((error = dlerror()) != NULL)
	{
		elog(WARNING, "pgraft: failed to load symbol 'pgraft_go_start_background': %s (optional)", error);
		pgraft_go_start_background_ptr = NULL;  /* Optional function */
	}
	
	pgraft_go_stop_ptr = (pgraft_go_stop_func) dlsym(go_lib_handle, "pgraft_go_stop");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: failed to load symbol 'pgraft_go_stop': %s", error);
		return -1;
	}
	
	pgraft_go_add_peer_ptr = (pgraft_go_add_peer_func) dlsym(go_lib_handle, "pgraft_go_add_peer");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: failed to load symbol 'pgraft_go_add_peer': %s", error);
		return -1;
	}
	
	pgraft_go_get_leader_ptr = (pgraft_go_get_leader_func) dlsym(go_lib_handle, "pgraft_go_get_leader");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: failed to load symbol 'pgraft_go_get_leader': %s", error);
		return -1;
	}
	
	pgraft_go_get_term_ptr = (pgraft_go_get_term_func) dlsym(go_lib_handle, "pgraft_go_get_term");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: failed to load symbol 'pgraft_go_get_term': %s", error);
		return -1;
	}
	
	pgraft_go_get_node_id_ptr = (pgraft_go_get_node_id_func) dlsym(go_lib_handle, "pgraft_go_get_node_id");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: failed to load symbol 'pgraft_go_get_node_id': %s", error);
		return -1;
	}
	
	pgraft_go_version_ptr = (pgraft_go_version_func) dlsym(go_lib_handle, "pgraft_go_version");
	if ((error = dlerror()) != NULL)
	{
		elog(ERROR, "pgraft: failed to load symbol 'pgraft_go_version': %s", error);
		return -1;
	}
	
	/* Load optional symbols - clear dlerror() before each to avoid false positives */
	dlerror(); /* Clear any existing error */
	pgraft_go_remove_peer_ptr = (pgraft_go_remove_peer_func) dlsym(go_lib_handle, "pgraft_go_remove_peer");
	
	dlerror();
	pgraft_go_is_initialized_ptr = (pgraft_go_is_initialized_func) dlsym(go_lib_handle, "pgraft_go_is_initialized");
	if (pgraft_go_is_initialized_ptr != NULL) {
		elog(DEBUG1, "pgraft: is_initialized function loaded successfully");
	} else {
		elog(WARNING, "pgraft: failed to load pgraft_go_is_initialized: %s", dlerror());
	}
	
	dlerror();
	pgraft_go_is_leader_ptr = (pgraft_go_is_leader_func) dlsym(go_lib_handle, "pgraft_go_is_leader");
	if (pgraft_go_is_leader_ptr != NULL) {
		elog(DEBUG1, "pgraft: is_leader function loaded successfully");
	} else {
		elog(WARNING, "pgraft: is_leader function not found in library");
	}
	
	dlerror();
	pgraft_go_append_log_ptr = (pgraft_go_append_log_func) dlsym(go_lib_handle, "pgraft_go_append_log");
	if (pgraft_go_append_log_ptr == NULL) {
		elog(DEBUG1, "pgraft: append_log function not found (optional)");
	}
	
	dlerror(); /* Clear error */
	pgraft_go_get_nodes_ptr = (pgraft_go_get_nodes_func) dlsym(go_lib_handle, "pgraft_go_get_nodes");
	
	dlerror(); /* Clear error */
	pgraft_go_log_replicate_ptr = (pgraft_go_log_replicate_func) dlsym(go_lib_handle, "pgraft_go_log_replicate");
	if (pgraft_go_log_replicate_ptr == NULL) {
		elog(DEBUG1, "pgraft: log_replicate function not found (optional)");
	}
	
	dlerror(); /* Clear error */
	pgraft_go_test_ptr = (pgraft_go_test_func) dlsym(go_lib_handle, "pgraft_go_test");
	
	dlerror(); /* Clear error */
	pgraft_go_set_debug_ptr = (pgraft_go_set_debug_func) dlsym(go_lib_handle, "pgraft_go_set_debug");
	
	dlerror(); /* Clear error */
	pgraft_go_start_network_server_ptr = (pgraft_go_start_network_server_func) dlsym(go_lib_handle, "pgraft_go_start_network_server");
	
	dlerror(); /* Clear error */
	pgraft_go_trigger_heartbeat_ptr = (pgraft_go_trigger_heartbeat_func) dlsym(go_lib_handle, "pgraft_go_trigger_heartbeat");
	
	dlerror(); /* Clear error */
	pgraft_go_free_string_ptr = (pgraft_go_free_string_func) dlsym(go_lib_handle, "pgraft_go_free_string");
	
	dlerror(); /* Clear error */
	pgraft_go_update_cluster_state_ptr = (pgraft_go_update_cluster_state_func) dlsym(go_lib_handle, "pgraft_go_update_cluster_state");
	
	dlerror(); /* Clear error */
	pgraft_go_replicate_log_entry_ptr = (pgraft_go_replicate_log_entry_func) dlsym(go_lib_handle, "pgraft_go_replicate_log_entry");
	
   	elog(LOG, "pgraft: all go library symbols loaded successfully");
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
		elog(WARNING, "pgraft: version function not available, skipping version check");
		return 0;
	}
	
	version = pgraft_go_version_ptr();
	if (version == NULL)
	{
		elog(WARNING, "pgraft: version function returned NULL, skipping version check");
		return 0;
	}
	
	if (strcmp(version, expected_version) != 0)
	{
		elog(WARNING, "pgraft: version mismatch - expected %s, got %s", expected_version, version);
		/* Don't fail, just warn */
	}
	else
	{
		elog(LOG, "pgraft: version check passed - %s", version);
	}
	
	return 0;
}

/*
 * Trigger heartbeat manually
 */
int
pgraft_go_trigger_heartbeat(void)
{
	if (!pgraft_go_is_loaded())
	{
		elog(ERROR, "pgraft: go library not loaded");
		return -1;
	}
	
	if (pgraft_go_trigger_heartbeat_ptr == NULL)
	{
		/* Function not available - silently ignore */
		return 0;
	}
	
	return pgraft_go_trigger_heartbeat_ptr();
}

int
pgraft_go_tick(void)
{
	typedef int (*pgraft_go_tick_func)(void);
	static pgraft_go_tick_func tick_func = NULL;
	static bool load_attempted = false;
	char *error;
	
	if (!pgraft_go_is_loaded())
	{
		return -1;
	}
	
	if (tick_func == NULL && !load_attempted)
	{
		load_attempted = true;
		dlerror();
		tick_func = (pgraft_go_tick_func) dlsym(go_lib_handle, "pgraft_go_tick");
		if (tick_func == NULL)
		{
			error = dlerror();
			elog(WARNING, "pgraft: failed to load pgraft_go_tick: %s", error ? error : "unknown error");
			return -1;
		}
		elog(LOG, "pgraft: pgraft_go_tick function loaded successfully");
	}
	
	if (tick_func == NULL)
	{
		return -1;
	}
	
	return tick_func();
}

int
pgraft_go_append_log(char *data, int length)
{
	if (!pgraft_go_is_loaded())
	{
		elog(ERROR, "pgraft: go library not loaded");
		return -1;
	}
	
	if (pgraft_go_append_log_ptr == NULL)
	{
		elog(ERROR, "pgraft: append_log function not available");
		return -1;
	}
	
	return pgraft_go_append_log_ptr(data, length);
}

void
pgraft_go_free_string(char *str)
{
	if (!pgraft_go_is_loaded())
	{
		return;
	}
	
	if (pgraft_go_free_string_ptr != NULL)
	{
		pgraft_go_free_string_ptr(str);
	}
}

