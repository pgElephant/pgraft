#ifndef PGRAFT_GO_H
#define PGRAFT_GO_H

#include "postgres.h"

/* Cluster member structure - parsed from initial_cluster */
typedef struct pgraft_go_cluster_member {
	char   *name;          /* Member name (e.g., "primary1") */
	char   *peer_host;     /* Peer host/IP (e.g., "127.0.0.1") */
	int		peer_port;     /* Peer port (e.g., 2380) */
} pgraft_go_cluster_member_t;

/* Configuration structure for Go init (etcd-style) */
typedef struct pgraft_go_config {
	int		node_id;
	char   *cluster_id;
	char   *address;
	int		port;
	char   *data_dir;
	
	/* Parsed cluster members (from initial_cluster) */
	pgraft_go_cluster_member_t *cluster_members;
	int		cluster_member_count;
	
	int		initial_cluster_state;
	char   *name;
	
	/* Parsed URLs - split into host/port */
	char   *listen_peer_host;
	int		listen_peer_port;
	char   *listen_client_host;
	int		listen_client_port;
	char   *advertise_client_host;
	int		advertise_client_port;
	char   *initial_advertise_peer_host;
	int		initial_advertise_peer_port;
	int		election_timeout;
	int		heartbeat_interval;
	int		snapshot_interval;
	int		quota_backend_bytes;
	int		max_request_bytes;
	int		max_snapshots;
	int		max_wals;
	int		auto_compaction_retention;
	int		auto_compaction_mode;
	int		compaction_batch_limit;
	char   *log_level;
	char   *log_outputs;
	char   *log_package_levels;
	int		client_cert_auth;
	char   *trusted_ca_file;
	char   *cert_file;
	char   *key_file;
	char   *client_cert_file;
	char   *client_key_file;
	char   *peer_trusted_ca_file;
	char   *peer_cert_file;
	char   *peer_key_file;
	int		peer_client_cert_auth;
	char   *peer_cert_allowed_cn;
	int		peer_cert_allowed_hostname;
	char   *cipher_suites;
	char   *cors;
	char   *host_whitelist;
	char   *listen_metrics_urls;
	char   *metrics;
	int		experimental_initial_corrupt_check;
	char   *experimental_corrupt_check_time;
	char   *experimental_enable_v2v3;
	int		experimental_enable_lease_checkpoint;
	int		experimental_compaction_batch_limit;
	int		experimental_peer_skip_client_san_verification;
	int		experimental_self_signed_cert_validity;
	char   *experimental_watch_progress_notify_interval;
	int		max_log_entries;
	int		batch_size;
	int		max_batch_delay;
} pgraft_go_config_t;

/* Function pointers for Go functions */
typedef int (*pgraft_go_init_func) (int nodeID, char *address, int port);
typedef int (*pgraft_go_init_config_func) (pgraft_go_config_t *config);
typedef int (*pgraft_go_start_func) (void);
typedef int (*pgraft_go_stop_func) (void);
typedef int (*pgraft_go_add_peer_func) (int nodeID, char *address, int port);
typedef int (*pgraft_go_remove_peer_func) (int nodeID);
typedef char *(*pgraft_go_get_state_func) (void);
typedef int64_t (*pgraft_go_get_leader_func) (void);
typedef int32_t (*pgraft_go_get_term_func) (void);
typedef int (*pgraft_go_is_initialized_func) (void);
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
typedef int64_t (*pgraft_go_get_node_id_func) (void);
typedef char *(*pgraft_go_version_func) (void);
typedef int (*pgraft_go_test_func) (void);
typedef void (*pgraft_go_cleanup_func) (void);
typedef int (*pgraft_go_replicate_log_entry_func) (char *data, int data_len);
typedef int (*pgraft_go_log_replicate_func) (unsigned long long leader_id, unsigned long long from_index);


/* C wrappers for Go functions */
extern int pgraft_go_init(int nodeID, char *address, int port);  /* Legacy */
extern int pgraft_go_init_with_config(pgraft_go_config_t *config);  /* New etcd-style */
extern int pgraft_go_start(void);
extern int pgraft_go_start_background(void);  /* Start ticker and background processing */
extern int pgraft_go_stop(void);
extern int pgraft_go_connect_to_peers(void);
extern int pgraft_go_add_peer(int nodeID, char *address, int port);
extern int pgraft_go_remove_peer(int nodeID);
extern char *pgraft_go_get_state(void);
extern int64_t pgraft_go_get_leader(void);
extern int32_t pgraft_go_get_term(void);
extern int pgraft_go_is_initialized(void);
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
extern int64_t pgraft_go_get_node_id(void);
extern int pgraft_go_tick(void);  /* Worker-driven tick function */
extern void cleanup_pgraft(void);

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
extern pgraft_go_get_node_id_func pgraft_go_get_get_node_id_func(void);
extern pgraft_go_version_func pgraft_go_get_version_func(void);
extern pgraft_go_test_func pgraft_go_get_test_func(void);
extern pgraft_go_set_debug_func pgraft_go_get_set_debug_func(void);
extern pgraft_go_start_network_server_func pgraft_go_get_start_network_server_func(void);
extern pgraft_go_trigger_heartbeat_func pgraft_go_get_trigger_heartbeat_func(void);
extern pgraft_go_free_string_func pgraft_go_get_free_string_func(void);
extern pgraft_go_update_cluster_state_func pgraft_go_get_update_cluster_state_func(void);
extern pgraft_go_replicate_log_entry_func pgraft_go_get_replicate_log_entry_func(void);
extern pgraft_go_log_replicate_func pgraft_go_get_log_replicate_func(void);

#endif
