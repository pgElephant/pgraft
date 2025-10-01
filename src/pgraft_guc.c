/*-------------------------------------------------------------------------
 *
 * pgraft_guc.c
 *		Configuration management for pgraft extension
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "../include/pgraft_guc.h"
#include "utils/guc.h"
#include "utils/elog.h"

#include <string.h>

/* Core cluster configuration - similar to etcd name, initial-cluster-state */
int			pgraft_node_id = 1;
char	   *pgraft_cluster_id = NULL;
char	   *pgraft_address = NULL;
int			pgraft_port = 0;
char	   *pgraft_data_dir = NULL;
char	   *pgraft_peers = NULL;

/* Consensus settings - similar to etcd election-timeout, heartbeat-interval */
int			pgraft_election_timeout = 1000;
int			pgraft_heartbeat_interval = 100;
int			pgraft_snapshot_interval = 10000;
int			pgraft_max_log_entries = 1000;

/* Logging - similar to etcd log-level, log-outputs */
int			pgraft_log_level = 1;
char	   *pgraft_log_file = NULL;

/* Performance settings - similar to etcd quota-backend-bytes */
int			pgraft_batch_size = 100;
int			pgraft_max_batch_delay = 10;
int			pgraft_compaction_threshold = 1000;

/* Security - similar to etcd client-cert-auth, peer-cert-auth */
bool		pgraft_auth_enabled = false;
bool		pgraft_tls_enabled = false;

/* Monitoring - similar to etcd metrics */
bool		pgraft_metrics_enabled = true;
int			pgraft_metrics_port = 9090;

/* Replication settings */
bool		pgraft_replication_enabled = true;
bool		pgraft_replication_slots = true;
bool		pgraft_synchronous_commit = false;
char	   *pgraft_synchronous_standby_names = NULL;

/* Legacy/compatibility GUCs */
bool		pgraft_worker_enabled = true;
int			pgraft_worker_interval = 1000;
char	   *pgraft_cluster_name = NULL;
int			pgraft_cluster_size = 3;
bool		pgraft_enable_auto_cluster_formation = true;
char	   *pgraft_node_name = NULL;
char	   *pgraft_node_ip = NULL;
bool		pgraft_is_primary = false;
int			pgraft_health_period_ms = 5000;
bool		pgraft_health_verbose = false;
bool		pgraft_trace_enabled = false;
char	   *pgraft_go_library_path = NULL;

/*
 * Register GUC variables
 * Organized similar to etcd configuration structure
 */
