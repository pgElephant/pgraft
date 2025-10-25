# Operations Guide

Production operations, monitoring, and maintenance for pgraft clusters.

## Overview

This section covers everything you need to know to operate pgraft clusters in production, from monitoring to troubleshooting to best practices.

## Contents

### Monitoring

Learn how to monitor cluster health, performance, and replication status.

[Monitoring Guide](monitoring.md){ .md-button .md-button--primary }

### Troubleshooting

Solutions for common issues and debugging techniques.

[Troubleshooting Guide](troubleshooting.md){ .md-button }

### Best Practices

Production-tested recommendations for configuration, deployment, and maintenance.

[Best Practices](best-practices.md){ .md-button }

## Quick Reference

### Health Checks

**Quick health check:**
```sql
SELECT 
    pgraft_is_leader() as is_leader,
    pgraft_get_term() as term,
    pgraft_get_leader() as leader_id,
    pgraft_get_worker_state() as worker;
```

**Detailed status:**
```sql
SELECT * FROM pgraft_get_cluster_status();
SELECT * FROM pgraft_get_nodes();
SELECT * FROM pgraft_log_get_stats();
```

### Common Issues

| Issue | Quick Check | Solution |
|-------|-------------|----------|
| Worker not running | `SELECT pgraft_get_worker_state();` | Check `shared_preload_libraries`, restart PostgreSQL |
| No leader elected | `SELECT pgraft_get_leader();` | Wait 10 seconds, check network connectivity |
| Cannot add node | `SELECT pgraft_is_leader();` | Run on leader node only |
| Frequent elections | `SELECT pgraft_get_term();` | Increase election timeout, check network |

### Key Metrics to Monitor

**Critical (alert if failing):**
- Leader exists: `pgraft_get_leader() > 0`
- Worker running: `pgraft_get_worker_state() = 'RUNNING'`
- Node reachable: Network connectivity

**Important (monitor trends):**
- Term number (should be stable)
- Log replication lag
- Election frequency
- Disk usage for log storage

### Emergency Procedures

**Lost quorum (majority of nodes failed):**
1. Cluster becomes read-only
2. Restore failed nodes or add new ones
3. Once quorum restored, leader will be elected
4. Cluster resumes normal operation

**Complete cluster failure:**
1. Restore nodes from backup
2. Start all PostgreSQL instances
3. Wait 10 seconds for election
4. Verify leader elected: `SELECT pgraft_get_leader();`

## Production Checklist

### Before Deployment

- [ ] Cluster size is odd (3, 5, or 7 nodes)
- [ ] Nodes distributed across availability zones
- [ ] Election timeout tuned for network latency
- [ ] Monitoring and alerting configured
- [ ] Backup strategy in place
- [ ] Disaster recovery plan documented
- [ ] Failover scenarios tested
- [ ] Network security configured
- [ ] Performance baselines established
- [ ] Team trained on operations

### Daily Operations

- [ ] Check cluster status
- [ ] Verify leader exists
- [ ] Check worker state on all nodes
- [ ] Review error logs

### Weekly Maintenance

- [ ] Review monitoring metrics
- [ ] Check log file sizes and disk usage
- [ ] Verify backups are running
- [ ] Review and analyze any alerts

### Monthly Tasks

- [ ] Test disaster recovery procedures
- [ ] Review and optimize configuration
- [ ] Update documentation
- [ ] Performance review and tuning

## Monitoring Tools

### Built-in Functions

pgraft provides SQL functions for monitoring:

- `pgraft_get_cluster_status()` - Overall cluster state
- `pgraft_get_nodes()` - All cluster members
- `pgraft_log_get_stats()` - Log statistics
- `pgraft_get_worker_state()` - Background worker status
- `pgraft_is_leader()` - Leadership status

### External Monitoring

**Prometheus (if enabled):**
```ini
pgraft.metrics_enabled = true
pgraft.metrics_port = 9100
```

Metrics available at `http://node:9100/metrics`

**PostgreSQL Logs:**
```bash
tail -f $PGDATA/log/postgresql-*.log | grep pgraft:
```

**Custom Monitoring Script:**
```bash
#!/bin/bash
# Check cluster health every 60 seconds
while true; do
    psql -c "SELECT * FROM pgraft_get_cluster_status();"
    sleep 60
done
```

## Performance Tuning

### Network Latency

**Low latency (<10ms):**
```ini
pgraft.election_timeout = 1000  # 1 second
pgraft.heartbeat_interval = 100 # 100ms
```

**Medium latency (10-50ms):**
```ini
pgraft.election_timeout = 2000  # 2 seconds
pgraft.heartbeat_interval = 200 # 200ms
```

**High latency (>50ms):**
```ini
pgraft.election_timeout = 5000  # 5 seconds
pgraft.heartbeat_interval = 500 # 500ms
```

### Throughput Optimization

```ini
# Larger batches for higher throughput
pgraft.batch_size = 200
pgraft.max_batch_delay = 20

# More frequent snapshots for faster recovery
pgraft.snapshot_interval = 5000
pgraft.max_log_entries = 500
```

## Security

### Network Security

- Use private networks or VPN for inter-node communication
- Configure firewalls to allow only necessary ports
- Consider enabling TLS (when available)

### Access Control

- pgraft functions require superuser privileges
- Don't grant superuser to application users
- Use connection limits and pg_hba.conf appropriately

### Audit Logging

Enable PostgreSQL logging for pgraft operations:
```ini
log_statement = 'all'
log_line_prefix = '%t [%p]: [%l-1] user=%u,db=%d,app=%a,client=%h '
```

## Backup and Recovery

### What to Backup

1. **PostgreSQL data directory** - All database data
2. **pgraft data directory** - Raft state and logs
3. **Configuration files** - postgresql.conf

### Backup Strategy

```bash
# PostgreSQL data
pg_basebackup -D /backup/pgdata -Ft -z -P

# pgraft state
tar -czf /backup/pgraft-$(date +%Y%m%d).tar.gz $PGRAFT_DATA_DIR

# Configuration
cp $PGDATA/postgresql.conf /backup/
```

### Recovery Procedure

1. Stop PostgreSQL
2. Restore data from backup
3. Restore pgraft state
4. Restore configuration
5. Start PostgreSQL
6. Verify cluster rejoins

## Resources

- **Detailed monitoring**: [Monitoring Guide](monitoring.md)
- **Problem solving**: [Troubleshooting Guide](troubleshooting.md)
- **Production tips**: [Best Practices](best-practices.md)
- **Understanding internals**: [Architecture](../concepts/architecture.md)

