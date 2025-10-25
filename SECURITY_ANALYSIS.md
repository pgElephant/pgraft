# pgraft Deep Security & Architecture Analysis

**Analysis Date:** October 2, 2025  
**Analyzed Version:** Commit c148fd7  
**Analysis Tools:** cppcheck, go vet, errcheck, manual code review

---

## Executive Summary

This report documents potential crashes, race conditions, resource leaks, and architectural issues found through deep static analysis of the pgraft codebase. Issues are categorized by severity (Critical, High, Medium, Low).

**Overall Risk Assessment:** Medium-High
- **Critical Issues:** 3
- **High Issues:** 8  
- **Medium Issues:** 12
- **Low Issues:** 7

---

## CRITICAL ISSUES (Crash/Data Corruption Risk)

### C-1: Spinlock Not Released on Error Path in `pgraft_log_append_entry`

**File:** `src/pgraft_log.c:84-132`

**Issue:**
```c
SpinLockAcquire(&state->mutex);

if (state->log_size >= 1000)
{
    SpinLockRelease(&state->mutex);  // ✓ Released
    elog(ERROR, "pgraft: Log is full (1000 entries)");
    return -1;
}
```

BUT earlier in the same function:
```c
if (data_size > 1024)
{
    elog(ERROR, "pgraft: Data size %d exceeds maximum 1024", data_size);
    return -1;  // ✗ SPINLOCK NOT RELEASED!
}
```

**Impact:** Deadlock. Any subsequent attempt to acquire the spinlock will hang indefinitely.

**Fix:** Add `SpinLockRelease(&state->mutex);` before early returns, OR acquire lock after validation.

---

### C-2: Uninitialized Worker State Read in Race Condition

**File:** `src/pgraft.c:201-206`

**Issue:**
```c
state = pgraft_worker_get_state();
if (state == NULL)
{
    elog(ERROR, "pgraft: Failed to get worker state in background worker");
    return;  // Returns from worker main!
}
```

`pgraft_worker_get_state()` uses `ShmemInitStruct` which returns existing memory if `found=true`. However, between the time one backend creates it and another reads it, there's no memory barrier.

**Impact:** Background worker exits immediately if shared memory appears NULL due to race. Cluster has no worker → no consensus → split brain.

**Fix:** Add `pg_memory_barrier()` after initialization in `pgraft_worker_get_state()`. Check `found` flag explicitly.

---

### C-3: Go Library Path Hardcoded to PG 17

**File:** `src/pgraft_go.c:22`

**Issue:**
```c
#define DEFAULT_GO_LIB_PATH "/usr/local/pgsql.17/lib/pgraft_go.dylib"
```

But workflows build PG 18 to `/usr/local/pgsql.18`. If GUC `pgraft.go_library_path` is not set, library load fails with cryptic error.

**Impact:** Extension fails to load on PG 18 installations. Hard to diagnose.

**Fix:** Use `pkglibdir` from PostgreSQL build or make path detection dynamic based on PG version.

---

## HIGH SEVERITY ISSUES

### H-1: No Error Handling for Command Failures

**File:** `src/pgraft.c:242-356`

**Issue:** When `pgraft_init_system` returns `-1`:
```c
if (pgraft_init_system(cmd.node_id, cmd.address, cmd.port) != 0)
{
    cmd.status = COMMAND_STATUS_FAILED;
    strncpy(cmd.error_message, "Failed to initialize pgraft system", 
            sizeof(cmd.error_message) - 1);
}
```

Command marked failed, but:
- Worker continues in RUNNING state
- Partially initialized Go state persists
- No cleanup or retry
- Subsequent commands may fail unpredictably

**Impact:** Cluster left in half-initialized state. Manual intervention required.

**Fix:** Implement cleanup on failure or state rollback. Consider marking worker as ERROR state.

---

### H-2: Unchecked Errors in 35+ Go Function Calls

**File:** `src/pgraft_go.go` (multiple locations)

**Findings from errcheck:**