void
pgraft_register_guc_variables(void)
{
	/* ========================================
	 * Core cluster configuration
	 * Similar to etcd: name, initial-cluster, initial-cluster-state, data-dir
	 * ======================================== */
	
	DefineCustomIntVariable("pgraft.node_id",
							"Unique node identifier (similar to etcd name)",
							"Must be unique across cluster members",
							&pgraft_node_id,
							1,
							1,
							1000,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.cluster_id",
							   "Cluster identifier (similar to etcd initial-cluster-token)",
							   "All nodes in a cluster must have the same cluster_id",
							   &pgraft_cluster_id,
							   "pgraft-cluster",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.address",
							   "Address for pgraft communication (similar to etcd listen-peer-urls)",
							   "IP address this node will listen on",
							   &pgraft_address,
							   "127.0.0.1",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomIntVariable("pgraft.port",
							"Port for pgraft communication (similar to etcd listen-peer-urls port)",
							"TCP port for raft consensus protocol",
							&pgraft_port,
							7000,
							1024,
							65535,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.data_dir",
							   "Data directory for pgraft state (similar to etcd data-dir)",
							   "Directory to store raft log and snapshots",
							   &pgraft_data_dir,
							   NULL,
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.peers",
							   "Initial cluster members (similar to etcd initial-cluster)",
							   "Comma-separated list in format 'id:address:port'",
							   &pgraft_peers,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	/* ========================================
	 * Consensus/Raft settings
	 * Similar to etcd: election-timeout, heartbeat-interval
	 * ======================================== */

	DefineCustomIntVariable("pgraft.election_timeout",
							"Election timeout in milliseconds (similar to etcd election-timeout)",
							"Time before starting new election if no heartbeat received",
							&pgraft_election_timeout,
							1000,
							100,
							30000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.heartbeat_interval",
							"Heartbeat interval in milliseconds (similar to etcd heartbeat-interval)",
							"Frequency of heartbeat messages from leader",
							&pgraft_heartbeat_interval,
							100,
							10,
							10000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.snapshot_interval",
							"Snapshot interval in number of log entries (similar to etcd snapshot-count)",
							"Number of committed entries before creating snapshot",
							&pgraft_snapshot_interval,
							10000,
							100,
							1000000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.max_log_entries",
							"Maximum raft log entries to keep (similar to etcd quota-backend-bytes)",
							"Limits memory usage from raft log",
							&pgraft_max_log_entries,
							1000,
							100,
							1000000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	/* ========================================
	 * Logging configuration
	 * Similar to etcd: log-level, log-outputs
	 * ======================================== */

	DefineCustomIntVariable("pgraft.log_level",
							"Log verbosity level (similar to etcd log-level)",
							"0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR",
							&pgraft_log_level,
							1,
							0,
							3,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.log_file",
							   "Log file path (similar to etcd log-outputs)",
							   "File path for pgraft logs, NULL uses PostgreSQL logging",
							   &pgraft_log_file,
							   NULL,
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	/* ========================================
	 * Performance tuning
	 * Similar to etcd: quota-backend-bytes, max-request-bytes
	 * ======================================== */

	DefineCustomIntVariable("pgraft.batch_size",
							"Maximum entries per append request",
							"Batch size for log replication efficiency",
							&pgraft_batch_size,
							100,
							1,
							10000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.max_batch_delay",
							"Maximum batch delay in milliseconds",
							"Max time to wait accumulating entries before sending",
							&pgraft_max_batch_delay,
							10,
							1,
							1000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.compaction_threshold",
							"Log compaction threshold",
							"Number of entries before triggering log compaction",
							&pgraft_compaction_threshold,
							1000,
							100,
							1000000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	/* ========================================
	 * Security settings
	 * Similar to etcd: client-cert-auth, peer-cert-auth, trusted-ca-file
	 * ======================================== */

	DefineCustomBoolVariable("pgraft.auth_enabled",
							"Enable authentication (similar to etcd client-cert-auth)",
							"Require authentication for cluster communication",
							&pgraft_auth_enabled,
							false,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pgraft.tls_enabled",
							"Enable TLS encryption (similar to etcd peer-client-cert-auth)",
							"Use TLS for all cluster communication",
							&pgraft_tls_enabled,
							false,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	/* ========================================
	 * Monitoring and metrics
	 * Similar to etcd: metrics, listen-metrics-urls
	 * ======================================== */

	DefineCustomBoolVariable("pgraft.metrics_enabled",
							"Enable metrics collection (similar to etcd metrics)",
							"Export Prometheus-compatible metrics",
							&pgraft_metrics_enabled,
							true,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.metrics_port",
							"Metrics HTTP port (similar to etcd listen-metrics-urls)",
							"Port for metrics endpoint",
							&pgraft_metrics_port,
							9090,
							1024,
							65535,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	/* ========================================
	 * PostgreSQL replication integration
	 * ======================================== */

	DefineCustomBoolVariable("pgraft.replication_enabled",
							"Enable pgraft-managed replication",
							"Coordinate PostgreSQL replication with raft consensus",
							&pgraft_replication_enabled,
							true,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pgraft.replication_slots",
							"Use replication slots",
							"Create and manage replication slots automatically",
							&pgraft_replication_slots,
							true,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pgraft.synchronous_commit",
							"Enable synchronous commits for raft operations",
							"Wait for raft consensus before commit",
							&pgraft_synchronous_commit,
							false,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.synchronous_standby_names",
							   "Synchronous standby names",
							   "Comma-separated list of standby names",
							   &pgraft_synchronous_standby_names,
							   "",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	/* ========================================
	 * Legacy/compatibility settings
	 * (Kept for backward compatibility)
	 * ======================================== */

	DefineCustomBoolVariable("pgraft.worker_enabled",
							"Enable background worker",
							NULL,
							&pgraft_worker_enabled,
							true,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.worker_interval",
							"Worker interval in milliseconds",
							NULL,
							&pgraft_worker_interval,
							1000,
							100,
							60000,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.cluster_name",
							   "Legacy cluster name (deprecated, use cluster_id)",
							   NULL,
							   &pgraft_cluster_name,
							   "pgraft_cluster",
							   PGC_SUSET,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomIntVariable("pgraft.cluster_size",
							"Expected cluster size",
							NULL,
							&pgraft_cluster_size,
							3,
							1,
							100,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pgraft.enable_auto_cluster_formation",
							"Enable automatic cluster formation on startup",
							NULL,
							&pgraft_enable_auto_cluster_formation,
							true,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.node_name",
							   "Node name for cluster identification",
							   NULL,
							   &pgraft_node_name,
							   "pgraft_node_1",
							   PGC_SUSET,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.node_ip",
							   "Node IP address",
							   NULL,
							   &pgraft_node_ip,
							   NULL,
							   PGC_SUSET,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomBoolVariable("pgraft.is_primary",
							"Whether this node is a primary",
							NULL,
							&pgraft_is_primary,
							false,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.health_period_ms",
							"Health check period in milliseconds",
							NULL,
							&pgraft_health_period_ms,
							5000,
							1000,
							60000,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pgraft.health_verbose",
							"Enable verbose health logging",
							NULL,
							&pgraft_health_verbose,
							false,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pgraft.trace_enabled",
							"Enable trace logging",
							NULL,
							&pgraft_trace_enabled,
							false,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.go_library_path",
							   "Path to the Go Raft library",
							   "Default path to the Go Raft shared library",
							   &pgraft_go_library_path,
							   NULL,
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);
}

/*
 * Validate configuration
 * Similar to etcd configuration validation
 */
void
pgraft_validate_configuration(void)
{
	/* Validate core cluster configuration */
	if (pgraft_node_id < 1 || pgraft_node_id > 1000)
	{
		elog(ERROR, "pgraft: Invalid node_id %d, must be between 1 and 1000", pgraft_node_id);
	}

	if (!pgraft_cluster_id || strlen(pgraft_cluster_id) == 0)
	{
		elog(WARNING, "pgraft: cluster_id not set, using default 'pgraft-cluster'");
	}

	if (!pgraft_address || strlen(pgraft_address) == 0)
	{
		elog(ERROR, "pgraft: address cannot be empty (similar to etcd listen-peer-urls)");
	}

	if (pgraft_port < 1024 || pgraft_port > 65535)
	{
		elog(ERROR, "pgraft: Invalid port %d, must be between 1024 and 65535", pgraft_port);
	}

	/* Validate consensus settings (similar to etcd validation) */
	if (pgraft_heartbeat_interval < 10 || pgraft_heartbeat_interval > 10000)
	{
		elog(ERROR, "pgraft: Invalid heartbeat_interval %d, must be between 10 and 10000 ms", 
			 pgraft_heartbeat_interval);
	}

	if (pgraft_election_timeout < 100 || pgraft_election_timeout > 30000)
	{
		elog(ERROR, "pgraft: Invalid election_timeout %d, must be between 100 and 30000 ms", 
			 pgraft_election_timeout);
	}

	/* Election timeout should be at least 5x heartbeat interval (etcd recommendation) */
	if (pgraft_election_timeout < (pgraft_heartbeat_interval * 5))
	{
		elog(WARNING, "pgraft: election_timeout (%d ms) should be at least 5x heartbeat_interval (%d ms) for stability",
			 pgraft_election_timeout, pgraft_heartbeat_interval);
	}

	/* Validate performance settings */
	if (pgraft_snapshot_interval < 100 || pgraft_snapshot_interval > 1000000)
	{
		elog(ERROR, "pgraft: Invalid snapshot_interval %d, must be between 100 and 1000000", 
			 pgraft_snapshot_interval);
	}

	if (pgraft_max_log_entries < 100 || pgraft_max_log_entries > 1000000)
	{
		elog(ERROR, "pgraft: Invalid max_log_entries %d, must be between 100 and 1000000", 
			 pgraft_max_log_entries);
	}

	/* Validate monitoring settings */
	if (pgraft_metrics_enabled && (pgraft_metrics_port < 1024 || pgraft_metrics_port > 65535))
	{
		elog(ERROR, "pgraft: Invalid metrics_port %d, must be between 1024 and 65535", 
			 pgraft_metrics_port);
	}

	/* Check for port conflicts */
	if (pgraft_metrics_enabled && pgraft_metrics_port == pgraft_port)
	{
		elog(ERROR, "pgraft: metrics_port (%d) cannot be the same as raft port (%d)", 
			 pgraft_metrics_port, pgraft_port);
	}

	elog(INFO, "pgraft: Configuration validation completed successfully");
	elog(DEBUG1, "pgraft: node_id=%d, cluster_id='%s', address='%s', port=%d", 
		 pgraft_node_id, 
		 pgraft_cluster_id ? pgraft_cluster_id : "(null)",
		 pgraft_address ? pgraft_address : "(null)",
		 pgraft_port);
}

/*
 * Initialize GUC variables
 */
void
pgraft_guc_init(void)
{
	elog(DEBUG1, "pgraft: Initializing GUC variables");
	/* GUC variables are automatically registered by DefineCustom*Variable */
}

/*
 * Shutdown GUC system
 */
void
pgraft_guc_shutdown(void)
{
	elog(DEBUG1, "pgraft: Shutting down GUC system");
	/* Cleanup if needed */
}