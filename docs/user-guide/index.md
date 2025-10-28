
# User Guide

Comprehensive guides for using **pgraft** in production.

## Overview

This section contains detailed documentation for operating pgraft clusters, from basic configuration to advanced cluster operations.

## Contents

- [Complete Tutorial](tutorial.md){ .md-button .md-button--primary } — Step-by-step guide covering installation, configuration, and advanced usage scenarios
- [Configuration Guide](configuration.md){ .md-button } — Complete reference of all configuration parameters and tuning guidelines
- [SQL Functions](sql-functions.md){ .md-button } — Comprehensive reference for all SQL functions provided by pgraft
- [Cluster Operations](cluster-operations.md){ .md-button } — Learn how to add/remove nodes, handle elections, and perform maintenance

## Quick Links

**Common Tasks:**
- **Initialize a node**: `SELECT pgraft_init();`
- **Check if leader**: `SELECT pgraft_is_leader();`
- **Add a node**: `SELECT pgraft_add_node(node_id, address, port);`
- **Get cluster status**: `SELECT * FROM pgraft_get_cluster_status();`
- **View all nodes**: `SELECT * FROM pgraft_get_nodes();`

**Key Configuration Parameters:**
- `pgraft.cluster_id` — Cluster identifier (must match on all nodes)
- `pgraft.node_id` — Unique node identifier
- `pgraft.address` — Node listen address
- `pgraft.port` — Raft communication port
- `pgraft.election_timeout` — Election timeout in milliseconds

**Common Patterns:**

Three-node cluster setup:
```sql
-- On each node
CREATE EXTENSION pgraft;
SELECT pgraft_init();

-- On leader (after 10 seconds)
SELECT pgraft_add_node(2, '192.168.1.12', 7002);
SELECT pgraft_add_node(3, '192.168.1.13', 7003);
```

Health check:
```sql
SELECT 
    pgraft_is_leader() as is_leader,
    pgraft_get_term() as term,
    pgraft_get_leader() as leader_id,
    pgraft_get_worker_state() as worker;
```

## Best Practices

1. **Use odd number of nodes** (3, 5, or 7) for optimal fault tolerance
2. **Always add nodes from the leader** — Configuration replicates automatically
3. **Monitor continuously** — Track leader, term, and worker status
4. **Test failover scenarios** before going to production
5. **Use fast storage** — SSD or NVMe recommended

## Next Steps

- **New users**: Start with the [Complete Tutorial](tutorial.md)
- **Configuring pgraft**: See [Configuration Reference](configuration.md)
- **Need SQL reference**: Check [SQL Functions](sql-functions.md)
- **Managing clusters**: Read [Cluster Operations](cluster-operations.md)

