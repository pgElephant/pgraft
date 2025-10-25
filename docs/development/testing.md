# Testing

This guide covers testing pgraft during development.

## Test Harness

pgraft includes a comprehensive test harness in the `examples/` directory.

### Quick Start

```bash
cd examples

# Destroy any existing test cluster
./run.sh --destroy

# Initialize new 3-node cluster
./run.sh --init

# Check cluster status
./run.sh --status
```

### Test Harness Commands

| Command | Description |
|---------|-------------|
| `--destroy` | Destroy test cluster and clean up |
| `--init` | Initialize new 3-node cluster |
| `--status` | Check cluster status |

## Test Cluster Configuration

The test harness creates a 3-node cluster:

- **primary1**: Node 1, PostgreSQL port 5432, Raft port 7001
- **replica1**: Node 2, PostgreSQL port 5433, Raft port 7002
- **replica2**: Node 3, PostgreSQL port 5434, Raft port 7003

## Manual Testing

### Basic Functionality

```sql
-- Test extension creation
CREATE EXTENSION pgraft;

-- Test initialization
SELECT pgraft_init();
-- Expected: t

-- Test worker status
SELECT pgraft_get_worker_state();
-- Expected: RUNNING

-- Test leader election (wait 10 seconds first)
SELECT pgraft_get_leader();
-- Expected: 1 (or another node ID)

-- Test leadership check
SELECT pgraft_is_leader();
-- Expected: t or f
```

### Cluster Operations

```sql
-- Add node (must be on leader)
SELECT pgraft_add_node(2, '127.0.0.1', 7002);
-- Expected: t

-- List nodes
SELECT * FROM pgraft_get_nodes();
-- Expected: Table with all nodes

-- Cluster status
SELECT * FROM pgraft_get_cluster_status();
-- Expected: Status information

-- Get current term
SELECT pgraft_get_term();
-- Expected: Integer term number
```

### Log Replication

```sql
-- Replicate entry
SELECT pgraft_replicate_entry('{"test": "data"}');
-- Expected: t (if quorum reached)

-- Get log stats
SELECT * FROM pgraft_log_get_stats();
-- Expected: Table with statistics
```

## Failover Testing

### Test Leader Failure

```bash
# 1. Identify leader
psql -p 5432 -c "SELECT pgraft_get_leader();"

# 2. Stop leader (e.g., if node 1 is leader)
pg_ctl stop -D examples/data/primary1 -m immediate

# 3. Verify new leader elected (check on remaining nodes)
sleep 2
psql -p 5433 -c "SELECT pgraft_get_leader();"
psql -p 5434 -c "SELECT pgraft_get_leader();"

# 4. Restart failed node
pg_ctl start -D examples/data/primary1

# 5. Verify it rejoins as follower
sleep 2
psql -p 5432 -c "SELECT pgraft_is_leader();"
```

### Test Network Partition

Using `iptables` (Linux) or `pfctl` (macOS):

```bash
# Simulate partition by blocking Raft port
# On node 3, block communication with node 1 and 2
sudo iptables -A INPUT -p tcp --sport 7001 -j DROP
sudo iptables -A INPUT -p tcp --sport 7002 -j DROP
sudo iptables -A OUTPUT -p tcp --dport 7001 -j DROP
sudo iptables -A OUTPUT -p tcp --dport 7002 -j DROP

# Verify node 3 cannot elect itself leader
psql -p 5434 -c "SELECT pgraft_is_leader();"
# Should return false

# Verify nodes 1 and 2 continue with leader
psql -p 5432 -c "SELECT pgraft_get_leader();"
psql -p 5433 -c "SELECT pgraft_get_leader();"

# Restore network
sudo iptables -D INPUT -p tcp --sport 7001 -j DROP
sudo iptables -D INPUT -p tcp --sport 7002 -j DROP
sudo iptables -D OUTPUT -p tcp --dport 7001 -j DROP
sudo iptables -D OUTPUT -p tcp --dport 7002 -j DROP

# Verify node 3 rejoins
sleep 2
psql -p 5434 -c "SELECT * FROM pgraft_get_cluster_status();"
```

## Performance Testing

### Throughput Test

```sql
-- Test log entry replication rate
DO $$
DECLARE
    start_time timestamp;
    end_time timestamp;
    i integer;
BEGIN
    start_time := clock_timestamp();
    
    FOR i IN 1..1000 LOOP
        PERFORM pgraft_replicate_entry('{"test": "data"}');
    END LOOP;
    
    end_time := clock_timestamp();
    
    RAISE NOTICE 'Time: %', end_time - start_time;
    RAISE NOTICE 'Entries/sec: %', 1000.0 / extract(epoch from (end_time - start_time));
END $$;
```

### Latency Test

```sql
-- Measure single entry replication latency
\timing on
SELECT pgraft_replicate_entry('{"test": "data"}');
\timing off
```

## Stress Testing

### Continuous Operations

```bash
# Terminal 1: Continuous writes on leader
while true; do
    psql -p 5432 -c "SELECT pgraft_replicate_entry(now()::text);" 2>&1 | grep -v "replicate"
    sleep 0.1
done

# Terminal 2: Monitor cluster status
watch -n 1 "psql -p 5432 -c 'SELECT * FROM pgraft_get_cluster_status();'"

# Terminal 3: Simulate failures
# Stop/start nodes randomly
```