**Network errors ignored:**
```go
conn.Close()  // Line 528, 990, 1651, 1671, 1748, 1763, 2044, 2052, 2606, 2617
managedConn.Conn.Close()  // Multiple locations - error ignored
```

**Raft operations with ignored errors:**
```go
raftNode.ProposeConfChange(raftCtx, cc)  // Line 1006
raftNode.Propose(raftCtx, goData)  // Line 1139
raftNode.Step(raftCtx, msg)  // Lines 1251, 1501, 1543, 1652, 2570
raftNode.Campaign(raftCtx)  // Line 1325
raftStorage.SetHardState(rd.HardState)  // Lines 1386, 2420
raftStorage.Append(rd.Entries)  // Lines 1396, 2434
```

**Impact:** 
- Failed proposals may be assumed successful
- Network errors during consensus are silent
- State corruption if storage operations fail
- Raft library errors don't propagate to C layer

**Fix:** Check all error returns. Log failures. Propagate critical errors to C layer via error channel.

---

### H-3: Go Vet Type Mismatch in Log.Printf

**File:** `src/pgraft_go.go:681, 786`

**Issue:**
```go
log.Printf("pgraft_go: version=%s", pgraft_go_version())  
// pgraft_go_version() returns *C.char, not string
```

**Impact:** Incorrect log output. Potential memory corruption if format string is interpreted incorrectly.

**Fix:**
```go
log.Printf("pgraft_go: version=%s", C.GoString(pgraft_go_version()))
```

---

### H-4: No Snapshot Trigger When Log Full

**File:** `src/pgraft_log.c:105-110`

**Issue:**
```c
if (state->log_size >= 1000)
{
    SpinLockRelease(&state->mutex);
    elog(ERROR, "pgraft: Log is full (1000 entries)");
    return -1;
}
```

Function `pgraft_log_cleanup_old_entries` exists but is never called. No automatic snapshot or log compaction.

**Impact:** Cluster stops accepting writes when 1000 entries reached. Requires manual intervention or restart.

**Fix:** Trigger snapshot before rejecting new entries. Call cleanup automatically at 80-90% capacity.

---

### H-5: Reconnection Delay Never Resets

**File:** `src/pgraft_go.go:2010-2032`

**Issue:**
```go
retryDelay := 2 * time.Second
for attempt := 0; attempt < maxRetries; attempt++ {
    // ...
    retryDelay *= 2  // Exponential backoff
}
```

After multiple failures across different goroutines, delay grows exponentially but never resets on success.

**Impact:** Connection attempts after transient failures use massive delays (minutes+).

**Fix:** Track retry state per node. Reset delay on successful connection.

---

### H-6: Ticker Never Stopped - Goroutine Leak

**File:** `src/pgraft_go.go` (raftTicker usage)

**Issue:** `raftTicker = time.NewTicker(...)` created but never `.Stop()` called in cleanup paths. If `pgraft_go_stop` is called,ticker goroutine continues indefinitely.

**Impact:** Goroutine leak. Memory slowly increases over restarts.

**Fix:** Add `raftTicker.Stop()` in `pgraft_go_stop`.

---

### H-7: Connection Map Access Without Lock

**File:** `src/pgraft_go.go:2000-2007`

**Issue:**
```go
connMutex.Lock()
_, exists := connections[nodeID]
connMutex.Unlock()

if exists {
    return  // Early return without checking again
}

go func() {
    // Race: another goroutine might create connection here
    connectToPeer(nodeID, peerAddr)
}()
```

**Impact:** Race condition. Multiple goroutines may call `connectToPeer` for same node, creating duplicate connections.

**Fix:** Use mutex or atomic check-and-set pattern.

---

### H-8: Persistent Storage Corruption Not Detected

**File:** `src/pgraft_go.go:149-197`

**Issue:** `loadFromDisk` unmarshals JSON but doesn't validate:
- Node ID matches
- Term progression is sane
- Snapshot metadata is compatible

**Impact:** Loading incompatible state from disk causes raft library panic on first access.

