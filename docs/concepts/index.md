# Core Concepts

Understand the fundamental concepts behind pgraft.

## Overview

This section explains the core concepts and algorithms that make pgraft work. Understanding these concepts will help you operate pgraft clusters effectively and troubleshoot issues.

## Key Concepts

### Architecture

Learn how pgraft integrates with PostgreSQL and how the Raft consensus algorithm works.

[Read Architecture Guide](architecture.md){ .md-button .md-button--primary }

### Automatic Replication

Understand how configuration changes automatically replicate across the cluster.

[Learn About Replication](automatic-replication.md){ .md-button }

### Split-Brain Protection

Discover how pgraft provides 100% guaranteed split-brain protection.

[Split-Brain Protection](split-brain.md){ .md-button }

## The Raft Consensus Algorithm

pgraft uses the **Raft consensus algorithm** to ensure all nodes agree on the cluster state. Raft provides:

### Leader Election

- One node is elected as leader
- Leader must have majority votes (N/2 + 1)
- Elections occur when leader fails or times out
- Higher term always wins

### Log Replication

- Leader accepts all writes
- Leader replicates entries to followers
- Entries committed when replicated to majority
- All nodes eventually have same log

### Safety Guarantees

**Election Safety**: At most one leader per term

**Leader Append-Only**: Leaders never overwrite entries

**Log Matching**: If logs contain same entry, all previous entries match

**Leader Completeness**: Committed entries present in all future leaders

**State Machine Safety**: All nodes apply same commands in same order

## How pgraft Works

### Component Architecture

```
┌─────────────────────────────────────────┐
│         PostgreSQL Process              │
├─────────────────────────────────────────┤
│  SQL Interface (C)                      │
│  ├─ pgraft_init()                       │
│  ├─ pgraft_add_node()                   │
│  └─ pgraft_get_cluster_status()         │
├─────────────────────────────────────────┤
│  Background Worker (C)                  │
│  └─ Drives Raft ticks every 100ms       │
├─────────────────────────────────────────┤
│  Go Raft Engine (Go)                    │
│  └─ etcd-io/raft implementation         │
├─────────────────────────────────────────┤
│  Persistent Storage                     │
│  ├─ HardState (term, vote, commit)      │
│  ├─ Log Entries                         │
│  └─ Snapshots                           │
└─────────────────────────────────────────┘
```

### Message Flow

1. **SQL function called** (e.g., `pgraft_add_node()`)
2. **Command queued** in shared memory
3. **Background worker** processes queue
4. **Go layer** proposes to Raft
5. **Leader replicates** to followers
6. **Majority commits** the entry
7. **All nodes apply** the change

## Key Properties

### Consistency

All nodes see the same data in the same order. If a write is acknowledged, it's guaranteed to be present on all surviving nodes.

### Availability

The cluster remains available as long as a majority of nodes are operational. For a 5-node cluster, up to 2 nodes can fail.

### Partition Tolerance

During network partitions, only the majority partition can elect a leader and accept writes. Minority partitions become read-only.

### Durability

All committed data survives node crashes and restarts. HardState, log entries, and snapshots are persisted to disk.

## Fault Tolerance

| Cluster Size | Nodes Required for Quorum | Tolerated Failures |
|--------------|---------------------------|-------------------|
| 1 node       | 1                         | 0                 |
| 3 nodes      | 2                         | 1                 |
| 5 nodes      | 3                         | 2                 |
| 7 nodes      | 4                         | 3                 |

**Note**: Even numbers waste resources. A 4-node cluster still only tolerates 1 failure (same as 3 nodes).

## Performance Characteristics

### Latency

- **Leader election**: ~1 second (configurable)
- **Write latency**: ~1 RTT to majority + disk fsync
- **Read latency**: Local (no consensus required)

### Throughput

- **Bottleneck**: Leader's disk write speed
- **Optimization**: Batch multiple entries per disk write
- **Typical**: Thousands of writes per second

### Resource Usage

- **CPU**: <1% idle, <5% during elections
- **Memory**: ~50MB per node
- **Disk I/O**: Proportional to write rate
- **Network**: Heartbeats every 100ms + replication traffic

## Common Patterns

### Configuration Changes

All configuration changes (adding/removing nodes) go through Raft consensus:

1. Leader receives request
2. Leader proposes ConfChange
3. ConfChange replicated to majority
4. All nodes apply configuration
5. Cluster now uses new configuration

### Automatic Failover

When leader fails:

1. Followers stop receiving heartbeats
2. After election timeout, followers start election
3. Candidate with up-to-date log wins
4. New leader begins sending heartbeats
5. Cluster resumes normal operation

**Total downtime**: ~1-2 seconds (configurable)

## Learn More

- **Deep dive**: [Architecture](architecture.md)
- **How replication works**: [Automatic Replication](automatic-replication.md)
- **Safety guarantees**: [Split-Brain Protection](split-brain.md)
- **Operating clusters**: [Operations Guide](../operations/index.md)

