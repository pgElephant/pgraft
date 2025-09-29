#ifndef PGRAFT_GO_H
#define PGRAFT_GO_H

#include "postgres.h"

/* Function pointers for Go functions */
typedef int (*pgraft_go_init_func) (int nodeID, char *address, int port);
typedef int (*pgraft_go_start_func) (void);
typedef int (*pgraft_go_stop_func) (void);
typedef int (*pgraft_go_add_peer_func) (int nodeID, char *address, int port);
typedef int (*pgraft_go_remove_peer_func) (int nodeID);
typedef char *(*pgraft_go_get_state_func) (void);
typedef int64_t (*pgraft_go_get_leader_func) (void);
typedef int32_t (*pgraft_go_get_term_func) (void);
typedef int (*pgraft_go_is_leader_func) (void);
typedef int (*pgraft_go_append_log_func) (char *data, int length);
typedef char *(*pgraft_go_get_stats_func) (void);
typedef char *(*pgraft_go_get_logs_func) (void);
typedef int (*pgraft_go_commit_log_func) (long index);
typedef int (*pgraft_go_step_message_func) (char *data, int length);
typedef char *(*pgraft_go_get_network_status_func) (void);
typedef void (*pgraft_go_free_string_func) (char *str);
typedef int (*pgraft_go_set_debug_func) (int enabled);
typedef int (*pgraft_go_update_cluster_state_func) (int64_t leaderID, int64_t currentTerm, const char *state);
typedef int (*pgraft_go_start_network_server_func) (int port);
typedef int (*pgraft_go_trigger_heartbeat_func) (void);
typedef char *(*pgraft_go_get_nodes_func) (void);
typedef char *(*pgraft_go_version_func) (void);
typedef int (*pgraft_go_test_func) (void);


/* C wrappers for Go functions */
extern int pgraft_go_init(int nodeID, char *address, int port);
extern int pgraft_go_start(void);
extern int pgraft_go_stop(void);
extern int pgraft_go_add_peer(int nodeID, char *address, int port);
extern int pgraft_go_remove_peer(int nodeID);
extern char *pgraft_go_get_state(void);
extern int64_t pgraft_go_get_leader(void);
extern int32_t pgraft_go_get_term(void);
extern int pgraft_go_is_leader(void);
extern int pgraft_go_append_log(char *data, int length);
extern char *pgraft_go_get_stats(void);
extern char *pgraft_go_get_logs(void);
extern int pgraft_go_commit_log(long index);
extern int pgraft_go_step_message(char *data, int length);
extern char *pgraft_go_get_network_status(void);
extern void pgraft_go_free_string(char *str);
extern int pgraft_go_set_debug(int enabled);
extern int pgraft_go_update_cluster_state(int64_t leaderID, int64_t currentTerm, const char *state);
extern int pgraft_go_start_network_server(int port);
extern int pgraft_go_trigger_heartbeat(void);

/* C-side Go library management functions */
extern bool pgraft_go_is_loaded(void);
extern int pgraft_go_load_library(void);
extern void pgraft_go_unload_library(void);
extern void pgraft_go_init_shared_memory(void);

/* Go function pointer accessors (C-side) */
extern pgraft_go_init_func pgraft_go_get_init_func(void);
extern pgraft_go_start_func pgraft_go_get_start_func(void);
extern pgraft_go_stop_func pgraft_go_get_stop_func(void);
extern pgraft_go_add_peer_func pgraft_go_get_add_peer_func(void);
extern pgraft_go_remove_peer_func pgraft_go_get_remove_peer_func(void);
extern pgraft_go_get_leader_func pgraft_go_get_get_leader_func(void);
extern pgraft_go_get_term_func pgraft_go_get_get_term_func(void);
extern pgraft_go_is_leader_func pgraft_go_get_is_leader_func(void);
extern pgraft_go_get_nodes_func pgraft_go_get_get_nodes_func(void);
extern pgraft_go_version_func pgraft_go_get_version_func(void);
extern pgraft_go_test_func pgraft_go_get_test_func(void);
extern pgraft_go_set_debug_func pgraft_go_get_set_debug_func(void);
extern pgraft_go_start_network_server_func pgraft_go_get_start_network_server_func(void);
extern pgraft_go_trigger_heartbeat_func pgraft_go_get_trigger_heartbeat_func(void);
extern pgraft_go_free_string_func pgraft_go_get_free_string_func(void);
extern pgraft_go_update_cluster_state_func pgraft_go_get_update_cluster_state_func(void);

#endif