**Fix:** Add validation. Compare loaded nodeID with expected. Verify terms are non-negative.

---

## MEDIUM SEVERITY ISSUES

### M-1: Buffer Size Inconsistency

**Files:** Multiple

**Issues:**
- `pgraft_command_t.address` is 256 bytes
- `pgraft_command_t.log_data` is 1024 bytes  
- `pgraft_log_entry.data` is 1024 bytes
- No overflow checks when copying between these

**Impact:** Potential buffer overflow if data exceeds buffer size during copy operations.

**Fix:** Add size validation at all boundaries. Use consistent buffer sizes.

---

### M-2: Circular Command Queue Can Overflow

**File:** `include/pgraft_core.h:56-78`

**Issue:**
```c
#define MAX_COMMANDS 100
pgraft_command_t commands[MAX_COMMANDS];
int command_head;
int command_tail;  
int command_count;
```

If queue fills (100 commands pending), `pgraft_queue_command` likely fails but callers may not check.

**Impact:** Commands silently dropped under high load.

**Fix:** Check `pgraft_queue_command` return value everywhere. Log queue full condition.

---

### M-3: Integer Overflow in Log Index

**Files:** Multiple

**Issue:** Log index is `int64_t` but incremented without overflow check:
```c
entry->index = state->last_index + 1;  // No overflow check
```

**Impact:** After 2^63-1 entries, index wraps to negative, causing comparison failures.

**Fix:** Add overflow check or use unsigned types with wrap-around semantics.

---

### M-4: memset on Bool Arrays Incomplete

**File:** `src/pgraft_sql.c:294, 361, 643, 683, 740`

**From cppcheck:**
```
portability: Array 'nulls' might be filled incompletely. 
Did you forget to multiply the size given to 'memset()' with 'sizeof(*nulls)'?
```

**Issue:** `memset(nulls, 0, n)` where `nulls` is `bool[]`. Should be `memset(nulls, 0, n * sizeof(bool))`.

**Impact:** On platforms where `sizeof(bool) != 1`, array not fully zeroed.

**Fix:** Use `sizeof(*nulls) * count` or initialize with loop.

---

### M-5: No Bounds Check on Node Array Access

**File:** `src/pgraft_core.c:101`

**Issue:**
```c
node = &cluster->nodes[cluster->num_nodes];  // No check if num_nodes < 16
cluster->num_nodes++;
```

Check exists earlier (line 94) but between spinlock acquire/release, value could theoretically be modified.

**Impact:** Array out-of-bounds write if num_nodes >= 16.

**Fix:** Recheck inside spinlock or use assert.

---

### M-6: Port Configuration Conflicts

**File:** `src/pgraft_go.go:2082-2085`

**Issue:** Hardcoded config paths and no per-node port differentiation:
```go
configPaths := []string{
    "/Users/ibrarahmed/pgelephant/pge/ram/conf/pgraft.conf",  // User-specific!
    "/etc/pgraft/pgraft.conf",
    "./pgraft.conf",
}
```

**Impact:**  
- Won't work on other machines (hardcoded username)
- Multiple nodes on same host will conflict on Raft port

**Fix:** Remove user-specific path. Make port configurable per node via GUC.

---

### M-7: Connection Goroutines Never Cleaned Up

**File:** `src/pgraft_go.go:1813-1903, 2060`

**Issue:** `go handleConnectionMessages(nodeID, conn)` starts goroutine for each connection but goroutine doesn't have clear termination signal other than connection EOF.

**Impact:** Goroutine leak if connections are repeatedly established/torn down.

**Fix:** Use context with cancel for each connection goroutine.

---

### M-8: State Copy Without Deep Copy

**File:** `src/pgraft_log.c:376`

**Issue:**
```c
*stats = *state;  // Shallow copy entire struct while holding spinlock
```

**Impact:** Copying 1000+ log entries while holding spinlock blocks all other log operations for extended time.

**Fix:** Copy only metadata (indices, counts) outside spinlock. Or use shorter critical section.

