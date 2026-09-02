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
#include <limits.h>
#include <string.h>
#include <ctype.h>

/* etcd-compatible configuration variables - exact same names and types */
char	   *pgraft_guc_name = NULL;
char	   *pgraft_guc_data_dir = NULL;
char	   *pgraft_guc_initial_cluster = NULL;
char	   *pgraft_guc_initial_cluster_state = NULL;
char	   *pgraft_guc_initial_cluster_token = NULL;
char	   *pgraft_guc_initial_advertise_peer_urls = NULL;
char	   *pgraft_guc_advertise_client_urls = NULL;
char	   *pgraft_guc_listen_client_urls = NULL;
char	   *pgraft_guc_listen_peer_urls = NULL;

/* etcd-compatible consensus settings - exact same names and types */
int			pgraft_guc_election_timeout = 1000;
int			pgraft_guc_heartbeat_interval = 100;
int			pgraft_guc_snapshot_count = 10000;
int			pgraft_guc_quota_backend_bytes = 2147483647; /* 2GB default (max int32) */
int			pgraft_guc_max_request_bytes = 1572864; /* 1.5MB default */

/* etcd-compatible logging settings - exact same names and types */
char	   *pgraft_guc_log_level = "info";
char	   *pgraft_guc_log_outputs = "default";
char	   *pgraft_guc_log_package_levels = "";

/* etcd-compatible performance and security settings - exact same names and types */
int			pgraft_guc_max_snapshots = 5;
int			pgraft_guc_max_wals = 5;
char	   *pgraft_guc_auto_compaction_retention = "0";
char	   *pgraft_guc_auto_compaction_mode = "periodic";
int			pgraft_guc_compaction_batch_limit = 1000;
bool		pgraft_guc_client_cert_auth = false;
char	   *pgraft_guc_trusted_ca_file = "";
char	   *pgraft_guc_cert_file = "";
char	   *pgraft_guc_key_file = "";
char	   *pgraft_guc_client_cert_file = "";
char	   *pgraft_guc_client_key_file = "";
char	   *pgraft_guc_peer_trusted_ca_file = "";
char	   *pgraft_guc_peer_cert_file = "";
char	   *pgraft_guc_peer_key_file = "";
bool		pgraft_guc_peer_client_cert_auth = false;
char	   *pgraft_guc_peer_cert_allowed_cn = "";
bool		pgraft_guc_peer_cert_allowed_hostname = false;
char	   *pgraft_guc_cipher_suites = "";
char	   *pgraft_guc_cors = "";
char	   *pgraft_guc_host_whitelist = "";

/* etcd-compatible monitoring and experimental settings - exact same names and types */
char	   *pgraft_guc_listen_metrics_urls = "";
char	   *pgraft_guc_metrics = "basic";

/* PostgreSQL-specific settings (not in etcd) */
char	   *pgraft_guc_go_library_path = "";
int			pgraft_max_log_entries = 10000;
int			pgraft_batch_size = 100;
int			pgraft_max_batch_delay = 10;

/*
 * Register GUC variables
 * Organized similar to etcd configuration structure
 */
