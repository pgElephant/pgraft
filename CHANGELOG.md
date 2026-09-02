# Changelog

All notable changes to pgraft will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - 2026-09-02

Correctness and security release.  No SQL signatures changed, so upgrading is
`ALTER EXTENSION pgraft UPDATE TO '2.0.0'` followed by a restart (the C and Go
libraries must be reloaded together).

### Fixed - data durability and Raft safety

- Only the first entry of the Raft log was ever written to disk.  `saveToDisk()`
  passed `maxSize = 0` to `MemoryStorage.Entries()`, and etcd's `limitSize()`
  returns exactly one entry for that; every persisted log therefore held a
  single entry with a commit index far past it, and a restart recovered a
  truncated log.
- The Raft state file was never fsynced.  HardState (term and vote) and log
  entries were published with a plain write plus rename, so a crash could lose
  state that Raft had already been told was durable - losing committed writes
  and allowing two leaders in one term.  The temp file and its directory are
  now both synced before the rename is considered done.
- Persistence failures were swallowed.  `Append()` and `SetHardState()` logged
  the error and returned success, and `processReady()` went on to send messages
  and call `Advance()`.  Both now return the error, and `processReady()` stops
  before advancing so the same Ready is retried.
- `loadFromDisk()` could rewind the commit index.  A snapshot with no trailing
  log entries hit a "no entries, so reset Commit to 0" rule, un-committing
  committed data.  Commit is now only ever clamped down to a genuinely higher
  bound, and the snapshot is restored before HardState is validated.
- The Raft log was never compacted.  Nothing on the live path called
  `CreateSnapshot()` or `Compact()`, so the log grew without bound - and since
  the whole log is rewritten on every append, each write got progressively more
  expensive.  `pgraft.snapshot_count` (previously read and then ignored) now
  drives snapshot-and-compact from the Ready loop, recording the real
  `ConfState` so a restart can recover the voter set.
- `Config.Applied` was never set, so every restart redelivered the entire
  committed history to the state machine and re-applied it.  It is now set from
  the durable applied index, except when no snapshot exists to recover the
  configuration from - in that case the log is deliberately replayed, because
  the voter set lives only in those entries.

### Fixed - crashes

- The Go to C apply callbacks were not exported.  The module is linked with
  `-fvisibility=hidden`, so `pgraft_enqueue_for_apply_from_go` was absent from
  the module's symbol table; the Go library binds it lazily via
  `-undefined dynamic_lookup`, resolved it to address 0, and segfaulted the
  background worker on the first committed entry.  Both callbacks are now
  `PGDLLEXPORT`.
- The Go Raft goroutine called PostgreSQL internals directly.  The apply
  callback ran `ShmemInitStruct()` (LWLocks, `MyProc`), took a spinlock, and
  called `elog()` - none of which is safe off the main thread, and
  `elog(ERROR)` siglongjmps into another thread's exception stack.  The apply
  queue is now a lock-free single-producer/single-consumer ring; the producer
  path takes no locks, allocates nothing, and logs nothing, reporting dropped
  entries through counters the worker surfaces instead.
- Worker shared memory was never allocated at startup.
  `pgraft_worker_init_shared_memory()` was an empty stub, so the command and
  apply queues were created lazily by whichever process touched them first,
  with no lock: a second process could observe `found == true` and use the
  spinlock before `SpinLockInit()` had run.  All five segments are now
  allocated in the shmem startup hook under `AddinShmemInitLock`, and the
  getters are pure attach.
- `pgraft_go_remove_peer()` dereferenced a nil `raftNode` and had no
  `recover()`, so a panic propagated through cgo and killed the worker process.
- `pgraft_go_tick()` cached its resolved symbol in a function static that
  unload/reload left dangling, and a single failed lookup disabled ticking for
  the life of the process.

### Security

- Every SQL function was executable by PUBLIC, PostgreSQL's default for a new
  function.  Any logged-in role could rewrite cluster membership with
  `pgraft_remove_node()`, push entries into the replicated log with
  `pgraft_replicate_entry()`, or wipe the key/value store.  Mutating functions
  are now revoked from PUBLIC; read-only introspection is granted back
  explicitly.
- SQL injection in the replicated log-entry parser.  `pgraft_parse_kv_json_entry()`
  interpolated the replicated key and value straight into
  `SELECT pgraft_kv_put_local('%s', '%s')`, which then ran through
  `SPI_execute()` - so a key containing a quote executed arbitrary SQL on every
  node in the cluster.  Both are now passed through `quote_literal_cstr()`.
- Peer frame lengths were unbounded.  The 4-byte length prefix from an
  unauthenticated peer socket was fed straight to `make([]byte, msgLen)`,
  allowing a single frame to request up to 4 GiB.  Frames are now capped at
  64 MiB and an oversized announcement drops the connection.

