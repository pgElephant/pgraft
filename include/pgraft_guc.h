#ifndef PGRAFT_GUC_H
#define PGRAFT_GUC_H

#include "postgres.h"
#include "utils/guc.h"

/* Configuration data structures for etcd-compatible parameters */

/* Cluster member structure */
typedef struct {
	char	   *name;
	char	   *peer_url;
} pgraft_cluster_member_t;

/* URL list structure for comma-separated URLs */
typedef struct {
	char	  **urls;
	int			count;
} pgraft_url_list_t;

/* Configuration structure to hold parsed values */
typedef struct {
	/* Cluster configuration */
	pgraft_cluster_member_t *cluster_members;
	int			cluster_member_count;
	char	   *cluster_state;
	char	   *cluster_token;
	
	/* URL lists */
	pgraft_url_list_t peer_urls;
	pgraft_url_list_t client_urls;
	pgraft_url_list_t advertise_peer_urls;
	pgraft_url_list_t advertise_client_urls;
	pgraft_url_list_t metrics_urls;
	
	/* Consensus settings */
	int			election_timeout;
	int			heartbeat_interval;
	int			snapshot_count;
	int			quota_backend_bytes;
	int			max_request_bytes;
	
	/* Logging settings */
	char	   *log_level;
	char	   *log_outputs;
	char	   *log_package_levels;
	
	/* Storage settings */
	int			max_snapshots;
	int			max_wals;
	char	   *auto_compaction_retention;
	char	   *auto_compaction_mode;
	int			compaction_batch_limit;
	
	/* Security settings */
	bool		client_cert_auth;
	char	   *trusted_ca_file;
	char	   *cert_file;
	char	   *key_file;
	char	   *client_cert_file;
	char	   *client_key_file;
	char	   *peer_trusted_ca_file;
	char	   *peer_cert_file;
	char	   *peer_key_file;
	bool		peer_client_cert_auth;
	char	   *peer_cert_allowed_cn;
	bool		peer_cert_allowed_hostname;
	
	/* Network settings */
	char	   *cipher_suites;
	char	   *cors;
	char	   *host_whitelist;
	char	   *metrics;
} pgraft_parsed_config_t;

/* etcd-compatible core cluster configuration - exact same parameter names and types as etcd */
extern char	   *pgraft_guc_name;
extern char	   *pgraft_guc_data_dir;
extern char	   *pgraft_guc_initial_cluster;
extern char	   *pgraft_guc_initial_cluster_state;
extern char	   *pgraft_guc_initial_cluster_token;
extern char	   *pgraft_guc_initial_advertise_peer_urls;
extern char	   *pgraft_guc_advertise_client_urls;
extern char	   *pgraft_guc_listen_client_urls;
extern char	   *pgraft_guc_listen_peer_urls;

/* etcd-compatible consensus settings - exact same parameter names and types as etcd */
extern int		pgraft_guc_election_timeout;
extern int		pgraft_guc_heartbeat_interval;
extern int		pgraft_guc_snapshot_count;
extern int		pgraft_guc_quota_backend_bytes;
extern int		pgraft_guc_max_request_bytes;

/* etcd-compatible logging settings - exact same parameter names and types as etcd */
extern char	   *pgraft_guc_log_level;
extern char	   *pgraft_guc_log_outputs;
extern char	   *pgraft_guc_log_package_levels;

/* etcd-compatible performance settings - exact same parameter names and types as etcd */
extern int		pgraft_guc_max_snapshots;
extern int		pgraft_guc_max_wals;
extern char	   *pgraft_guc_auto_compaction_retention;
extern char	   *pgraft_guc_auto_compaction_mode;
extern int		pgraft_guc_compaction_batch_limit;

/* etcd-compatible security settings - exact same parameter names and types as etcd */
extern bool		pgraft_guc_client_cert_auth;
extern char	   *pgraft_guc_trusted_ca_file;
extern char	   *pgraft_guc_cert_file;
extern char	   *pgraft_guc_key_file;
extern char	   *pgraft_guc_client_cert_file;
extern char	   *pgraft_guc_client_key_file;
extern char	   *pgraft_guc_peer_trusted_ca_file;
extern char	   *pgraft_guc_peer_cert_file;
extern char	   *pgraft_guc_peer_key_file;
extern bool		pgraft_guc_peer_client_cert_auth;
extern char	   *pgraft_guc_peer_cert_allowed_cn;
extern bool		pgraft_guc_peer_cert_allowed_hostname;
extern char	   *pgraft_guc_cipher_suites;
extern char	   *pgraft_guc_cors;
extern char	   *pgraft_guc_host_whitelist;

/* etcd-compatible monitoring settings - exact same parameter names and types as etcd */
extern char	   *pgraft_guc_listen_metrics_urls;
extern char	   *pgraft_guc_metrics;

/* PostgreSQL-specific settings (not in etcd) */
extern char	   *pgraft_guc_go_library_path;
extern int		pgraft_max_log_entries;
extern int		pgraft_batch_size;
extern int		pgraft_max_batch_delay;

/* GUC functions */
void		pgraft_guc_init(void);
void		pgraft_guc_shutdown(void);
void		pgraft_register_guc_variables(void);
void		pgraft_validate_configuration(void);

/* Configuration parsing functions */
void		pgraft_parse_configuration(pgraft_parsed_config_t *config);
void		pgraft_parse_initial_cluster(const char *cluster_str, pgraft_cluster_member_t **members, int *count);
void		pgraft_parse_url_list(const char *url_str, pgraft_url_list_t *url_list);
bool		pgraft_parse_url(const char *url_str, char **host, int *port);
void		pgraft_free_parsed_config(pgraft_parsed_config_t *config);

/* GUC validation functions - TODO: implement proper PostgreSQL GUC callbacks */

#endif