### Sustained Load

```bash
# Generate sustained load
for i in {1..10000}; do
    psql -p 5432 -c "SELECT pgraft_replicate_entry('entry_$i');" &
    if [ $((i % 100)) -eq 0 ]; then
        wait  # Wait every 100 to avoid overwhelming
    fi
done
wait
```

## Integration Testing

### Multi-Node Test Script

Create `test_cluster.sh`:

```bash
#!/bin/bash

echo "Testing cluster operations..."

# Test on all nodes
for PORT in 5432 5433 5434; do
    echo "Node on port $PORT:"
    psql -p $PORT -c "SELECT pgraft_is_leader(), pgraft_get_term();" -t
done

# Get leader
LEADER_PORT=$(psql -p 5432 -c "SELECT pgraft_get_leader();" -t | tr -d ' ')
LEADER_PORT=$((5431 + LEADER_PORT))

echo "Leader is on port $LEADER_PORT"

# Add and remove node on leader
echo "Testing add/remove node on leader..."
psql -p $LEADER_PORT -c "SELECT pgraft_add_node(4, '127.0.0.1', 7004);"
sleep 1
psql -p $LEADER_PORT -c "SELECT * FROM pgraft_get_nodes();"
psql -p $LEADER_PORT -c "SELECT pgraft_remove_node(4);"
sleep 1
psql -p $LEADER_PORT -c "SELECT * FROM pgraft_get_nodes();"

echo "Test completed!"
```

## Regression Testing

### SQL Test Suite

Create `regression_tests.sql`:

```sql
-- Test 1: Extension creation
CREATE EXTENSION IF NOT EXISTS pgraft;

-- Test 2: Initialization
SELECT pgraft_init();

-- Wait for leader election
SELECT pg_sleep(2);

-- Test 3: Worker running
SELECT pgraft_get_worker_state() = 'RUNNING' AS worker_ok;

-- Test 4: Leader elected
SELECT pgraft_get_leader() > 0 AS leader_elected;

-- Test 5: Get term
SELECT pgraft_get_term() > 0 AS term_ok;

-- Test 6: Cluster status
SELECT node_id, term, state FROM pgraft_get_cluster_status();

-- Test 7: Log operations (on leader only)
DO $$
BEGIN
    IF pgraft_is_leader() THEN
        PERFORM pgraft_replicate_entry('test_entry');
    END IF;
END $$;

-- Test 8: Log statistics
SELECT * FROM pgraft_log_get_stats();

-- Test 9: Version
SELECT pgraft_get_version();

-- Test 10: Debug mode
SELECT pgraft_set_debug(true);
SELECT pgraft_set_debug(false);
```

Run tests:
```bash
psql -f regression_tests.sql
```

## Automated Testing

### GitHub Actions

The repository includes GitHub Actions workflow for CI/CD (see `.github/workflows/test.yml`).

### Local Automation

```bash
#!/bin/bash
# automated_test.sh

set -e  # Exit on error

echo "Starting automated tests..."

# Clean slate
cd examples
./run.sh --destroy

# Initialize
./run.sh --init
sleep 5  # Wait for cluster to stabilize

# Run regression tests on all nodes
for PORT in 5432 5433 5434; do
    echo "Testing node on port $PORT..."
    psql -p $PORT -f ../tests/regression_tests.sql
done

# Failover test
echo "Testing failover..."
LEADER=$(psql -p 5432 -t -c "SELECT pgraft_get_leader();" | tr -d ' ')
LEADER_PORT=$((5431 + LEADER))

pg_ctl stop -D data/node$LEADER -m immediate
sleep 3
NEW_LEADER=$(psql -p $((LEADER_PORT + 1)) -t -c "SELECT pgraft_get_leader();" | tr -d ' ')

if [ "$NEW_LEADER" != "$LEADER" ]; then
    echo "✓ Failover successful"
else
    echo "✗ Failover failed"
    exit 1
fi

# Cleanup
./run.sh --destroy

echo "All tests passed!"
```

## Test Coverage

Key areas to test:

- Extension creation and initialization
- Background worker startup
- Leader election
- Node addition (on leader)
- Node addition (on follower - should fail)
- Node removal
- Log replication
- Cluster status queries
- Leader failure and recovery
- Network partition handling
- Concurrent operations
- Configuration changes
- Snapshot creation and recovery

## Debugging Test Failures

### Enable Debug Logging

```sql
SELECT pgraft_set_debug(true);
```

### Check Logs

```bash
# View logs for all nodes
tail -f examples/logs/primary1/postgresql.log &
tail -f examples/logs/replica1/postgresql.log &
tail -f examples/logs/replica2/postgresql.log &
```

### Common Issues

**Leader not elected:**
- Check network connectivity
- Verify clock synchronization
- Increase election timeout

**Node won't join:**
- Verify configuration matches
- Check firewall rules
- Ensure node is initialized

**Replication lag:**
- Check disk I/O
- Monitor network latency
- Review snapshot settings

## Reporting Issues

When reporting test failures, include:

1. **Test scenario**: What were you testing?
2. **Expected result**: What should happen?
3. **Actual result**: What actually happened?
4. **Logs**: Relevant log excerpts (with debug enabled)
5. **Configuration**: postgresql.conf settings
6. **Environment**: OS, PostgreSQL version, pgraft version

