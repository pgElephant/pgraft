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

/* Core GUC variables */
int			pgraft_node_id = 1;
int			pgraft_port = 0;	/* Will be set from configuration */
char	   *pgraft_address = NULL;
int			pgraft_log_level = 1;
int			pgraft_heartbeat_interval = 1000;
int			pgraft_election_timeout = 5000;
bool		pgraft_worker_enabled = true;
int			pgraft_worker_interval = 1000;
char	   *pgraft_cluster_name = NULL;
int			pgraft_cluster_size = 3;
bool		pgraft_enable_auto_cluster_formation = true;
char	   *pgraft_peers = NULL;

/* Additional GUCs for cluster management */
char	   *pgraft_node_name = NULL;
char	   *pgraft_node_ip = NULL;
bool		pgraft_is_primary = false;
int			pgraft_health_period_ms = 5000;
bool		pgraft_health_verbose = false;

/* Metrics and debugging GUCs */
bool		pgraft_metrics_enabled = true;
bool		pgraft_trace_enabled = false;

/*
 * Register GUC variables
 */
void
pgraft_register_guc_variables(void)
{
	/* Core configuration */
	DefineCustomIntVariable("pgraft.node_id",
							"Node ID for this instance",
							NULL,
							&pgraft_node_id,
							1,
							1,
							1000,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.port",
							"Port for pgraft communication",
							NULL,
							&pgraft_port,
							0,	/* Default to 0, will be set from config */
							1,
							65535,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomStringVariable("pgraft.address",
							   "Address for pgraft communication",
							   NULL,
							   &pgraft_address,
							   NULL,	/* No default, must be configured */
							   PGC_SUSET,
							   0,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomIntVariable("pgraft.log_level",
							"Log level for pgraft (0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR)",
							NULL,
							&pgraft_log_level,
							1,
							0,
							3,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.heartbeat_interval",
							"Heartbeat interval in milliseconds",
							NULL,
							&pgraft_heartbeat_interval,
							1000,
							100,
							60000,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomIntVariable("pgraft.election_timeout",
							"Election timeout in milliseconds",
							NULL,
							&pgraft_election_timeout,
							5000,
							1000,
							30000,
							PGC_SUSET,
							0,
							NULL,
							NULL,
							NULL);

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

    /* Cluster configuration */
    DefineCustomStringVariable("pgraft.cluster_name",
                              "Name of the cluster",
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

    DefineCustomStringVariable("pgraft.peers",
                              "Comma-separated list of cluster peers in format 'id:address:port'",
                              NULL,
                              &pgraft_peers,
                              "",
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

    /* Additional cluster management GUCs */
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
							   NULL,	/* No default, must be configured */
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

	/* Metrics and debugging GUCs */
	DefineCustomBoolVariable("pgraft.metrics_enabled",
							"Enable metrics collection",
							NULL,
							&pgraft_metrics_enabled,
							true,
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
}

/*
 * Validate configuration
 */
void
pgraft_validate_configuration(void)
{
	/* Validate node ID */
	if (pgraft_node_id < 1 || pgraft_node_id > 1000)
	{
		elog(ERROR, "pgraft: Invalid node_id %d, must be between 1 and 1000", pgraft_node_id);
	}

	/* Validate address */
	if (!pgraft_address || strlen(pgraft_address) == 0)
	{
		elog(ERROR, "pgraft: Address cannot be empty");
	}

	/* Validate port */
	if (pgraft_port < 1 || pgraft_port > 65535)
	{
		elog(ERROR, "pgraft: Invalid port %d, must be between 1 and 65535", pgraft_port);
	}

	/* Validate heartbeat interval */
	if (pgraft_heartbeat_interval < 100 || pgraft_heartbeat_interval > 60000)
	{
		elog(ERROR, "pgraft: Invalid heartbeat_interval %d, must be between 100 and 60000 ms", 
			 pgraft_heartbeat_interval);
	}

	/* Validate election timeout */
	if (pgraft_election_timeout < 1000 || pgraft_election_timeout > 30000)
	{
		elog(ERROR, "pgraft: Invalid election_timeout %d, must be between 1000 and 30000 ms", 
			 pgraft_election_timeout);
	}

	elog(DEBUG1, "pgraft: Configuration validation completed successfully");
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