### Fixed - correctness

- `pgraft_core_update_nodes()` zeroed every node's port and leader flag.  It
  rebuilt the array from the id/address pairs the Go layer reports and ran
  every ~500 ms, so ports registered by `pgraft_core_add_node()` were
  discarded.  Ports are now parsed out of the reported `host:port` and
  unreported fields are carried over.
- `pgraft_parse_log_entry()` mis-parsed entries.  `strtok_r` collapses runs of
  delimiters, so an empty database or schema shifted every later field, and
  parsing stopped at the first `|` inside the SQL text.
- `pgraft_remove_completed_commands()` corrupted the status ring: it compacted
  survivors to the head and then also advanced the head past them.
- `pgraft_serialize_log_entry()` returned `snprintf`'s would-be length, so a
  truncated entry reported a length past the end of its buffer.
- Recoverable failures used `elog(ERROR)` inside the background worker, which
  unwound the apply and command loops the caller was trying to protect, left
  `result = -1` lines unreachable, and leaked json-c objects allocated off the
  palloc heap.
- Peer writes had no deadline.  `sendMessage()` is called synchronously from
  the single Ready consumer, so one peer that vanished without closing its
  socket stalled all Raft progress indefinitely.
- The apply path now refuses SQL entries explicitly instead of crashing: the
  worker holds `BGWORKER_SHMEM_ACCESS` only, so `GetTransactionSnapshot()` and
  `SPI_connect()` cannot legally run in it.

### Changed

- 26 GUCs were registered without the `pgraft.` prefix (`cert_file`, `metrics`,
  `cors`, `auto_compaction_mode`, ...), so they were unreachable under their
  documented names and collided in the global GUC namespace.  All 43 are now
  properly qualified; user-facing names are unchanged for the ones that already
  were.
- The GUC C variables were exported as generic globals (`name`, `data_dir`,
  `metrics`, `key_file`, ...) from a library loaded `RTLD_GLOBAL`, a collision
  hazard against the backend and other extensions.  They now carry a
  `pgraft_guc_` prefix.  GUC names are unaffected.
- The background worker no longer busy-polls.  It waits on its latch, honours
  `CHECK_FOR_INTERRUPTS()` and SIGHUP, unblocks signals before its startup
  delays (it previously ignored shutdown requests for the first several
  seconds), and emits periodic diagnostics roughly once a minute instead of
  several log lines every 100 ms.
- `src/pgraft_go.h` is no longer tracked; it is generated by
  `go build -buildmode=c-shared` and removed by `make clean`, so hand edits
  were silently reverted on the next build.
- Removed dead code: `src/pgraft_go_apply.c` (never in `OBJS`, no references),
  and the unreachable `processRaftReady`, `processRaftTicker`, `processMessage`,
  `sendToNode`, `broadcastToAllNodes`, `processCommittedEntry`,
  `handleIncomingMessage` and `getClusterNodes` in the Go layer.  Several were
  live hazards: `sendToNode` wrote frames without holding the connection mutex,
  `processCommittedEntry` mutated indices non-atomically, and `getClusterNodes`
  iterated array indices as if they were node IDs and fell back to a hardcoded
  `{1, 2, 3}` voter set.
- CI now builds with Go 1.23, matching `src/go.mod`.

### Known limitations

- The peer transport is unauthenticated and unencrypted.  A node ID is taken
  from the wire and trusted, and the `pgraft.*_cert_file` GUCs are accepted but
  not yet used.  Do not expose the peer port outside a trusted network.
- Node IDs are assigned from each member's position in `pgraft.initial_cluster`,
  so every node must list members in the same order or they will disagree about
  identities.
- SQL/DDL replication is not implemented; only shared-memory key/value entries
  are applied.

## [1.0.0] - 2024-01-XX

### Added
- Initial release of pgraft
- Raft consensus protocol integration using etcd-io/raft library
- Automatic leader election with quorum-based voting
- Crash-safe log replication across cluster nodes
- Background worker architecture for Raft state machine
- SQL functions for cluster management and status monitoring
- Support for dynamic node addition and removal
- Persistent storage for Raft state and log entries
- Split-brain prevention with mathematical guarantees
- Comprehensive documentation and examples
- GitHub Actions CI/CD workflows for multiple PostgreSQL versions (16, 17, 18)
- RPM and DEB packaging support
- Prometheus metrics integration
- PostgreSQL extension registration and control files

### Technical Details
- Written in PostgreSQL C with Go integration for Raft consensus
- Compatible with PostgreSQL versions 16, 17, and 18
- Zero compilation warnings
- Follows PostgreSQL C coding conventions
- Comprehensive error handling and logging
- Production-ready quality and testing

[Unreleased]: https://github.com/pgElephant/pgraft/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/pgElephant/pgraft/releases/tag/v1.0.0
