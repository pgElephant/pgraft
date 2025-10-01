# Best Practices

Follow these best practices for running pgraft in production.

## Cluster Size

### Use Odd Number of Nodes

Always use an odd number of nodes (3, 5, 7) for better fault tolerance:

| Nodes | Fault Tolerance | Recommended For |
|-------|-----------------|-----------------|
| **3** | 1 node failure | Development, small production |
| **5** | 2 node failures | **Production (recommended)** |
| **7** | 3 node failures | High-availability critical systems |

!!! warning "Don't Use Even Numbers"
    4 nodes tolerates only 1 failure (same as 3)  
    6 nodes tolerates only 2 failures (same as 5)  
    Even numbers waste resources!

### Don't Go Too Large

**Avoid >7 nodes:**
- More nodes = more replication overhead
- Diminishing returns on availability
- Slower consensus

If you need more than 7 nodes, consider:
- Multiple independent clusters
- Read replicas (outside Raft cluster)
- Sharding

## Geographic Distribution

### Multi-Zone Deployment

For disaster recovery, distribute nodes across zones:

**5-node example:**
```
Zone A (2 nodes): Primary data center
Zone B (2 nodes): Secondary data center  
Zone C (1 node): Tiebreaker/DR site
```

**Benefits:**
- Survives zone failure
- Tiebreaker prevents split votes
- Geographic disaster recovery

### Network Considerations

- **Low latency required**: <50ms between nodes
- **Stable network**: Packet loss <0.1%
- **Sufficient bandwidth**: For log replication

!!! warning "High Latency"
    If latency >50ms between zones, increase election timeout:
    ```ini
    pgraft.election_timeout = 2000  # 2 seconds for WAN
    ```

## Configuration

### Election Timeout

**Rules of thumb:**

```ini
# LAN deployment (low latency <10ms)
pgraft.election_timeout = 1000  # 1 second

# WAN deployment (medium latency 10-50ms)
pgraft.election_timeout = 2000  # 2 seconds

# High latency (>50ms)
pgraft.election_timeout = 5000  # 5 seconds
```

**Key relationship:**
```
election_timeout >= 10 × heartbeat_interval
```

### Heartbeat Interval

```ini
# Default (recommended)
pgraft.heartbeat_interval = 100  # 100ms

# High-throughput systems
pgraft.heartbeat_interval = 50   # 50ms (more network traffic)

# Low-priority systems
pgraft.heartbeat_interval = 200  # 200ms (less overhead)
```

### Snapshot Configuration

```ini
# Frequent snapshots (faster recovery, more I/O)
pgraft.snapshot_interval = 5000
pgraft.max_log_entries = 500

# Infrequent snapshots (less I/O, slower recovery)
pgraft.snapshot_interval = 100000
pgraft.max_log_entries = 10000
```

**Trade-offs:**
- **Frequent snapshots**: Faster crash recovery, more I/O
- **Infrequent snapshots**: Better performance, slower recovery

## Operations

### Adding Nodes

**Always add nodes on the leader:**

```sql
-- 1. Check if leader
SELECT pgraft_is_leader();

-- 2. If not leader, find and connect to leader
SELECT pgraft_get_leader();

-- 3. Add node
SELECT pgraft_add_node(4, '192.168.1.14', 7004);
```

**Best practice:** Add one node at a time, wait for it to catch up before adding next.

### Removing Nodes

**Graceful removal:**

```sql
-- 1. Verify cluster health
SELECT * FROM pgraft_get_cluster_status();

-- 2. Remove node (on leader)
SELECT pgraft_remove_node(4);

-- 3. Shutdown removed node's PostgreSQL
-- On node 4:
pg_ctl stop -D /data/node4
```

**Never** remove nodes during:
- Active elections
- Network partitions
- While already removing another node

### Upgrading

**Rolling upgrade procedure:**

1. **Upgrade followers first:**
```bash
# On follower node:
pg_ctl stop -D /data/node2
# Install new pgraft version
make clean && make && make install
pg_ctl start -D /data/node2
```

2. **Then upgrade leader:**
```bash
# On leader, wait for followers to be healthy
# Then upgrade leader last
pg_ctl stop -D /data/node1
# Install new version
pg_ctl start -D /data/node1
```

## Monitoring

### Critical Metrics

Monitor these continuously:

```sql
-- Leader exists (should always be true)
SELECT pgraft_get_leader() > 0;

-- Worker running (should always be RUNNING)
SELECT pgraft_get_worker_state() = 'RUNNING';

-- Term stable (should not increase frequently)
SELECT pgraft_get_term();
```

### Set Up Alerts

**Critical alerts:**
- No leader for >10 seconds
- Worker not running
- Node unreachable

**Warning alerts:**
- Term increased (election occurred)
- Log lag >1000 entries
- Replication to follower slow

See [Monitoring](monitoring.md) for details.

## Backup and Recovery

### Backup Strategy

**1. Backup PostgreSQL data:**
```bash
pg_basebackup -D /backup/node1 -Ft -z -P
```

**2. Backup pgraft state:**
```bash
# Backup pgraft data directory
tar -czf pgraft-backup.tar.gz $PGRAFT_DATA_DIR
```

