#ifndef PGRAFT_GO_H
#define PGRAFT_GO_H

#include "postgres.h"

/* Go library function types */
typedef int (*pgraft_go_init_func) (int node_id, char *address, int port);
typedef int (*pgraft_go_start_func) (void);
typedef int (*pgraft_go_stop_func) (void);
typedef int (*pgraft_go_add_peer_func) (int node_id, char *address, int port);
typedef int (*pgraft_go_remove_peer_func) (int node_id);
typedef int64_t (*pgraft_go_get_leader_func) (void);
typedef int32_t (*pgraft_go_get_term_func) (void);
typedef int (*pgraft_go_is_leader_func) (void);
typedef char *(*pgraft_go_get_nodes_func) (void);
typedef char *(*pgraft_go_version_func) (void);
typedef int (*pgraft_go_test_func) (void);
typedef void (*pgraft_go_set_debug_func) (int enabled);
typedef int (*pgraft_go_start_network_server_func) (int port);
typedef void (*pgraft_go_free_string_func) (char *str);
typedef int (*pgraft_go_update_cluster_state_func) (int64_t leader_id, int64_t current_term, const char *state);

/* Go library interface functions */
int			pgraft_go_load_library(void);
void		pgraft_go_unload_library(void);
bool		pgraft_go_is_loaded(void);
int			pgraft_go_init(int node_id, char *address, int port);
int			pgraft_go_start(void);
int			pgraft_go_start_network_server(int port);

/* Function pointer accessors */
pgraft_go_init_func pgraft_go_get_init_func(void);
pgraft_go_start_func pgraft_go_get_start_func(void);
pgraft_go_stop_func pgraft_go_get_stop_func(void);
pgraft_go_add_peer_func pgraft_go_get_add_peer_func(void);
pgraft_go_remove_peer_func pgraft_go_get_remove_peer_func(void);
pgraft_go_get_leader_func pgraft_go_get_get_leader_func(void);
pgraft_go_get_term_func pgraft_go_get_get_term_func(void);
pgraft_go_is_leader_func pgraft_go_get_is_leader_func(void);
pgraft_go_get_nodes_func pgraft_go_get_get_nodes_func(void);
pgraft_go_version_func pgraft_go_get_version_func(void);
pgraft_go_test_func pgraft_go_get_test_func(void);
pgraft_go_set_debug_func pgraft_go_get_set_debug_func(void);
pgraft_go_start_network_server_func pgraft_go_get_start_network_server_func(void);
pgraft_go_free_string_func pgraft_go_get_free_string_func(void);
pgraft_go_update_cluster_state_func pgraft_go_get_update_cluster_state_func(void);

#endif
