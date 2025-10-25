# Monitoring

This page describes how to monitor your pgraft cluster for health, performance, and troubleshooting.

## Quick Health Check

Run this query on any node to get a quick overview:

```sql
SELECT 
    pgraft_is_leader() as is_leader,
    pgraft_get_term() as term,
    pgraft_get_leader() as leader_id,
    pgraft_get_worker_state() as worker;
```

**Expected output:**
```
 is_leader | term | leader_id | worker  
-----------+------+-----------+---------
 f         |   42 |         1 | RUNNING
```

## Cluster Status

### Get Detailed Cluster Status

```sql
SELECT * FROM pgraft_get_cluster_status();
```

**Output includes:**
- Current term
- Leader ID
- Node state (Leader/Follower/Candidate)
- Number of nodes in cluster

### List All Nodes

```sql
SELECT * FROM pgraft_get_nodes();
```

**Sample output:**
```
 node_id |   address   | port | is_leader 
---------+-------------+------+-----------
       1 | 127.0.0.1   | 7001 | t
       2 | 127.0.0.1   | 7002 | f
       3 | 127.0.0.1   | 7003 | f
```

## Worker Status

### Check Background Worker

The background worker is responsible for driving the Raft consensus:

```sql
SELECT pgraft_get_worker_state();
```

**Possible states:**
- `RUNNING`: Normal operation
- `STOPPED`: Worker not running
- `ERROR`: Worker encountered error

### Troubleshoot Worker Issues

If worker is not running:

```sql
-- 1. Check if extension is loaded
SELECT * FROM pg_extension WHERE extname = 'pgraft';

-- 2. Check shared_preload_libraries
SHOW shared_preload_libraries;

-- Should include 'pgraft'
```

## Log Monitoring

### Get Log Statistics

```sql
SELECT * FROM pgraft_log_get_stats();
```

**Output:**
```
 log_size | last_index | commit_index | last_applied 
----------+------------+--------------+--------------
     1000 |       1000 |          995 |          995
```

**What to monitor:**

- **log_size**: Total number of log entries
- **last_index**: Index of last log entry
- **commit_index**: Last committed entry
- **last_applied**: Last applied entry

!!! warning "Lag Detection"
    If `commit_index` is significantly behind `last_index`, followers may be lagging.

### Check Replication Status

On the leader:

```sql
SELECT * FROM pgraft_log_get_replication_status();
```

Shows replication progress for each follower.

## Performance Metrics

### Key Metrics to Monitor

| Metric | SQL Query | Normal Range | Alert If |
|--------|-----------|--------------|----------|
| Term stability | `SELECT pgraft_get_term();` | Stable | Frequent changes |
| Leader stability | `SELECT pgraft_get_leader();` | Stable | Frequent changes |
| Worker state | `SELECT pgraft_get_worker_state();` | RUNNING | Not RUNNING |
| Log lag | `SELECT * FROM pgraft_log_get_stats();` | commit_index ≈ last_index | Large difference |

### Monitoring Script

Create a monitoring script that runs periodically:

```sql
-- monitoring.sql
\set QUIET on
\pset format unaligned
\pset fieldsep ','
\pset tuples_only on

SELECT 
    now() as timestamp,
    pgraft_is_leader() as is_leader,
    pgraft_get_term() as term,
    pgraft_get_leader() as leader_id,
    pgraft_get_worker_state() as worker_state;

\pset tuples_only off
```

Run it:
```bash
psql -f monitoring.sql >> pgraft_metrics.csv
```

## Prometheus Metrics

Enable Prometheus metrics in `postgresql.conf`:

```ini
pgraft.metrics_enabled = true
pgraft.metrics_port = 9100
```

### Available Metrics

Metrics are exposed at `http://node-address:9100/metrics`:

- `pgraft_term`: Current Raft term
- `pgraft_leader_id`: Current leader ID
- `pgraft_is_leader`: 1 if leader, 0 if follower
- `pgraft_log_size`: Number of log entries
- `pgraft_commit_index`: Last committed index
- `pgraft_applied_index`: Last applied index