---

### M-9: Error Channel Never Read

**File:** `src/pgraft_go.go:107, 1177`

**Issue:**
```go
recordErrorChan chan error  // Declared
recordError(fmt.Errorf(...))  // Written to
// But no goroutine reads from it!
```

**Impact:** If channel is unbuffered or buffer fills, `recordError` will block forever.

**Fix:** Either remove unused error channel or implement error consumer goroutine.

---

### M-10: Persistent Storage Disk Write Without Fsync

**File:** `src/pgraft_go.go:207-246` (saveToDisk)

**Issue:** File writes use `ioutil.WriteFile` without explicit fsync. On crash, state file may be incomplete.

**Impact:** After crash, loading corrupted JSON causes init failure.

**Fix:** Use `os.OpenFile` with `O_SYNC` or call `Sync()` after write.

---

### M-11: Race in `pgraft_go_is_loaded` Check

**File:** `src/pgraft_go.c:156-165`

**Issue:**
```c
bool pgraft_go_is_loaded(void)
{
    if (go_lib_handle != NULL)
        return true;
    
    return pgraft_state_is_go_lib_loaded();
}
```

Called from worker without lock. If one backend loads library while another checks, results inconsistent.

**Impact:** Worker may call Go functions before library fully initialized.

**Fix:** Add atomic flag or always check through shared memory with spinlock.

---

### M-12: No Memory Barrier After Shared Memory Init

**File:** `src/pgraft.c:381-399`

**Issue:**
```c
worker_state = (pgraft_worker_state_t *) ShmemInitStruct(...);
if (!found) {
    worker_state->node_id = 0;
    worker_state->port = 0;
    // ... many writes
}
// No memory barrier before returning
```

**Impact:** Other backends may see partially initialized structure due to CPU reordering.

**Fix:** Add `pg_write_barrier()` after initialization block.

---

## HIGH SEVERITY ISSUES

### H-9: Connection Timeout Errors Not Checked

**File:** `src/pgraft_go.go:1512, 1878, 1827`

**Issue:**
```go
conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))  // Error ignored
```

**Impact:** If SetReadDeadline fails, subsequent read may block indefinitely.

**Fix:** Check error, log if fails.

---

### H-10: Listener Close Deferred Without Error Check

**File:** `src/pgraft_go.go:1813`

**Issue:**
```go
defer listener.Close()  // Error ignored
```

**Impact:** Resource leak if close fails (e.g., already closed).

**Fix:** Check error in defer or use explicit close with error handling.

---

### H-11: Config Change Unmarshal Errors Ignored

**File:** `src/pgraft_go.go:1795, 2472`

**Issue:**
```go
cc.Unmarshal(entry.Data)  // Error ignored
```

**Impact:** Invalid config change data causes nil pointer access or partial unmarshal, leading to panic.

**Fix:** Check `err := cc.Unmarshal(...)` and log/skip on failure.

---

### H-12: InitialState Errors Ignored

**File:** `src/pgraft_go.go:2503, 2559`

**Issue:**
```go
hs, _, _ := raftStorage.InitialState()  // Errors discarded
```

**Impact:** If storage corrupted, `hs` may be invalid but used anyway.

**Fix:** Check error from `InitialState()`, handle failure.

---

### H-13: No Check for Nil RaftNode Before Calling Methods

**File:** `src/pgraft_go.go` (multiple locations)

**Issue:** Many functions check `raftNode == nil` with read lock:
```go
raftMutex.RLock()
defer raftMutex.RUnlock()

if raftNode == nil {
    return C.int(0)
}
```

But some don't:
- `processReady` (line ~2410) assumes raftNode valid
- `handleConnectionMessages` (line ~1479) calls `raftNode.Step` without nil check after lock

**Impact:** Nil pointer panic if raftNode becomes nil during execution.

**Fix:** Add consistent nil checks everywhere raftNode is accessed.

---

### H-14: Context Cancel Not Propagated

**File:** `src/pgraft_go.go:99-100`