void
pgraft_register_guc_variables(void)
{
	/* ========================================
	 * etcd-compatible core cluster configuration
	 * Exact same parameter names and types as etcd
	 * ======================================== */
	
	DefineCustomStringVariable("pgraft.name",
							"Human-readable name for this member (same as etcd name)",
							"Must be unique across cluster members",
							&pgraft_guc_name,
							"default",
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.data_dir",
							   "Path to the data directory (same as etcd data-dir)",
							   "Directory to store raft log and snapshots",
							   &pgraft_guc_data_dir,
							   "default.etcd",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.initial_cluster",
							   "Initial cluster configuration for bootstrapping (same as etcd initial-cluster)",
							   "Comma-separated list in format 'member1=http://peer1:2380,member2=http://peer2:2380'",
							   &pgraft_guc_initial_cluster,
							  "default=http://localhost:2380",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.initial_cluster_state",
							   "Initial cluster state (same as etcd initial-cluster-state)",
							   "Options: 'new', 'existing' - whether this is a new or existing cluster",
							   &pgraft_guc_initial_cluster_state,
							   "new",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.initial_cluster_token",
							   "Initial cluster token (same as etcd initial-cluster-token)",
							   "Token to prevent accidental cross-cluster communication",
							   &pgraft_guc_initial_cluster_token,
							   "etcd-cluster",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.initial_advertise_peer_urls",
							   "Initial advertised peer URLs (same as etcd initial-advertise-peer-urls)",
							   "URLs to advertise to peers",
							   &pgraft_guc_initial_advertise_peer_urls,
							   "http://localhost:2380",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.advertise_client_urls",
							   "Advertised client URLs (same as etcd advertise-client-urls)",
							   "URLs to advertise to clients",
							   &pgraft_guc_advertise_client_urls,
							   "http://localhost:2379",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.listen_client_urls",
							   "Client URLs (same as etcd listen-client-urls)",
							   "Comma-separated list of client URLs",
							   &pgraft_guc_listen_client_urls,
							   "http://localhost:2379",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.listen_peer_urls",
							   "Peer URLs (same as etcd listen-peer-urls)",
							   "Comma-separated list of peer URLs",
							   &pgraft_guc_listen_peer_urls,
							   "http://localhost:2380",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	/* ========================================
	 * etcd-compatible consensus/raft settings
	 * Exact same parameter names and types as etcd
	 * ======================================== */

	DefineCustomIntVariable("pgraft.election_timeout",
							"Election timeout in milliseconds (same as etcd election-timeout)",
							"Time before starting new election if no heartbeat received",
							&pgraft_guc_election_timeout,
							1000,
							100,
							30000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.heartbeat_interval",
							"Heartbeat interval in milliseconds (same as etcd heartbeat-interval)",
							"Frequency of heartbeat messages from leader",
							&pgraft_guc_heartbeat_interval,
							100,
							10,
							10000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.snapshot_count",
							"Number of committed transactions to trigger a snapshot (same as etcd snapshot-count)",
							"Number of committed entries before creating snapshot",
							&pgraft_guc_snapshot_count,
							10000,
							100,
							1000000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.quota_backend_bytes",
							"Raise alarm when backend size exceeds the given quota (same as etcd quota-backend-bytes)",
							"Soft limit for backend storage in bytes",
							&pgraft_guc_quota_backend_bytes,
							2147483647, /* 2GB default (max int32) */
							1048576,    /* 1MB minimum */
							INT_MAX, /* Use max int value */
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.max_request_bytes",
							"Maximum client request size in bytes (same as etcd max-request-bytes)",
							"Maximum size of request that server will accept",
							&pgraft_guc_max_request_bytes,
							1572864, /* 1.5MB default */
							1024,    /* 1KB minimum */
							67108864, /* 64MB maximum */
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	/* ========================================
	 * etcd-compatible logging configuration
	 * Exact same parameter names and types as etcd
	 * ======================================== */

	DefineCustomStringVariable("pgraft.log_level",
							"Log level for etcd (same as etcd log-level)",
							"Options: 'debug', 'info', 'warn', 'error', 'panic', 'fatal'",
							&pgraft_guc_log_level,
							"info",
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.log_outputs",
							   "Specify 'stdout' or 'stderr' to skip journald logging even when running under systemd (same as etcd log-outputs)",
							   "Comma-separated list of log output targets",
							   &pgraft_guc_log_outputs,
							   "default",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.log_package_levels",
							   "Specify a particular log level for each etcd package (same as etcd log-package-levels)",
							   "Example: 'etcdmain=CRITICAL,etcdserver=DEBUG'",
							   &pgraft_guc_log_package_levels,
							   "",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	/* ========================================
	 * etcd-compatible performance and security settings
	 * Exact same parameter names and types as etcd
	 * ======================================== */

	DefineCustomIntVariable("pgraft.max_snapshots",
							"Maximum number of snapshot files to retain (same as etcd max-snapshots)",
							"Number of snapshot files to retain",
							&pgraft_guc_max_snapshots,
							5,
							1,
							100,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.max_wals",
							"Maximum number of WAL files to retain (same as etcd max-wals)",
							"Number of WAL files to retain",
							&pgraft_guc_max_wals,
							5,
							1,
							100,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.auto_compaction_retention",
							   "Auto compaction retention length (same as etcd auto-compaction-retention)",
							   "Auto compaction retention length",
							   &pgraft_guc_auto_compaction_retention,
							   "0",
							   PGC_SIGHUP,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.auto_compaction_mode",
							   "Interpretation of 'auto-compaction-retention' (same as etcd auto-compaction-mode)",
							   "Interpretation of auto-compaction-retention",
							   &pgraft_guc_auto_compaction_mode,
							   "periodic",
							   PGC_SIGHUP,
							   0,
							   NULL,
							NULL,
							   NULL);

	DefineCustomIntVariable("pgraft.compaction_batch_limit",
							"Maximum number of keys to compact in one batch (same as etcd experimental-compaction-batch-limit)",
							"Maximum number of keys to compact in one batch",
							&pgraft_guc_compaction_batch_limit,
							1000,
							1,
							10000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	/* Security settings - exact etcd names */
	DefineCustomBoolVariable("pgraft.client_cert_auth",
							"Enable client cert authentication (same as etcd client-cert-auth)",
							"Require authentication for cluster communication",
							&pgraft_guc_client_cert_auth,
							false,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.trusted_ca_file",
							   "Path to the client server TLS CA file (same as etcd trusted-ca-file)",
							   "Path to the client server TLS CA file",
							   &pgraft_guc_trusted_ca_file,
							   "",
							   PGC_POSTMASTER,
                           0,
                           NULL,
                           NULL,
                           NULL);

	DefineCustomStringVariable("pgraft.cert_file",
							   "Path to the client server TLS cert file (same as etcd cert-file)",
							   "Path to the client server TLS cert file",
							   &pgraft_guc_cert_file,
							   "",
							   PGC_POSTMASTER,
                              0,
                              NULL,
                              NULL,
                              NULL);

	DefineCustomStringVariable("pgraft.key_file",
							   "Path to the client server TLS key file (same as etcd key-file)",
							   "Path to the client server TLS key file",
							   &pgraft_guc_key_file,
							   "",
							   PGC_POSTMASTER,
                           0,
                           NULL,
                           NULL,
                           NULL);

	DefineCustomStringVariable("pgraft.client_cert_file",
							   "Path to the client server TLS cert file (same as etcd client-cert-file)",
							   "Path to the client server TLS cert file",
							   &pgraft_guc_client_cert_file,
							   "",
							   PGC_POSTMASTER,
                              0,
                              NULL,
                              NULL,
                              NULL);

	DefineCustomStringVariable("pgraft.client_key_file",
							   "Path to the client server TLS key file (same as etcd client-key-file)",
							   "Path to the client server TLS key file",
							   &pgraft_guc_client_key_file,
							   "",
							   PGC_POSTMASTER,
                            0,
                            NULL,
                            NULL,
                            NULL);

	DefineCustomStringVariable("pgraft.peer_trusted_ca_file",
							   "Path to the peer server TLS CA file (same as etcd peer-trusted-ca-file)",
							   "Path to the peer server TLS CA file",
							   &pgraft_guc_peer_trusted_ca_file,
							   "",
							   PGC_POSTMASTER,
                              0,
                              NULL,
                              NULL,
                              NULL);

	DefineCustomStringVariable("pgraft.peer_cert_file",
							   "Path to the peer server TLS cert file (same as etcd peer-cert-file)",
							   "Path to the peer server TLS cert file",
							   &pgraft_guc_peer_cert_file,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.peer_key_file",
							   "Path to the peer server TLS key file (same as etcd peer-key-file)",
							   "Path to the peer server TLS key file",
							   &pgraft_guc_peer_key_file,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomBoolVariable("pgraft.peer_client_cert_auth",
							"Enable peer client cert authentication (same as etcd peer-client-cert-auth)",
							"Require authentication for peer communication",
							&pgraft_guc_peer_client_cert_auth,
							false,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.peer_cert_allowed_cn",
							   "Allowed CN for peer certs (same as etcd peer-cert-allowed-cn)",
							   "Allowed CN for peer certs",
							   &pgraft_guc_peer_cert_allowed_cn,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							NULL,
							   NULL);

	DefineCustomBoolVariable("pgraft.peer_cert_allowed_hostname",
							"Allowed hostname for peer certs (same as etcd peer-cert-allowed-hostname)",
							"Allowed hostname for peer certs",
							&pgraft_guc_peer_cert_allowed_hostname,
							false,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.cipher_suites",
							   "Comma-separated list of supported cipher suites (same as etcd cipher-suites)",
							   "Comma-separated list of supported cipher suites",
							   &pgraft_guc_cipher_suites,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.cors",
							   "Comma-separated whitelist of origins for CORS (same as etcd cors)",
							   "Comma-separated whitelist of origins for CORS",
							   &pgraft_guc_cors,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.host_whitelist",
							   "Comma-separated whitelist of origins for CORS (same as etcd host-whitelist)",
							   "Comma-separated whitelist of origins for CORS",
							   &pgraft_guc_host_whitelist,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	/* Monitoring settings - exact etcd names */
	DefineCustomStringVariable("pgraft.listen_metrics_urls",
							   "List of URLs to listen on for metrics (same as etcd listen-metrics-urls)",
							   "List of URLs to listen on for metrics",
							   &pgraft_guc_listen_metrics_urls,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("pgraft.metrics",
							   "Set of metrics to expose (same as etcd metrics)",
							   "Set of metrics to expose",
							   &pgraft_guc_metrics,
							   "basic",
							   PGC_SIGHUP,
							   0,
							   NULL,
							NULL,
							   NULL);

	/* Experimental settings - exact etcd names */

	/* ========================================
	 * Legacy/compatibility settings
	 * (Kept for backward compatibility)
	 * ======================================== */


	/* ========================================
	 * PostgreSQL-specific settings (not in etcd)
	 * ======================================== */


	DefineCustomStringVariable("pgraft.go_library_path",
							   "Path to the Go library",
							   "Path to the pgraft Go library file (.so on Linux, .dylib on macOS)",
							   &pgraft_guc_go_library_path,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomIntVariable("pgraft.max_log_entries",
							"Maximum log entries",
							"Maximum number of log entries to retain",
							&pgraft_max_log_entries,
							10000,
							100,
							1000000,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.batch_size",
							"Batch size for operations",
							"Maximum entries per batch operation",
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
							"Maximum time to wait accumulating entries before sending",
							&pgraft_max_batch_delay,
							10,
							1,
							1000,
							PGC_SIGHUP,
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
	if (!pgraft_guc_initial_cluster_token || strlen(pgraft_guc_initial_cluster_token) == 0)
	{
		elog(ERROR, "pgraft: initial_cluster_token must be set");
	}

	if (!pgraft_guc_listen_peer_urls || strlen(pgraft_guc_listen_peer_urls) == 0)
	{
		elog(ERROR, "pgraft: listen_peer_urls must be set");
	}
	
	/* Validate initial_cluster format */
	if (!pgraft_guc_initial_cluster || strlen(pgraft_guc_initial_cluster) == 0)
	{
		elog(ERROR, "pgraft: initial_cluster must be set");
	}
	else
	{
		char	   *cluster_str;
		char	   *member_str;
		char	   *saveptr;
		int			member_count = 0;
		
		/* Parse and validate initial_cluster members */
		cluster_str = pstrdup(pgraft_guc_initial_cluster);
		member_str = strtok_r(cluster_str, ",", &saveptr);
		while (member_str != NULL)
		{
			char	   *equals_pos;
			char	   *name_part;
			char	   *url_part;
			
			/* Trim whitespace */
			while (*member_str && isspace((unsigned char) *member_str))
				member_str++;
			
			if (*member_str == '\0')
			{
				member_str = strtok_r(NULL, ",", &saveptr);
				continue;
			}
			
			/* Find the '=' separator */
			equals_pos = strchr(member_str, '=');
			if (!equals_pos)
			{
				pfree(cluster_str);
				elog(ERROR, "pgraft: invalid member format in initial_cluster: %s (expected name=url)", member_str);
			}
			
			/* Split name and URL */
			*equals_pos = '\0';
			name_part = member_str;
			url_part = equals_pos + 1;
			
			/* Validate name */
			if (strlen(name_part) == 0)
			{
				pfree(cluster_str);
				elog(ERROR, "pgraft: empty member name in initial_cluster");
			}
			
			/* Validate URL format */
			if (!strstr(url_part, "http://") && !strstr(url_part, "https://"))
			{
				pfree(cluster_str);
				elog(ERROR, "pgraft: invalid peer URL format: %s (must start with http:// or https://)", url_part);
			}
			
			member_count++;
			elog(DEBUG2, "pgraft: validated cluster member: %s -> %s", name_part, url_part);
			member_str = strtok_r(NULL, ",", &saveptr);
		}
		pfree(cluster_str);
		
		if (member_count < 1)
		{
			elog(ERROR, "pgraft: initial_cluster must contain at least 1 member");
		}
		
		elog(DEBUG1, "pgraft: validated %d cluster members in initial_cluster", member_count);
	}
	
	/* Validate cluster state */
	if (pgraft_guc_initial_cluster_state && strlen(pgraft_guc_initial_cluster_state) > 0)
	{
		if (strcmp(pgraft_guc_initial_cluster_state, "new") != 0 && strcmp(pgraft_guc_initial_cluster_state, "existing") != 0)
		{
			elog(ERROR, "pgraft: invalid initial_cluster_state '%s', must be 'new' or 'existing'", pgraft_guc_initial_cluster_state);
		}
	}

	/* Validate consensus settings */
	if (pgraft_guc_heartbeat_interval < 10 || pgraft_guc_heartbeat_interval > 10000)
	{
		elog(ERROR, "pgraft: invalid heartbeat_interval %d, must be between 10 and 10000 ms", 
			 pgraft_guc_heartbeat_interval);
	}

	if (pgraft_guc_election_timeout < 100 || pgraft_guc_election_timeout > 30000)
	{
		elog(ERROR, "pgraft: invalid election_timeout %d, must be between 100 and 30000 ms", 
			 pgraft_guc_election_timeout);
	}

	/* Election timeout should be at least 5x heartbeat interval */
	if (pgraft_guc_election_timeout < (pgraft_guc_heartbeat_interval * 5))
	{
		elog(WARNING, "pgraft: election_timeout (%d ms) should be at least 5x heartbeat_interval (%d ms) for stability",
			 pgraft_guc_election_timeout, pgraft_guc_heartbeat_interval);
	}

	/* Validate performance settings */
	if (pgraft_guc_snapshot_count < 100 || pgraft_guc_snapshot_count > 1000000)
	{
		elog(ERROR, "pgraft: invalid snapshot_count %d, must be between 100 and 1000000", 
			 pgraft_guc_snapshot_count);
	}

	if (pgraft_max_log_entries < 100 || pgraft_max_log_entries > 1000000)
	{
		elog(ERROR, "pgraft: invalid max_log_entries %d, must be between 100 and 1000000", 
			 pgraft_max_log_entries);
	}

	/* Validate monitoring settings */
	if (pgraft_guc_listen_metrics_urls && strlen(pgraft_guc_listen_metrics_urls) > 0)
	{
		/* Parse metrics URLs to validate port ranges */
		elog(DEBUG1, "pgraft: validating listen_metrics_urls: %s", pgraft_guc_listen_metrics_urls);
	}

	elog(INFO, "pgraft: configuration validation completed successfully");
	elog(DEBUG1, "pgraft: name='%s', initial_cluster_token='%s', listen_peer_urls='%s'", 
		 pgraft_guc_name ? pgraft_guc_name : "(null)",
		 pgraft_guc_initial_cluster_token ? pgraft_guc_initial_cluster_token : "(null)",
		 pgraft_guc_listen_peer_urls ? pgraft_guc_listen_peer_urls : "(null)");
}

/*
 * Initialize GUC variables
 */
void
pgraft_guc_init(void)
{
	elog(DEBUG1, "pgraft: initializing GUC variables");
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

/*
 * Configuration parsing functions for etcd-compatible parameters
 */

/*
 * Parse comma-separated URL list
 */
/*
 * Parse a single URL into host and port
 * Format: http://host:port or https://host:port
 * Returns: true on success, false on error
 */
bool
pgraft_parse_url(const char *url_str, char **host, int *port)
{
	const char *start;
	const char *colon_pos;
	const char *end;
	int			host_len;

	if (!url_str || strlen(url_str) == 0)
	{
		elog(WARNING, "pgraft: empty URL string");
		return false;
	}

	/* Skip http:// or https:// prefix */
	start = url_str;
	if (strncmp(start, "http://", 7) == 0)
	{
		start += 7;
	}
	else if (strncmp(start, "https://", 8) == 0)
	{
		start += 8;
	}

	/* Find the colon that separates host and port */
	colon_pos = strchr(start, ':');
	if (!colon_pos)
	{
		elog(WARNING, "pgraft: no port found in URL: %s", url_str);
		return false;
	}

	/* Extract host */
	host_len = colon_pos - start;
	*host = (char *) palloc(host_len + 1);
	strncpy(*host, start, host_len);
	(*host)[host_len] = '\0';

	/* Extract port */
	end = colon_pos + 1;
	*port = atoi(end);

	if (*port <= 0 || *port > 65535)
	{
		elog(WARNING, "pgraft: invalid port %d in URL: %s", *port, url_str);
		pfree(*host);
		return false;
	}

	elog(DEBUG2, "pgraft: parsed URL '%s' -> host='%s', port=%d", url_str, *host, *port);
	return true;
}

void
pgraft_parse_url_list(const char *url_str, pgraft_url_list_t *url_list)
{
	char	   *str_copy;
	char	   *url_token;
	char	   *saveptr;
	int			url_count = 0;
	int			i;

	/* Initialize the structure */
	url_list->urls = NULL;
	url_list->count = 0;

	if (!url_str || strlen(url_str) == 0)
	{
		return;
	}
	
	/* Count URLs first */
	str_copy = pstrdup(url_str);
	url_token = strtok_r(str_copy, ",", &saveptr);
	while (url_token != NULL)
	{
		/* Trim whitespace */
		while (*url_token && isspace((unsigned char) *url_token))
			url_token++;
		
		if (*url_token != '\0')
		{
			url_count++;
		}
		url_token = strtok_r(NULL, ",", &saveptr);
	}
	pfree(str_copy);
	
	if (url_count == 0)
	{
		return;
	}
	
	/* Allocate array for URLs */
	url_list->urls = (char **) palloc(url_count * sizeof(char *));
	url_list->count = url_count;
	
	/* Parse URLs into array */
	str_copy = pstrdup(url_str);
	url_token = strtok_r(str_copy, ",", &saveptr);
	i = 0;
	while (url_token != NULL && i < url_count)
	{
		/* Trim whitespace */
		while (*url_token && isspace((unsigned char) *url_token))
			url_token++;
		
		if (*url_token != '\0')
		{
			url_list->urls[i] = pstrdup(url_token);
			i++;
		}
		url_token = strtok_r(NULL, ",", &saveptr);
	}
	pfree(str_copy);
	
	elog(DEBUG2, "pgraft: parsed %d URLs from '%s'", url_count, url_str);
}

/*
 * Parse initial_cluster string into cluster members
 */
void
pgraft_parse_initial_cluster(const char *cluster_str, pgraft_cluster_member_t **members, int *count)
{
	char	   *str_copy;
	char	   *member_token;
	char	   *saveptr;
	int			member_count = 0;
	int			i;
	
	/* Initialize */
	*members = NULL;
	*count = 0;
	
	if (!cluster_str || strlen(cluster_str) == 0)
	{
		elog(WARNING, "pgraft: initial_cluster is empty");
		return;
	}
	
	/* Count members first */
	str_copy = pstrdup(cluster_str);
	member_token = strtok_r(str_copy, ",", &saveptr);
	while (member_token != NULL)
	{
		/* Trim whitespace */
		while (*member_token && isspace((unsigned char) *member_token))
			member_token++;
		
		if (*member_token != '\0')
		{
			member_count++;
		}
		member_token = strtok_r(NULL, ",", &saveptr);
	}
	pfree(str_copy);
	
	if (member_count == 0)
	{
		elog(WARNING, "pgraft: no valid members found in initial_cluster");
		return;
	}
	
	/* Allocate array for members */
	*members = (pgraft_cluster_member_t *) palloc(member_count * sizeof(pgraft_cluster_member_t));
	*count = member_count;
	
	/* Parse members into array */
	str_copy = pstrdup(cluster_str);
	member_token = strtok_r(str_copy, ",", &saveptr);
	i = 0;
	while (member_token != NULL && i < member_count)
	{
		char	   *equals_pos;
		char	   *name_part;
		char	   *url_part;
		
		/* Trim whitespace */
		while (*member_token && isspace((unsigned char) *member_token))
			member_token++;
		
		if (*member_token == '\0')
		{
			member_token = strtok_r(NULL, ",", &saveptr);
			continue;
		}
		
		/* Find the '=' separator */
		equals_pos = strchr(member_token, '=');
		if (!equals_pos)
		{
			elog(ERROR, "pgraft: invalid member format in initial_cluster: %s (expected name=url)", member_token);
			pfree(str_copy);
			return;
		}
		
		/* Split name and URL */
		*equals_pos = '\0';
		name_part = member_token;
		url_part = equals_pos + 1;
		
		/* Trim whitespace from parts */
		while (*name_part && isspace((unsigned char) *name_part))
			name_part++;
		while (*url_part && isspace((unsigned char) *url_part))
			url_part++;
		
		/* Store member */
		(*members)[i].name = pstrdup(name_part);
		(*members)[i].peer_url = pstrdup(url_part);
		
		elog(DEBUG2, "pgraft: parsed cluster member: %s -> %s", name_part, url_part);
		i++;
		
		member_token = strtok_r(NULL, ",", &saveptr);
	}
	pfree(str_copy);
	
	elog(DEBUG1, "pgraft: parsed %d cluster members from initial_cluster", member_count);
}

/*
 * Parse all configuration from GUC variables into structured format
 */
void
pgraft_parse_configuration(pgraft_parsed_config_t *config)
{
	/* Initialize the structure */
	memset(config, 0, sizeof(pgraft_parsed_config_t));
	
	/* Parse cluster configuration */
	pgraft_parse_initial_cluster(pgraft_guc_initial_cluster, &config->cluster_members, &config->cluster_member_count);
	config->cluster_state = pgraft_guc_initial_cluster_state ? pstrdup(pgraft_guc_initial_cluster_state) : NULL;
	config->cluster_token = pgraft_guc_initial_cluster_token ? pstrdup(pgraft_guc_initial_cluster_token) : NULL;
	
	/* Parse URL lists */
	pgraft_parse_url_list(pgraft_guc_listen_peer_urls, &config->peer_urls);
	pgraft_parse_url_list(pgraft_guc_listen_client_urls, &config->client_urls);
	pgraft_parse_url_list(pgraft_guc_initial_advertise_peer_urls, &config->advertise_peer_urls);
	pgraft_parse_url_list(pgraft_guc_advertise_client_urls, &config->advertise_client_urls);
	pgraft_parse_url_list(pgraft_guc_listen_metrics_urls, &config->metrics_urls);
	
	/* Parse consensus settings */
	config->election_timeout = pgraft_guc_election_timeout;
	config->heartbeat_interval = pgraft_guc_heartbeat_interval;
	config->snapshot_count = pgraft_guc_snapshot_count;
	config->quota_backend_bytes = pgraft_guc_quota_backend_bytes;
	config->max_request_bytes = pgraft_guc_max_request_bytes;
	
	/* Parse logging settings */
	config->log_level = pgraft_guc_log_level ? pstrdup(pgraft_guc_log_level) : NULL;
	config->log_outputs = pgraft_guc_log_outputs ? pstrdup(pgraft_guc_log_outputs) : NULL;
	config->log_package_levels = pgraft_guc_log_package_levels ? pstrdup(pgraft_guc_log_package_levels) : NULL;
	
	/* Parse storage settings */
	config->max_snapshots = pgraft_guc_max_snapshots;
	config->max_wals = pgraft_guc_max_wals;
	config->auto_compaction_retention = pgraft_guc_auto_compaction_retention ? pstrdup(pgraft_guc_auto_compaction_retention) : NULL;
	config->auto_compaction_mode = pgraft_guc_auto_compaction_mode ? pstrdup(pgraft_guc_auto_compaction_mode) : NULL;
	config->compaction_batch_limit = pgraft_guc_compaction_batch_limit;
	
	/* Parse security settings */
	config->client_cert_auth = pgraft_guc_client_cert_auth;
	config->trusted_ca_file = pgraft_guc_trusted_ca_file ? pstrdup(pgraft_guc_trusted_ca_file) : NULL;
	config->cert_file = pgraft_guc_cert_file ? pstrdup(pgraft_guc_cert_file) : NULL;
	config->key_file = pgraft_guc_key_file ? pstrdup(pgraft_guc_key_file) : NULL;
	config->client_cert_file = pgraft_guc_client_cert_file ? pstrdup(pgraft_guc_client_cert_file) : NULL;
	config->client_key_file = pgraft_guc_client_key_file ? pstrdup(pgraft_guc_client_key_file) : NULL;
	config->peer_trusted_ca_file = pgraft_guc_peer_trusted_ca_file ? pstrdup(pgraft_guc_peer_trusted_ca_file) : NULL;
	config->peer_cert_file = pgraft_guc_peer_cert_file ? pstrdup(pgraft_guc_peer_cert_file) : NULL;
	config->peer_key_file = pgraft_guc_peer_key_file ? pstrdup(pgraft_guc_peer_key_file) : NULL;
	config->peer_client_cert_auth = pgraft_guc_peer_client_cert_auth;
	config->peer_cert_allowed_cn = pgraft_guc_peer_cert_allowed_cn ? pstrdup(pgraft_guc_peer_cert_allowed_cn) : NULL;
	config->peer_cert_allowed_hostname = pgraft_guc_peer_cert_allowed_hostname;
	
	/* Parse network settings */
	config->cipher_suites = pgraft_guc_cipher_suites ? pstrdup(pgraft_guc_cipher_suites) : NULL;
	config->cors = pgraft_guc_cors ? pstrdup(pgraft_guc_cors) : NULL;
	config->host_whitelist = pgraft_guc_host_whitelist ? pstrdup(pgraft_guc_host_whitelist) : NULL;
	config->metrics = pgraft_guc_metrics ? pstrdup(pgraft_guc_metrics) : NULL;
	
	
	elog(INFO, "pgraft: parsed configuration with %d cluster members", config->cluster_member_count);
}

/*
 * Free parsed configuration structure
 */
void
pgraft_free_parsed_config(pgraft_parsed_config_t *config)
{
	int i;
	
	if (!config)
		return;
	
	/* Free cluster members */
	if (config->cluster_members)
	{
		for (i = 0; i < config->cluster_member_count; i++)
		{
			if (config->cluster_members[i].name)
				pfree(config->cluster_members[i].name);
			if (config->cluster_members[i].peer_url)
				pfree(config->cluster_members[i].peer_url);
		}
		pfree(config->cluster_members);
	}
	
	/* Free URL lists */
	if (config->peer_urls.urls)
	{
		for (i = 0; i < config->peer_urls.count; i++)
			pfree(config->peer_urls.urls[i]);
		pfree(config->peer_urls.urls);
	}
	
	if (config->client_urls.urls)
	{
		for (i = 0; i < config->client_urls.count; i++)
			pfree(config->client_urls.urls[i]);
		pfree(config->client_urls.urls);
	}
	
	if (config->advertise_peer_urls.urls)
	{
		for (i = 0; i < config->advertise_peer_urls.count; i++)
			pfree(config->advertise_peer_urls.urls[i]);
		pfree(config->advertise_peer_urls.urls);
	}
	
	if (config->advertise_client_urls.urls)
	{
		for (i = 0; i < config->advertise_client_urls.count; i++)
			pfree(config->advertise_client_urls.urls[i]);
		pfree(config->advertise_client_urls.urls);
	}
	
	if (config->metrics_urls.urls)
	{
		for (i = 0; i < config->metrics_urls.count; i++)
			pfree(config->metrics_urls.urls[i]);
		pfree(config->metrics_urls.urls);
	}
	
	/* Free string fields */
	if (config->cluster_state) pfree(config->cluster_state);
	if (config->cluster_token) pfree(config->cluster_token);
	if (config->log_level) pfree(config->log_level);
	if (config->log_outputs) pfree(config->log_outputs);
	if (config->log_package_levels) pfree(config->log_package_levels);
	if (config->auto_compaction_retention) pfree(config->auto_compaction_retention);
	if (config->auto_compaction_mode) pfree(config->auto_compaction_mode);
	if (config->trusted_ca_file) pfree(config->trusted_ca_file);
	if (config->cert_file) pfree(config->cert_file);
	if (config->key_file) pfree(config->key_file);
	if (config->client_cert_file) pfree(config->client_cert_file);
	if (config->client_key_file) pfree(config->client_key_file);
	if (config->peer_trusted_ca_file) pfree(config->peer_trusted_ca_file);
	if (config->peer_cert_file) pfree(config->peer_cert_file);
	if (config->peer_key_file) pfree(config->peer_key_file);
	if (config->peer_cert_allowed_cn) pfree(config->peer_cert_allowed_cn);
	if (config->cipher_suites) pfree(config->cipher_suites);
	if (config->cors) pfree(config->cors);
	if (config->host_whitelist) pfree(config->host_whitelist);
	if (config->metrics) pfree(config->metrics);
	
	/* Zero out the structure */
	memset(config, 0, sizeof(pgraft_parsed_config_t));
}