**3. Backup configuration:**
```bash
cp $PGDATA/postgresql.conf /backup/
```

### Disaster Recovery

**Scenario: Total cluster loss**

1. **Restore one node from backup**
2. **Initialize new cluster:**
```sql
CREATE EXTENSION pgraft;
SELECT pgraft_init();
```
3. **Add other nodes:**
```sql
SELECT pgraft_add_node(2, '192.168.1.12', 7002);
SELECT pgraft_add_node(3, '192.168.1.13', 7003);
```

## Security

### Network Security

**Firewall rules:**
```bash
# Allow Raft communication between nodes
# Node 1 → Node 2, 3
iptables -A INPUT -p tcp --dport 7002 -s node1_ip -j ACCEPT
iptables -A INPUT -p tcp --dport 7003 -s node1_ip -j ACCEPT

# Node 2 → Node 1, 3
# ... etc
```

**Best practice:** Use VPN or private network for inter-node communication.

### Access Control

**Limit pgraft functions to superuser:**
```sql
-- pgraft functions already require superuser
-- Don't grant superuser to application users
```

## Performance

### Hardware Recommendations

**Minimum (per node):**
- CPU: 2 cores
- RAM: 4GB
- Disk: SSD with >100 IOPS
- Network: 100 Mbps

**Production (per node):**
- CPU: 4+ cores
- RAM: 16GB+
- Disk: NVMe SSD with >1000 IOPS
- Network: 1 Gbps+

### Disk I/O

**Recommendations:**
- Use SSD or NVMe for pgraft data directory
- Separate disk from PostgreSQL data if possible
- Monitor disk I/O wait time

### Network

**Recommendations:**
- Dedicated network for Raft traffic (if possible)
- Monitor network latency continuously
- Use quality of service (QoS) for Raft ports

## Testing

### Test Failover Scenarios

**Test 1: Leader failure**
```bash
# Kill leader process
pg_ctl stop -D /data/leader -m immediate

# Verify new leader elected
psql -h follower1 -c "SELECT pgraft_get_leader();"

# Restart failed node
pg_ctl start -D /data/leader
```

**Test 2: Network partition**
```bash
# Simulate partition using iptables
iptables -A INPUT -s node2_ip -j DROP
iptables -A OUTPUT -d node2_ip -j DROP

# Verify majority partition continues
# Restore network
iptables -D INPUT -s node2_ip -j DROP
iptables -D OUTPUT -d node2_ip -j DROP
```

**Test 3: Slow follower**
```bash
# Simulate slow disk
# On follower, use cgroup or tc to throttle I/O
tc qdisc add dev sda root tbf rate 1mbit burst 32kbit latency 400ms

# Verify leader continues operating
# Remove throttle
tc qdisc del dev sda root
```

## Checklist

### Before Going to Production

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

### Regular Maintenance

Daily:
- [ ] Check cluster status
- [ ] Verify leader exists
- [ ] Check worker state

Weekly:
- [ ] Review monitoring metrics
- [ ] Check log file sizes
- [ ] Verify backups

Monthly:
- [ ] Test disaster recovery
- [ ] Review and optimize configuration
- [ ] Update documentation

## Common Pitfalls

### Don't

1. **Use even number of nodes** - Waste of resources
2. **Add nodes during elections** - Wait for stable leader
3. **Ignore monitoring** - Set up alerts!
4. **Run on slow disks** - Use SSD/NVMe
5. **Deploy across high-latency links** without tuning timeouts
6. **Add multiple nodes simultaneously** - Add one at a time
7. **Forget to backup pgraft state** - Backup both PostgreSQL and pgraft data

### Do

1. **Use 3, 5, or 7 nodes** for optimal fault tolerance
2. **Monitor continuously** - Leader, worker, term, logs
3. **Test failover scenarios** regularly
4. **Use fast storage** - SSD or better
5. **Distribute geographically** for disaster recovery
6. **Tune for your network** - Adjust timeouts based on latency
7. **Document your setup** - Configuration, topology, procedures
8. **Train your team** - Everyone should know how to operate pgraft

## Production Deployment Example

Here's a complete production configuration:

```ini
# postgresql.conf - Production Node 1

# PostgreSQL basics
port = 5432
max_connections = 200
shared_buffers = 4GB

# pgraft extension
shared_preload_libraries = 'pgraft'

# Core cluster configuration
pgraft.cluster_id = 'production-cluster'
pgraft.node_id = 1
pgraft.address = '10.0.1.11'
pgraft.port = 7001
pgraft.data_dir = '/var/lib/postgresql/pgraft'

# Consensus settings (tuned for datacenter LAN)
pgraft.election_timeout = 1000
pgraft.heartbeat_interval = 100
pgraft.snapshot_interval = 10000
pgraft.max_log_entries = 1000

# Performance
pgraft.batch_size = 100
pgraft.max_batch_delay = 10
pgraft.compaction_threshold = 10000

# Monitoring
pgraft.metrics_enabled = true
pgraft.metrics_port = 9100
```

Repeat for nodes 2, 3, 4, 5 with different `node_id`, `address`, and `port` values.