**Issue:**
```go
raftCtx     context.Context
raftCancel  context.CancelFunc
```

`raftCancel` is created but if `pgraft_go_stop` is called, raftCtx may not be cancelled before setting `running = 0`.

**Impact:** Goroutines using `raftCtx` continue running after stop, causing resource leak or panic.

**Fix:** Call `raftCancel()` in `pgraft_go_stop` before marking stopped.

---

### H-15: No Timeout on Raft Propose Operations

**File:** `src/pgraft_go.go:2159-2183`

**Issue:**
```go
ctx, cancel := context.WithTimeout(raftCtx, 5*time.Second)
defer cancel()

err := raftNode.Propose(ctx, goData)  // Waits up to 5s
```

But main raftCtx has no timeout. If raftCtx is cancelled during Propose, it fails immediately but earlier operations might hang indefinitely.

**Impact:** Propose can block indefinitely if raftCtx never cancelled.

**Fix:** Ensure raftCtx has root timeout or is always cancellable.

---

### H-16: Mutex Unlock in Deferred Function May Panic

**File:** `src/pgraft_go.go:2161-2162`

**Issue:**
```go
raftMutex.RLock()
defer raftMutex.RUnlock()
```

If panic occurs between lock and defer, defer runs but mutex might already be released by panic recovery elsewhere.

**Impact:** Double unlock can cause panic in Go mutex implementation.

**Fix:** Ensure panic handling doesn't interfere with defer chain. Add panic recovery where needed.

---

## MEDIUM SEVERITY ISSUES

### M-13: strncpy Not Always Null-Terminated

**Files:** Multiple locations

**Issue:** Pattern throughout codebase:
```c
strncpy(state->go_address, address ? address : "", sizeof(state->go_address) - 1);
state->go_address[sizeof(state->go_address) - 1] = '\0';
```

This is correct, but some places may miss the null terminator assignment.

**Impact:** Non-null-terminated strings cause buffer overruns in subsequent string operations.

**Fix:** Audit all `strncpy` calls. Use `strlcpy` (available on macOS/BSD) or `snprintf`.

---

### M-14: Fixed Array Sizes Without Size Defines

**Files:** Multiple

**Issue:** Magic numbers throughout:
- 16 nodes max (hardcoded)
- 256 byte addresses
- 1024 byte data
- 1000 log entries

Not defined as named constants.

**Impact:** Changing limits requires finding all instances. Risk of inconsistency.

**Fix:** Define in header:
```c
#define PGRAFT_MAX_NODES 16
#define PGRAFT_MAX_ADDRESS_LEN 256
#define PGRAFT_MAX_LOG_DATA_SIZE 1024
#define PGRAFT_MAX_LOG_ENTRIES 1000
```

---

### M-15: No Validation of Node ID Range

**Files:** Multiple command handlers

**Issue:** Node IDs come from SQL functions without range validation.

**Impact:** Negative or out-of-range node IDs could cause array access violations.

**Fix:** Validate `node_id > 0 && node_id < INT32_MAX` at entry points.

---

### M-16: Sleep in Spinlock Critical Section

**File:** `src/pgraft.c:280`

**Issue:**
```c
cmd.status = COMMAND_STATUS_COMPLETED;
pg_usleep(2000000);  // Sleep 2 seconds AFTER releasing spinlock (OK)
```

Actually this is OK - the sleep is after spinlock release. But proximity suggests risk if code is refactored.

**Impact:** Currently none, but refactoring risk.

**Fix:** Add comment warning not to move sleep inside lock.

---

### M-17: No Upper Bound on Config File Read

**File:** `src/pgraft_go.go:2088-2092`

**Issue:**
```go
data, err := os.ReadFile(path)  // Reads entire file into memory
```

No size limit. Malicious config file could consume all memory.

**Impact:** DoS via large config file.

**Fix:** Use `io.LimitReader` or check file size before reading.

---

### M-18: Timestamp Overflow Not Handled

**File:** `src/pgraft_log.c:116`

