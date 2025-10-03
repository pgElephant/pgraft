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
extern char	   *name;
extern char	   *data_dir;
extern char	   *initial_cluster;
extern char	   *initial_cluster_state;
extern char	   *initial_cluster_token;
extern char	   *initial_advertise_peer_urls;
extern char	   *advertise_client_urls;
extern char	   *listen_client_urls;
extern char	   *listen_peer_urls;

/* etcd-compatible consensus settings - exact same parameter names and types as etcd */
extern int		election_timeout;
extern int		heartbeat_interval;
extern int		snapshot_count;
extern int		quota_backend_bytes;
extern int		max_request_bytes;

/* etcd-compatible logging settings - exact same parameter names and types as etcd */
extern char	   *log_level;
extern char	   *log_outputs;
extern char	   *log_package_levels;

/* etcd-compatible performance settings - exact same parameter names and types as etcd */
extern int		max_snapshots;
extern int		max_wals;
extern char	   *auto_compaction_retention;
extern char	   *auto_compaction_mode;
extern int		compaction_batch_limit;

/* etcd-compatible security settings - exact same parameter names and types as etcd */
extern bool		client_cert_auth;
extern char	   *trusted_ca_file;
extern char	   *cert_file;
extern char	   *key_file;
extern char	   *client_cert_file;
extern char	   *client_key_file;
extern char	   *peer_trusted_ca_file;
extern char	   *peer_cert_file;
extern char	   *peer_key_file;
extern bool		peer_client_cert_auth;
extern char	   *peer_cert_allowed_cn;
extern bool		peer_cert_allowed_hostname;
extern char	   *cipher_suites;
extern char	   *cors;
extern char	   *host_whitelist;

/* etcd-compatible monitoring settings - exact same parameter names and types as etcd */
extern char	   *listen_metrics_urls;
extern char	   *metrics;

/* etcd-compatible experimental settings - exact same parameter names and types as etcd */
extern bool		experimental_initial_corrupt_check;
extern char	   *experimental_corrupt_check_time;
extern char	   *experimental_enable_v2v3;
extern char	   *experimental_enable_lease_checkpoint;
extern char	   *experimental_compaction_batch_limit;
extern char	   *experimental_peer_skip_client_san_verification;
extern char	   *experimental_self_signed_cert_validity;
extern char	   *experimental_watch_progress_notify_interval;

/* PostgreSQL-specific settings (not in etcd) */
extern char	   *go_library_path;
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
