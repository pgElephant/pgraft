# Split-Brain Protection

pgraft provides **100% split-brain protection** through the Raft consensus algorithm. This page explains how it works and why you can trust it.

## What is Split-Brain?

Split-brain is a dangerous condition in distributed systems where:

- Network partition divides the cluster
- Multiple nodes believe they are the leader
- Different leaders accept conflicting writes
- **Data corruption and inconsistency result**

!!! danger "Without Protection"
    In traditional PostgreSQL replication, network partitions can lead to split-brain scenarios where multiple nodes accept writes, causing permanent data inconsistencies.

## How pgraft Prevents Split-Brain

pgraft uses the **Raft consensus algorithm** which provides mathematical guarantees against split-brain through four key mechanisms:

### 1. Quorum Requirement

Leader election requires a **majority of votes** (N/2 + 1):

- 3-node cluster: Needs 2 votes
- 5-node cluster: Needs 3 votes
- 7-node cluster: Needs 4 votes

**Why this prevents split-brain:**

```
Network Partition Example (3-node cluster):

Partition 1: Node 1, Node 2
Partition 2: Node 3

Partition 1 (2 nodes):
   - Has majority (2 out of 3)
   - Can elect leader
   - Can accept writes

Partition 2 (1 node):
   - No majority (1 out of 3)
   - Cannot elect leader
   - Cannot accept writes (read-only)
```

**Mathematical guarantee:** Only one partition can have a majority, therefore only one leader can be elected.

### 2. Term Monotonicity

Each leader election increments the **term number**:

- Terms are strictly increasing
- Higher term always wins
- Old leaders automatically step down when they see a higher term

**Example:**
```
Time T0:
  Node 1 is leader in term 5

Network partition occurs

Time T1:
  Partition A: Node 1 (term 5)
  Partition B: Node 2, 3 elect new leader (term 6)

Network heals

Time T2:
  Node 1 sees messages from term 6
  Node 1 automatically steps down
  Only one leader remains (term 6)
```

### 3. Log Completeness

Only nodes with **up-to-date logs** can be elected:

- Candidates with incomplete logs lose elections
- New leader always has all committed entries
- Prevents data loss during leadership transitions

**Election Rules:**
```sql
Candidate A: Last log index = 100, Last log term = 5
Candidate B: Last log index = 95,  Last log term = 5

Result: A wins (more up-to-date log)

Candidate C: Last log index = 100, Last log term = 4
Candidate D: Last log index = 95,  Last log term = 5

Result: D wins (higher term in last entry)
```

### 4. Single Leader Per Term

**Raft's fundamental guarantee:**

> At most one leader can be elected in a given term.

**Why?**

- Leader needs majority of votes
- Each node votes for at most one candidate per term
- Two candidates cannot both get majority votes (mathematical impossibility)

```
Example (5-node cluster):

Candidate A gets votes from: Node 1, 2, 3 (majority ✅)
Candidate B can only get: Node 4, 5 (not majority ❌)

Impossible for both A and B to get 3+ votes!
```

## Network Partition Scenarios

### Scenario 1: Minority Partition

**Setup:** 3-node cluster (Node 1, 2, 3), Node 1 is leader

**Event:** Node 3 is isolated

```
Before:
  [Node 1 (Leader)] ←→ [Node 2] ←→ [Node 3]

After Partition:
  [Node 1 (Leader)] ←→ [Node 2]   |   [Node 3 (isolated)]
```

**Result:**

- Node 1 remains leader (still has majority with Node 2)
- Cluster continues operating normally
- Node 3 becomes follower, cannot accept writes
- **No split-brain** - only one leader

### Scenario 2: Equal Partition (3 nodes)

**Setup:** 3-node cluster, network splits 1-2 vs 3

**Event:** Network partition

```
Before:
  [Node 1 (Leader)] ←→ [Node 2] ←→ [Node 3]

After Partition:
  [Node 1] ←→ [Node 2]   |   [Node 3]
```

**Result:**