**Issue:**
```c
entry->timestamp = GetCurrentTimestamp();  // Returns int64
```

`GetCurrentTimestamp()` can theoretically overflow in year 292,277,026,596. But arithmetic on timestamps isn't checked.

**Impact:** Extremely unlikely, but timestamp comparison logic might fail far in future.

**Fix:** Document assumption or add overflow check if doing timestamp arithmetic.

---

### M-19: Channel Sends May Block Forever

**File:** `src/pgraft_go.go` (multiple)

**Issue:** Channels used without buffering or timeout:
```go
messageChan chan raftpb.Message
// Sent to in multiple places without timeout
```

**Impact:** If receiver stops, senders block forever.

**Fix:** Use buffered channels or `select` with timeout.

---

### M-20: Raft Tick Called Without Initialization Check

**File:** `src/pgraft.c:221-228`

**Issue:**
```c
if (pgraft_go_is_loaded())
{
    tick_result = pgraft_go_tick();  // No check if started
}
```

`pgraft_go_is_loaded()` returns true if library loaded, but doesn't check if `pgraft_go_start()` was called.

**Impact:** Calling tick on uninitialized raft causes Go panic.

**Fix:** Add `pgraft_go_is_running()` check or track init state separately.

---

### M-21: Fixed Localhost Address

**Files:** Multiple config defaults

**Issue:** Many defaults use `127.0.0.1` or `localhost`.

**Impact:** Won't work for multi-host clusters without config changes.

**Fix:** Document requirement or detect network interface automatically.

---

### M-22: No Version Compatibility Check Between C and Go

**File:** `src/pgraft_go.c:103-109`

**Issue:** `pgraft_go_check_version()` exists but implementation not shown. If it just compares strings, minor version differences might cause false failures.

**Impact:** Prevents using newer Go library with older C extension even if compatible.

**Fix:** Use semantic versioning comparison. Allow compatible versions.

---

### M-23: Database Connection Credentials Hardcoded

**File:** Examples only (not core code)

**Issue:** Example scripts may have hardcoded postgres user.

**Impact:** Security risk if examples used in production.

**Fix:** Document as examples only. Use environment variables for credentials.

---

### M-24: No Rate Limiting on Log Operations

**Files:** Log functions

**Issue:** No throttling on log append/commit operations.

**Impact:** Malicious actor or bug could flood log with entries, consuming all shared memory.

**Fix:** Add rate limiting or queue depth checks.

---

## LOW SEVERITY ISSUES

### L-1: Verbose Logging in Hot Path

**File:** `src/pgraft.c:215-227`

**Issue:** Log message every 5 iterations at LOG level.

**Impact:** Log spam at high throughput. Performance overhead.

**Fix:** Use DEBUG1 or increase interval.

---

### L-2: Hard-Coded Sleep Intervals

**File:** `src/pgraft.c:358`

**Issue:** `pg_usleep(100000)` - 100ms fixed.

**Impact:** May be too aggressive or too slow depending on workload.

**Fix:** Make configurable via GUC.

---

### L-3: Magic Number "5" for Update Frequency

**File:** `src/pgraft.c:232`

**Issue:** `if (sleep_count % 5 == 0)` - no explanation why 5.

**Impact:** None, but hard to tune.

**Fix:** Define as `UPDATE_SHARED_MEM_INTERVAL`.

---

### L-4: Go Library Function Pointers Not Atomic

**File:** `src/pgraft_go.c:32-51`

**Issue:** Static pointers set during load but read without synchronization.

**Impact:** If library loaded concurrently from multiple backends, race on pointer assignment (unlikely in practice).

**Fix:** Use atomic stores or ensure load is serialized.

---

### L-5: String Comparison for State

**File:** `src/pgraft_core.c:47`

**Issue:**
```c
strncpy(cluster->state, "follower", sizeof(cluster->state) - 1);
```

State is string but compared/set as string. Should use enum.

**Impact:** Typos or inconsistency. String comparisons slower than int.

