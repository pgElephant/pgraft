
# Configuration

This page describes all available configuration parameters for **pgraft**.

## PostgreSQL Configuration

All pgraft configuration parameters are set in `postgresql.conf`. The extension must be added to `shared_preload_libraries` and PostgreSQL must be restarted for changes to take effect.

```ini
shared_preload_libraries = 'pgraft'
```

## Core Cluster Configuration

These parameters define the basic cluster identity and network settings.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pgraft.cluster_id` | string | "pgraft-cluster" | Cluster identifier - must be same for all nodes |
| `pgraft.node_id` | int | 1 | Unique node ID (1-based) - must be unique per node |
| `pgraft.address` | string | "127.0.0.1" | Node listen address for Raft communication |
| `pgraft.port` | int | 7001 | Raft communication port (not PostgreSQL port) |
| `pgraft.data_dir` | string | "/tmp/pgraft/${node_id}" | Persistent storage directory |

### Example

```ini
pgraft.cluster_id = 'production-cluster'
pgraft.node_id = 1
pgraft.address = '127.0.0.1'
pgraft.port = 7001
pgraft.data_dir = '/var/lib/postgresql/pgraft'
```

!!! warning "Important"
    - `cluster_id` must be identical on all nodes in the cluster
    - `node_id` must be unique for each node (1, 2, 3, ...)
    - `port` is for Raft protocol, not PostgreSQL connections

## Consensus Settings

These parameters control the Raft consensus algorithm behavior.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pgraft.election_timeout` | int | 1000 | Election timeout in milliseconds |
| `pgraft.heartbeat_interval` | int | 100 | Heartbeat interval in milliseconds |
| `pgraft.snapshot_interval` | int | 10000 | Snapshot frequency (entries) |
| `pgraft.max_log_entries` | int | 1000 | Log compaction threshold |

### Example

```ini
pgraft.election_timeout = 1000        # milliseconds
pgraft.heartbeat_interval = 100       # milliseconds
pgraft.snapshot_interval = 10000      # entries
pgraft.max_log_entries = 1000         # compaction threshold
```

### Tuning Guidelines

**Election Timeout**

- Typical range: 500-5000ms
- Higher values: More stable but slower failover
- Lower values: Faster failover but more prone to spurious elections
- Should be **at least 10x** the heartbeat interval

**Heartbeat Interval**

- Typical range: 50-200ms
- Determines how often leader sends heartbeats
- Lower values: Better detection of failures, more network traffic
- Higher values: Less network traffic, slower failure detection

**Snapshot Interval**

- How many log entries before creating a snapshot
- Affects recovery time and disk usage
- Typical range: 1000-100000 entries

**Max Log Entries**

- Threshold for log compaction
- Prevents unbounded log growth
- Should be larger than snapshot_interval

## Performance Settings

These parameters control batching and compaction behavior.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pgraft.batch_size` | int | 100 | Entry batch size for replication |
| `pgraft.max_batch_delay` | int | 10 | Max batching delay in milliseconds |
| `pgraft.compaction_threshold` | int | 10000 | Compaction trigger threshold |

### Example

```ini
pgraft.batch_size = 100
pgraft.max_batch_delay = 10           # milliseconds
pgraft.compaction_threshold = 10000
```

## Security & Monitoring

Optional security and monitoring features.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pgraft.auth_enabled` | bool | false | Enable authentication between nodes |
| `pgraft.tls_enabled` | bool | false | Enable TLS for inter-node communication |
| `pgraft.metrics_enabled` | bool | false | Enable Prometheus metrics |
| `pgraft.metrics_port` | int | 9100 | Metrics server port |

### Example

```ini
pgraft.auth_enabled = false
pgraft.tls_enabled = false
pgraft.metrics_enabled = true
pgraft.metrics_port = 9100
```

!!! note "Coming Soon"
    Authentication and TLS features are planned for future releases.

## Complete Configuration Example

Here's a complete configuration for a production 3-node cluster:

=== "Node 1"
    ```ini
    # PostgreSQL settings
    port = 5432
    shared_preload_libraries = 'pgraft'

    # Core cluster configuration
    pgraft.cluster_id = 'prod-cluster'
    pgraft.node_id = 1
    pgraft.address = '192.168.1.101'
    pgraft.port = 7001
    pgraft.data_dir = '/var/lib/postgresql/pgraft'

    # Consensus settings
    pgraft.election_timeout = 1000
    pgraft.heartbeat_interval = 100
    pgraft.snapshot_interval = 10000
    pgraft.max_log_entries = 1000

    # Performance settings
    pgraft.batch_size = 100
    pgraft.max_batch_delay = 10
    pgraft.compaction_threshold = 10000

    # Monitoring
    pgraft.metrics_enabled = true
    pgraft.metrics_port = 9100
    ```

=== "Node 2"
    ```ini
    # PostgreSQL settings
    port = 5432
    shared_preload_libraries = 'pgraft'

    # Core cluster configuration
    pgraft.cluster_id = 'prod-cluster'
    pgraft.node_id = 2
    pgraft.address = '192.168.1.102'
    pgraft.port = 7002
    pgraft.data_dir = '/var/lib/postgresql/pgraft'

    # Consensus settings (same as node 1)
    pgraft.election_timeout = 1000
    pgraft.heartbeat_interval = 100
    pgraft.snapshot_interval = 10000
    pgraft.max_log_entries = 1000

    # Performance settings (same as node 1)
    pgraft.batch_size = 100
    pgraft.max_batch_delay = 10
    pgraft.compaction_threshold = 10000

    # Monitoring
    pgraft.metrics_enabled = true
    pgraft.metrics_port = 9100
    ```

=== "Node 3"
    ```ini
    # PostgreSQL settings
    port = 5432
    shared_preload_libraries = 'pgraft'

    # Core cluster configuration
    pgraft.cluster_id = 'prod-cluster'
    pgraft.node_id = 3
    pgraft.address = '192.168.1.103'
    pgraft.port = 7003
    pgraft.data_dir = '/var/lib/postgresql/pgraft'

    # Consensus settings (same as node 1)
    pgraft.election_timeout = 1000
    pgraft.heartbeat_interval = 100
    pgraft.snapshot_interval = 10000
    pgraft.max_log_entries = 1000

    # Performance settings (same as node 1)
    pgraft.batch_size = 100
    pgraft.max_batch_delay = 10
    pgraft.compaction_threshold = 10000

    # Monitoring
    pgraft.metrics_enabled = true
    pgraft.metrics_port = 9100
    ```

## Applying Configuration Changes

After modifying `postgresql.conf`:

```bash
# Restart PostgreSQL
pg_ctl restart -D /path/to/data

# Or reload (for parameters that support reload)
pg_ctl reload -D /path/to/data
```

!!! warning "Restart Required"
    Most pgraft parameters require a PostgreSQL restart because the extension is loaded via `shared_preload_libraries`.

## Verifying Configuration

After starting PostgreSQL, verify your configuration:

```sql
-- Check if pgraft is loaded
SELECT * FROM pg_extension WHERE extname = 'pgraft';

-- Get current cluster configuration
SELECT * FROM pgraft_get_cluster_status();

-- Check worker status
SELECT pgraft_get_worker_state();
```

