# pgraft Tutorial: Complete Setup and Usage Guide

This tutorial will walk you through setting up a complete pgraft cluster from scratch, including installation, configuration, and advanced usage scenarios.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Installation](#installation)
3. [Basic Cluster Setup](#basic-cluster-setup)
4. [Advanced Configuration](#advanced-configuration)
5. [Cluster Operations](#cluster-operations)
6. [Monitoring and Maintenance](#monitoring-and-maintenance)
7. [Troubleshooting](#troubleshooting)
8. [Best Practices](#best-practices)

## Prerequisites

### System Requirements

- **Operating System**: Linux, macOS, or Windows
- **PostgreSQL**: Version 17 or higher
- **Go**: Version 1.21 or higher
- **Memory**: Minimum 2GB RAM per node
- **Disk**: Minimum 10GB free space per node
- **Network**: Reliable network connectivity between nodes

### Software Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install postgresql-17 postgresql-server-dev-17 golang-go build-essential

# CentOS/RHEL
sudo yum install postgresql17 postgresql17-devel golang gcc make

# macOS
brew install postgresql@17 go
```

## Installation

### Step 1: Download and Build

```bash
# Clone the repository
git clone https://github.com/pgelephant/pgraft.git
cd pgraft

# Build the extension
make clean
make
sudo make install

# Verify installation
make installcheck
```

### Step 2: Verify Installation

```bash
# Check if extension files are installed
ls -la /usr/local/pgsql.17/lib/pgraft*
ls -la /usr/local/pgsql.17/share/extension/pgraft*

# Expected output:
# pgraft.dylib (or .so on Linux)
# pgraft.control
# pgraft--1.0.sql
```

## Basic Cluster Setup

### Step 1: Prepare PostgreSQL Instances

Create three PostgreSQL instances for our cluster:

```bash
# Create data directories
mkdir -p /data/node1 /data/node2 /data/node3

# Initialize databases
/usr/local/pgsql.17/bin/initdb -D /data/node1
/usr/local/pgsql.17/bin/initdb -D /data/node2
/usr/local/pgsql.17/bin/initdb -D /data/node3
```

### Step 2: Configure PostgreSQL

**Node 1 Configuration (`/data/node1/postgresql.conf`):**

```ini
# Network settings
listen_addresses = '*'
port = 5433

# Load pgraft extension
shared_preload_libraries = 'pgraft'

# pgraft configuration
pgraft.node_id = 1
pgraft.address = '127.0.0.1'
pgraft.port = 5433
pgraft.cluster_name = 'tutorial_cluster'

# Logging for debugging
log_min_messages = info
log_line_prefix = '%t [%p]: [%l-1] user=%u,db=%d,app=%a,client=%h '
log_checkpoints = on
log_connections = on
log_disconnections = on
log_lock_waits = on
```

**Node 2 Configuration (`/data/node2/postgresql.conf`):**

```ini
# Network settings
listen_addresses = '*'
port = 5434

# Load pgraft extension
shared_preload_libraries = 'pgraft'

# pgraft configuration
pgraft.node_id = 2
pgraft.address = '127.0.0.1'
pgraft.port = 5434
pgraft.cluster_name = 'tutorial_cluster'

# Logging for debugging
log_min_messages = info
log_line_prefix = '%t [%p]: [%l-1] user=%u,db=%d,app=%a,client=%h '
log_checkpoints = on
log_connections = on
log_disconnections = on
log_lock_waits = on
```

**Node 3 Configuration (`/data/node3/postgresql.conf`):**

```ini
# Network settings
listen_addresses = '*'
port = 5435

# Load pgraft extension
shared_preload_libraries = 'pgraft'

# pgraft configuration
pgraft.node_id = 3
pgraft.address = '127.0.0.1'
pgraft.port = 5435
pgraft.cluster_name = 'tutorial_cluster'

# Logging for debugging
log_min_messages = info
log_line_prefix = '%t [%p]: [%l-1] user=%u,db=%d,app=%a,client=%h '
log_checkpoints = on
log_connections = on
log_disconnections = on
log_lock_waits = on
```

### Step 3: Start PostgreSQL Instances

```bash
# Start all three nodes
/usr/local/pgsql.17/bin/pg_ctl -D /data/node1 -l /data/node1/logfile start
/usr/local/pgsql.17/bin/pg_ctl -D /data/node2 -l /data/node2/logfile start
/usr/local/pgsql.17/bin/pg_ctl -D /data/node3 -l /data/node3/logfile start

# Verify they're running
ps aux | grep postgres
```

### Step 4: Initialize the Cluster

Connect to each node and initialize pgraft:

```bash
# Connect to Node 1
psql -h 127.0.0.1 -p 5433 -U postgres

# Create the extension
CREATE EXTENSION IF NOT EXISTS pgraft;

# Initialize the first node
SELECT pgraft_init();

# Check the status
SELECT pgraft_get_worker_state();
SELECT * FROM pgraft_get_cluster_status();

# Exit
\q
```

```bash
# Connect to Node 2
psql -h 127.0.0.1 -p 5434 -U postgres

# Create the extension
CREATE EXTENSION IF NOT EXISTS pgraft;

# Initialize the node
SELECT pgraft_init();

# Exit
\q
```

```bash
# Connect to Node 3
psql -h 127.0.0.1 -p 5435 -U postgres

# Create the extension
CREATE EXTENSION IF NOT EXISTS pgraft;

# Initialize the node
SELECT pgraft_init();

# Exit
\q
```

### Step 5: Form the Cluster

Connect to the first node and add the other nodes:

```bash
# Connect to Node 1 (should be the initial leader)
psql -h 127.0.0.1 -p 5433 -U postgres

# Add Node 2 to the cluster
SELECT pgraft_add_node(2, '127.0.0.1', 5434);

# Add Node 3 to the cluster
SELECT pgraft_add_node(3, '127.0.0.1', 5435);

# Verify cluster formation
SELECT * FROM pgraft_get_nodes();
SELECT * FROM pgraft_get_cluster_status();
SELECT pgraft_is_leader();

# Exit
\q
```

### Step 6: Verify Cluster Health

Check each node to ensure they're properly connected:

```bash
# Check Node 1
psql -h 127.0.0.1 -p 5433 -U postgres -c "SELECT pgraft_is_leader(), pgraft_get_term(), pgraft_get_leader();"

# Check Node 2
psql -h 127.0.0.1 -p 5434 -U postgres -c "SELECT pgraft_is_leader(), pgraft_get_term(), pgraft_get_leader();"

# Check Node 3
psql -h 127.0.0.1 -p 5435 -U postgres -c "SELECT pgraft_is_leader(), pgraft_get_term(), pgraft_get_leader();"
```

Expected output should show:
- One node as leader (`pgraft_is_leader()` returns `true`)
- Same term number on all nodes
- Same leader ID on all nodes

## Advanced Configuration

### Performance Tuning

**Optimize for High Throughput:**

```ini
# In postgresql.conf
pgraft.heartbeat_interval = 500  # Faster heartbeats
pgraft.election_timeout = 3000   # Faster elections
pgraft.worker_interval = 100     # More frequent processing

# PostgreSQL settings
shared_buffers = 256MB
effective_cache_size = 1GB
work_mem = 4MB
maintenance_work_mem = 64MB
```

**Optimize for Low Latency:**

```ini
# In postgresql.conf
pgraft.heartbeat_interval = 1000  # Standard heartbeats
pgraft.election_timeout = 5000    # Standard elections
pgraft.worker_interval = 50       # Very frequent processing

# PostgreSQL settings
synchronous_commit = on
fsync = on
wal_sync_method = fdatasync
```

### Security Configuration

**Enable SSL/TLS:**

```ini
# In postgresql.conf
ssl = on
ssl_cert_file = 'server.crt'
ssl_key_file = 'server.key'
ssl_ca_file = 'ca.crt'

# pgraft will automatically use SSL for inter-node communication
```

**Network Security:**

```bash
# Configure firewall (example for iptables)
sudo iptables -A INPUT -p tcp --dport 5433 -s 127.0.0.1 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 5434 -s 127.0.0.1 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 5435 -s 127.0.0.1 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 5433 -j DROP
sudo iptables -A INPUT -p tcp --dport 5434 -j DROP
sudo iptables -A INPUT -p tcp --dport 5435 -j DROP
```

## Cluster Operations

### Adding a New Node

1. **Prepare the new node:**
```bash
# Create data directory
mkdir -p /data/node4

# Initialize database
/usr/local/pgsql.17/bin/initdb -D /data/node4

# Configure postgresql.conf
# (similar to other nodes but with node_id=4, port=5436)
```

2. **Start the new node:**
```bash
/usr/local/pgsql.17/bin/pg_ctl -D /data/node4 -l /data/node4/logfile start
```

3. **Add to cluster:**
```sql
-- Connect to any existing node
psql -h 127.0.0.1 -p 5433 -U postgres

-- Add the new node
SELECT pgraft_add_node(4, '127.0.0.1', 5436);

-- Verify
SELECT * FROM pgraft_get_nodes();
```

### Removing a Node

```sql
-- Connect to any node
psql -h 127.0.0.1 -p 5433 -U postgres

-- Remove the node
SELECT pgraft_remove_node(4);

-- Verify
SELECT * FROM pgraft_get_nodes();
```

### Leader Election Testing

Test automatic leader election by stopping the current leader:

```bash
# Find the current leader
psql -h 127.0.0.1 -p 5433 -U postgres -c "SELECT pgraft_is_leader(), pgraft_get_leader();"

# Stop the leader (replace with actual port)
/usr/local/pgsql.17/bin/pg_ctl -D /data/node1 stop

# Wait a few seconds, then check remaining nodes
psql -h 127.0.0.1 -p 5434 -U postgres -c "SELECT pgraft_is_leader(), pgraft_get_term();"
psql -h 127.0.0.1 -p 5435 -U postgres -c "SELECT pgraft_is_leader(), pgraft_get_term();"

# One should now be the leader with a higher term

# Restart the stopped node
/usr/local/pgsql.17/bin/pg_ctl -D /data/node1 -l /data/node1/logfile start

# It will automatically rejoin as a follower
```

### Log Replication Testing

Test log replication by performing operations:

```sql
-- Connect to the leader
psql -h 127.0.0.1 -p 5433 -U postgres

-- Create a test table
CREATE TABLE test_data (id SERIAL PRIMARY KEY, data TEXT, created_at TIMESTAMP DEFAULT NOW());

-- Insert some data
INSERT INTO test_data (data) VALUES ('Test entry 1'), ('Test entry 2'), ('Test entry 3');

-- Check that data is replicated to followers
-- (Connect to followers and verify the table exists)
```

## Monitoring and Maintenance

### Health Monitoring Script

Create a monitoring script:

```bash
#!/bin/bash
# monitor_cluster.sh

NODES=(5433 5434 5435)
CLUSTER_NAME="tutorial_cluster"

echo "=== pgraft Cluster Health Check ==="
echo "Cluster: $CLUSTER_NAME"
echo "Timestamp: $(date)"
echo ""

for port in "${NODES[@]}"; do
    echo "--- Node on port $port ---"
    
    # Check if PostgreSQL is running
    if pg_isready -h 127.0.0.1 -p $port > /dev/null 2>&1; then
        echo "PostgreSQL: ✓ Running"
        
        # Check pgraft status
        STATUS=$(psql -h 127.0.0.1 -p $port -U postgres -t -c "
            SELECT 
                CASE WHEN pgraft_is_leader() THEN 'LEADER' ELSE 'FOLLOWER' END,
                pgraft_get_term(),
                pgraft_get_leader()
        " 2>/dev/null)
        
        if [ $? -eq 0 ]; then
            echo "pgraft: ✓ $STATUS"
        else
            echo "pgraft: ✗ Not responding"
        fi
    else
        echo "PostgreSQL: ✗ Not running"
    fi
    echo ""
done

# Check cluster consistency
echo "--- Cluster Consistency ---"
LEADERS=$(for port in "${NODES[@]}"; do
    if pg_isready -h 127.0.0.1 -p $port > /dev/null 2>&1; then
        psql -h 127.0.0.1 -p $port -U postgres -t -c "SELECT pgraft_is_leader()::text" 2>/dev/null
    fi
done | grep -c true)

if [ "$LEADERS" -eq 1 ]; then
    echo "Leadership: ✓ Single leader detected"
elif [ "$LEADERS" -gt 1 ]; then
    echo "Leadership: ✗ Multiple leaders detected (split-brain)"
else
    echo "Leadership: ✗ No leader detected"
fi
```

Make it executable and run:

```bash
chmod +x monitor_cluster.sh
./monitor_cluster.sh
```

### Automated Backup with pgraft

Create a backup script that coordinates with the cluster:

```bash
#!/bin/bash
# backup_cluster.sh

BACKUP_DIR="/backups/pgraft"
DATE=$(date +%Y%m%d_%H%M%S)
CLUSTER_NAME="tutorial_cluster"

# Create backup directory
mkdir -p $BACKUP_DIR

# Find the current leader
LEADER_PORT=$(for port in 5433 5434 5435; do
    if psql -h 127.0.0.1 -p $port -U postgres -t -c "SELECT pgraft_is_leader()" 2>/dev/null | grep -q true; then
        echo $port
        break
    fi
done)

if [ -z "$LEADER_PORT" ]; then
    echo "Error: No leader found"
    exit 1
fi

echo "Backing up from leader on port $LEADER_PORT"

# Perform backup
pg_dump -h 127.0.0.1 -p $LEADER_PORT -U postgres \
    --format=custom \
    --compress=9 \
    --file="$BACKUP_DIR/backup_${CLUSTER_NAME}_${DATE}.dump" \
    --verbose

# Verify backup
if [ $? -eq 0 ]; then
    echo "Backup completed successfully: backup_${CLUSTER_NAME}_${DATE}.dump"
    
    # Clean up old backups (keep last 7 days)
    find $BACKUP_DIR -name "backup_${CLUSTER_NAME}_*.dump" -mtime +7 -delete
else
    echo "Backup failed"
    exit 1
fi
```

### Performance Monitoring

Create a performance monitoring script:

```bash
#!/bin/bash
# perf_monitor.sh

echo "=== pgraft Performance Metrics ==="
echo "Timestamp: $(date)"
echo ""

for port in 5433 5434 5435; do
    if pg_isready -h 127.0.0.1 -p $port > /dev/null 2>&1; then
        echo "--- Node on port $port ---"
        
        # Get cluster status
        psql -h 127.0.0.1 -p $port -U postgres -c "
            SELECT 
                node_id,
                current_term,
                leader_id,
                state,
                num_nodes,
                messages_processed,
                heartbeats_sent,
                elections_triggered
            FROM pgraft_get_cluster_status();
        " 2>/dev/null
        
        # Get log statistics
        psql -h 127.0.0.1 -p $port -U postgres -c "
            SELECT 
                log_size,
                last_index,
                commit_index,
                last_applied,
                replicated,
                committed,
                applied,
                errors
            FROM pgraft_log_get_stats();
        " 2>/dev/null
        
        echo ""
    fi
done
```

## Troubleshooting

### Common Issues and Solutions

#### 1. Extension Not Loading

**Symptoms:**
```
ERROR: extension "pgraft" is not available
```

**Solutions:**
```bash
# Check if extension is installed
ls -la /usr/local/pgsql.17/lib/pgraft*

# Rebuild and reinstall
cd /path/to/pgraft
make clean
make
sudo make install

# Check shared_preload_libraries
psql -c "SHOW shared_preload_libraries;"
```

#### 2. Worker Not Starting

**Symptoms:**
```sql
SELECT pgraft_get_worker_state();
-- Returns: "STOPPED"
```

**Solutions:**
```bash
# Check PostgreSQL logs
tail -f /data/node1/logfile

# Restart PostgreSQL
/usr/local/pgsql.17/bin/pg_ctl -D /data/node1 restart

# Check if pgraft is in shared_preload_libraries
grep shared_preload_libraries /data/node1/postgresql.conf
```

#### 3. Network Connectivity Issues

**Symptoms:**
```
pgraft: WARNING - Failed to connect to peer 2 at 127.0.0.1:5434
```

**Solutions:**
```bash
# Test network connectivity
telnet 127.0.0.1 5434

# Check firewall
sudo iptables -L

# Verify port configuration
netstat -tlnp | grep 543
```

#### 4. Split-Brain Scenario

**Symptoms:**
```sql
-- Multiple nodes think they're leader
SELECT pgraft_is_leader() FROM (SELECT 5433 as port UNION SELECT 5434 UNION SELECT 5435) ports;
-- Returns multiple true values
```

**Solutions:**
```bash
# Stop all nodes
/usr/local/pgsql.17/bin/pg_ctl -D /data/node1 stop
/usr/local/pgsql.17/bin/pg_ctl -D /data/node2 stop
/usr/local/pgsql.17/bin/pg_ctl -D /data/node3 stop

# Wait 30 seconds

# Start nodes one by one with delays
/usr/local/pgsql.17/bin/pg_ctl -D /data/node1 -l /data/node1/logfile start
sleep 10
/usr/local/pgsql.17/bin/pg_ctl -D /data/node2 -l /data/node2/logfile start
sleep 10
/usr/local/pgsql.17/bin/pg_ctl -D /data/node3 -l /data/node3/logfile start
```

### Debug Mode

Enable debug mode for troubleshooting:

```sql
-- Enable debug logging
SELECT pgraft_set_debug(true);

-- Perform operations and check logs
SELECT pgraft_get_worker_state();
SELECT * FROM pgraft_get_queue_status();

-- Disable debug logging
SELECT pgraft_set_debug(false);
```

### Log Analysis

Key log patterns to look for:

```bash
# Successful operations
grep "pgraft: INFO" /data/node*/logfile

# Warnings
grep "pgraft: WARNING" /data/node*/logfile

# Errors
grep "pgraft: ERROR" /data/node*/logfile

# Leader elections
grep "election\|leader" /data/node*/logfile
```

## Best Practices

### 1. Cluster Design

- **Odd Number of Nodes**: Use 3, 5, or 7 nodes for optimal fault tolerance
- **Geographic Distribution**: Place nodes in different availability zones
- **Network Latency**: Keep inter-node latency under 100ms
- **Resource Allocation**: Ensure consistent resources across nodes

### 2. Configuration Management

- **Consistent Configuration**: Use identical settings across all nodes
- **Version Control**: Track configuration changes
- **Documentation**: Document all custom settings
- **Testing**: Test configuration changes in staging first

### 3. Monitoring and Alerting

- **Health Checks**: Implement automated health monitoring
- **Performance Metrics**: Track key performance indicators
- **Alert Thresholds**: Set appropriate alert levels
- **Response Procedures**: Define incident response procedures

### 4. Backup and Recovery

- **Regular Backups**: Schedule automated backups
- **Backup Testing**: Regularly test backup restoration
- **Point-in-Time Recovery**: Implement PITR capabilities
- **Disaster Recovery**: Plan for complete cluster failure

### 5. Security

- **Network Security**: Use firewalls and VPNs
- **Authentication**: Implement strong authentication
- **Encryption**: Encrypt data in transit and at rest
- **Access Control**: Implement principle of least privilege

### 6. Performance Optimization

- **Hardware Selection**: Choose appropriate hardware
- **Configuration Tuning**: Optimize for your workload
- **Monitoring**: Continuously monitor performance
- **Capacity Planning**: Plan for growth

This tutorial provides a comprehensive guide to setting up and managing a pgraft cluster. For additional information, refer to the main documentation and architecture guides.
