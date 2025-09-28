#ifndef PGRAFT_GUC_H
#define PGRAFT_GUC_H

#include "postgres.h"

/* GUC variables */
extern int		pgraft_node_id;
extern int		pgraft_port;
extern char	   *pgraft_address;
extern int		pgraft_log_level;
extern int		pgraft_heartbeat_interval;
extern int		pgraft_election_timeout;
extern bool		pgraft_worker_enabled;
extern int		pgraft_worker_interval;
extern char	   *pgraft_cluster_name;
extern int		pgraft_cluster_size;
extern bool		pgraft_enable_auto_cluster_formation;
extern char	   *pgraft_peers;
extern char	   *pgraft_node_name;
extern char	   *pgraft_node_ip;
extern bool		pgraft_is_primary;
extern int		pgraft_health_period_ms;
extern bool		pgraft_health_verbose;

/* GUC functions */
void		pgraft_guc_init(void);
void		pgraft_guc_shutdown(void);
void		pgraft_register_guc_variables(void);
void		pgraft_validate_configuration(void);

#endif
