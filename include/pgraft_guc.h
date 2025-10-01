#ifndef PGRAFT_GUC_H
#define PGRAFT_GUC_H

#include "postgres.h"

/* Core cluster configuration - similar to etcd name, initial-cluster-state */
extern int		pgraft_node_id;
extern char	   *pgraft_cluster_id;
extern char	   *pgraft_address;
extern int		pgraft_port;
extern char	   *pgraft_data_dir;
extern char	   *pgraft_peers;

/* Consensus settings - similar to etcd election-timeout, heartbeat-interval */
extern int		pgraft_election_timeout;
extern int		pgraft_heartbeat_interval;
extern int		pgraft_snapshot_interval;
extern int		pgraft_max_log_entries;

/* Logging - similar to etcd log-level, log-outputs */
extern int		pgraft_log_level;
extern char	   *pgraft_log_file;

/* Performance settings - similar to etcd quota-backend-bytes */
extern int		pgraft_batch_size;
extern int		pgraft_max_batch_delay;
extern int		pgraft_compaction_threshold;

/* Security - similar to etcd client-cert-auth, peer-cert-auth */
extern bool		pgraft_auth_enabled;
extern bool		pgraft_tls_enabled;

/* Monitoring - similar to etcd metrics */
extern bool		pgraft_metrics_enabled;
extern int		pgraft_metrics_port;

/* Replication settings */
extern bool		pgraft_replication_enabled;
extern bool		pgraft_replication_slots;
extern bool		pgraft_synchronous_commit;
extern char	   *pgraft_synchronous_standby_names;

/* Legacy/compatibility GUCs */
extern bool		pgraft_worker_enabled;
extern int		pgraft_worker_interval;
extern char	   *pgraft_cluster_name;
extern int		pgraft_cluster_size;
extern bool		pgraft_enable_auto_cluster_formation;
extern char	   *pgraft_node_name;
extern char	   *pgraft_node_ip;
extern bool		pgraft_is_primary;
extern int		pgraft_health_period_ms;
extern bool		pgraft_health_verbose;
extern bool		pgraft_trace_enabled;
extern char	   *pgraft_go_library_path;

/* GUC functions */
void		pgraft_guc_init(void);
void		pgraft_guc_shutdown(void);
void		pgraft_register_guc_variables(void);
void		pgraft_validate_configuration(void);

#endif