- Partition with nodes 1, 2 keeps leader (has majority)
- Node 3 cannot elect new leader (no majority)
- **No split-brain** - only one leader

### Scenario 3: Leader in Minority

**Setup:** 5-node cluster, Node 1 is leader

**Event:** Node 1 and 2 isolated from 3, 4, 5

```
Before:
  [Node 1 (Leader)] ←→ [Node 2] ←→ [Node 3] ←→ [Node 4] ←→ [Node 5]

After Partition:
  [Node 1] ←→ [Node 2]   |   [Node 3] ←→ [Node 4] ←→ [Node 5]
```

**Result:**

- Node 1 loses leadership (no majority - only 2 of 5)
- Nodes 3, 4, 5 elect new leader (have majority - 3 of 5)
- Node 1 steps down after election timeout
- **No split-brain** - old leader steps down, new leader elected

## Verification

You can verify split-brain protection yourself:

### Test 1: Isolate Minority

```bash
# 3-node cluster
# Block Node 3's network

# On Node 1 or 2 (majority partition):
psql -c "SELECT pgraft_is_leader();"  # One returns true
psql -c "SELECT pgraft_add_node(4, '127.0.0.1', 7004);"  # Works!

# On Node 3 (minority):
psql -c "SELECT pgraft_is_leader();"  # Returns false
psql -c "SELECT pgraft_add_node(4, '127.0.0.1', 7004);"  # Fails!
```

### Test 2: Leader in Minority

```bash
# 5-node cluster, Node 1 is leader
# Isolate Node 1 and 2

# Majority partition (3, 4, 5) will elect new leader after ~1 second
# Minority partition (1, 2) cannot elect leader
# When network heals, Node 1 sees higher term and steps down
```

## Mathematical Proof Sketch

**Theorem:** At most one leader per term.

**Proof:**

1. Leader requires majority votes: N/2 + 1
2. Each node votes once per term
3. Two majorities must overlap (Pigeonhole Principle)
4. Overlapping node cannot vote for both candidates
5. Therefore, at most one candidate can get majority
6. **QED: At most one leader per term**

## Comparison with Other Systems

| System | Split-Brain Protection | Method |
|--------|----------------------|---------|
| **pgraft** | **100%** | Raft consensus |
| PostgreSQL streaming replication | No | Manual failover |
| MySQL replication | No | Manual failover |
| Patroni/Stolon | Partial | Requires external consensus (etcd/Zookeeper) |
| PostgreSQL with Pacemaker | Partial | STONITH fencing |

!!! success "pgraft Advantage"
    pgraft provides split-brain protection **natively** without requiring external consensus systems or STONITH devices.

## Best Practices

### 1. Use Odd Number of Nodes

Odd numbers provide better fault tolerance:

- 3 nodes: Tolerates 1 failure
- 5 nodes: Tolerates 2 failures
- 7 nodes: Tolerates 3 failures

Even numbers waste resources:

- 4 nodes: Still tolerates only 1 failure (same as 3)
- 6 nodes: Still tolerates only 2 failures (same as 5)

### 2. Geographic Distribution

For disaster recovery, distribute nodes across:

- Different availability zones
- Different data centers
- Different geographic regions

**Example (5-node cluster):**
```
Region A (2 nodes): Primary data center
Region B (2 nodes): Secondary data center
Region C (1 node): Tiebreaker
```

### 3. Monitor Term Changes

Frequent term changes indicate problems:

```sql
-- Monitor term changes
SELECT pgraft_get_term();

-- If term increases rapidly:
-- - Network instability
-- - Node failures
-- - Election timeout too low
```

## Summary

pgraft provides **guaranteed split-brain protection** through:

- **Quorum-based elections** - Only majority can elect leader  
- **Term monotonicity** - Higher term always wins  
- **Log completeness** - Only up-to-date nodes elected  
- **Mathematical guarantees** - Proven by Raft algorithm

**You can trust pgraft to never allow split-brain scenarios.**