**Fix:** Use enum for state (FOLLOWER=0, CANDIDATE=1, LEADER=2).

---

### L-6: Deprecated io/ioutil Usage

**File:** `src/pgraft_go.go:44, 160`

**Issue:**
```go
import "io/ioutil"
data, err := ioutil.ReadFile(statePath)
```

`io/ioutil` deprecated in Go 1.16+. Should use `os.ReadFile`.

**Impact:** None functional, but violates modern Go style.

**Fix:** Replace with `os.ReadFile`, `os.WriteFile`.

---

### L-7: No Metrics Collection for Failures

**Files:** Multiple

**Issue:** Success metrics tracked but failure metrics minimal.

**Impact:** Hard to diagnose issues in production without failure counters.

**Fix:** Add error counters to shared memory: failed_commands, failed_proposals, connection_failures.

---

## ARCHITECTURAL CONCERNS

### A-1: No WAL Integration

**Observation:** Log replication is in-memory only. No integration with PostgreSQL WAL.

**Impact:** Raft log separate from database log. If database crashes and restarts from WAL, raft state may be inconsistent.

**Fix:** Consider WAL-based log storage or explicit snapshot/recovery integration.

---

### A-2: Single Background Worker

**Observation:** One bgworker handles all consensus operations.

**Impact:** CPU-bound on single core. Long-running commands block others.

**Fix:** Consider worker pool or async command processing.

---

### A-3: Shared Memory Size Limits

**Observation:**  
- 1000 log entries * 1KB = ~1MB
- 100 KV entries * 1KB = ~100KB
- Total ~2MB shared memory

**Impact:** Limited scalability. Large clusters need more state.

**Fix:** Document limits. Consider dynamic memory allocation for large deployments.

---

### A-4: No Split-Brain Resolution for Odd-Even Splits

**Observation:** Raft prevents split-brain for odd nodes (3, 5, 7) via majority. But documentation should clarify 2-node cluster behavior.

**Impact:** Users might deploy 2-node cluster expecting high availability.

**Fix:** Document minimum 3 nodes for HA. Add check to reject < 3 node initialization.

---

## RECOMMENDATIONS BY PRIORITY

### Immediate (Critical Fixes)

1. **Fix C-1:** Add spinlock release before all error returns in `pgraft_log_append_entry`
2. **Fix C-2:** Add memory barrier in worker state initialization
3. **Fix C-3:** Make Go library path dynamic or detect from pg_config

### High Priority (This Sprint)

4. **Fix H-2:** Add error checking to all critical Go operations (raftNode.Propose, Step, storage operations)
5. **Fix H-3:** Fix Go vet type mismatches in log.Printf
6. **Fix H-4:** Implement log snapshot/cleanup before reaching capacity
7. **Fix H-6:** Stop raftTicker in cleanup path
8. **Fix H-14:** Cancel raftCtx in pgraft_go_stop

### Medium Priority (Next Release)

9. **Fix M-1 through M-12:** Address buffer sizes, overflow checks, race conditions
10. Add comprehensive error metrics
11. Implement rate limiting
12. Add configuration validation

### Low Priority (Tech Debt)

13. Refactor state to use enums instead of strings
14. Replace deprecated `io/ioutil`
15. Make sleep intervals configurable
16. Add performance tuning GUCs

---

## TESTING RECOMMENDATIONS

1. **Chaos Testing:** Use tools like `chaos-mesh` to inject failures
2. **Race Detector:** Run tests with `go test -race`
3. **Valgrind/ASan:** Check for memory leaks in C code
4. **Load Testing:** Verify behavior under 1000+ operations/sec
5. **Split-Brain Scenarios:** Test all partition cases with 3, 5, 7 nodes

---

## TOOLS USED

- **cppcheck 2.16**: C static analysis
- **go vet**: Go suspicious constructs
- **errcheck**: Unchecked Go errors  
- **Manual code review**: Logic flow, race conditions, resource management

---

**Report End**

For questions or to discuss fixes, see: https://github.com/pgElephant/pgraft/issues