### Grafana Dashboard

Sample Prometheus queries for Grafana:

```promql
# Leader election frequency
rate(pgraft_term[5m])

# Cluster has leader (should be 1)
max(pgraft_is_leader)

# Log lag
pgraft_log_size - pgraft_commit_index
```

## Log Files

### PostgreSQL Logs

pgraft logs to PostgreSQL's standard log:

```bash
tail -f /path/to/postgresql/log/postgresql-*.log | grep pgraft
```

### Log Levels

Enable debug logging:

```sql
SELECT pgraft_set_debug(true);
```

Disable debug logging:

```sql
SELECT pgraft_set_debug(false);
```

### Important Log Messages

**Normal operation:**
```
pgraft: Background worker started
pgraft: Raft node initialized, node_id=1
pgraft: Elected as leader in term 5
pgraft: Heartbeat sent to node 2
```

**Warning signs:**
```
pgraft: Election timeout, starting election
pgraft: Lost leadership, stepping down
pgraft: Failed to replicate to majority
```

**Errors:**
```
pgraft: Cannot add node - this node is not the leader
pgraft: Failed to persist HardState
pgraft: Network connection failed to node 2
```

## Alerting

### Critical Alerts

Set up alerts for these conditions:

**No Leader:**
```sql
SELECT pgraft_get_leader() = 0;
-- Alert if true for > 10 seconds
```

**Worker Not Running:**
```sql
SELECT pgraft_get_worker_state() != 'RUNNING';
-- Alert immediately
```

**Frequent Leader Changes:**
```sql
-- Monitor pgraft_get_term()
-- Alert if changes > 3 times per minute
```

### Warning Alerts

**Log Lag:**
```sql
SELECT last_index - commit_index > 100 
FROM pgraft_log_get_stats();
-- Alert if true
```

**Term Increasing:**
```sql
-- Monitor pgraft_get_term()
-- Warn if increases (indicates elections happening)
```

## Health Check Endpoint

Create a health check function for load balancers:

```sql
CREATE OR REPLACE FUNCTION pgraft_health_check()
RETURNS json AS $$
DECLARE
    result json;
BEGIN
    SELECT json_build_object(
        'healthy', pgraft_get_worker_state() = 'RUNNING',
        'is_leader', pgraft_is_leader(),
        'leader_id', pgraft_get_leader(),
        'term', pgraft_get_term()
    ) INTO result;
    
    RETURN result;
END;
$$ LANGUAGE plpgsql;

-- Usage
SELECT pgraft_health_check();
```

**Output:**
```json
{
  "healthy": true,
  "is_leader": false,
  "leader_id": 1,
  "term": 42
}
```

## Monitoring Checklist

Daily:
- [ ] Check worker state on all nodes
- [ ] Verify leader is elected
- [ ] Check for errors in logs

Weekly:
- [ ] Review term changes (should be stable)
- [ ] Check log statistics
- [ ] Verify all nodes are in cluster

Monthly:
- [ ] Review performance metrics
- [ ] Check disk usage for log storage
- [ ] Test failover scenarios

## Example Monitoring Dashboard

Here's a sample SQL script for a monitoring dashboard:

```sql
-- pgraft_dashboard.sql

\echo 'Cluster Overview'
\echo '================'
SELECT 
    'Leader ID' as metric, 
    pgraft_get_leader()::text as value
UNION ALL
SELECT 
    'Current Term', 
    pgraft_get_term()::text
UNION ALL
SELECT 
    'Worker State', 
    pgraft_get_worker_state();

\echo ''
\echo 'Cluster Nodes'
\echo '============='
SELECT * FROM pgraft_get_nodes();

\echo ''
\echo 'Log Statistics'
\echo '=============='
SELECT * FROM pgraft_log_get_stats();

\echo ''
\echo 'Node Status'
\echo '==========='
SELECT 
    CASE WHEN pgraft_is_leader() THEN 'LEADER' ELSE 'FOLLOWER' END as role,
    pgraft_get_worker_state() as worker;
```

Run it:
```bash
psql -f pgraft_dashboard.sql
```

