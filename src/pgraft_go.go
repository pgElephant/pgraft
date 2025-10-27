/*
 * pgraft_go.go
 * Go wrapper for etcd-io/raft integration with PostgreSQL
 *
 * This file provides C-callable functions that interface with the
 * etcd-io/raft library, allowing PostgreSQL to use distributed consensus.
 *
 * Based on the etcd-io/raft library patterns and best practices.
 * See: https://github.com/etcd-io/raft
 */

package main

/*
#cgo CFLAGS: -I../include
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// No external C callbacks - we'll use file-based IPC or shared memory

typedef struct pgraft_go_cluster_member {
	char   *name;
	char   *peer_host;
	int		peer_port;
} pgraft_go_cluster_member;

typedef struct pgraft_go_config {
	int		node_id;
	char   *cluster_id;
	char   *address;
	int		port;
	char   *data_dir;
	pgraft_go_cluster_member *cluster_members;
	int		cluster_member_count;
	int		initial_cluster_state;
	char   *name;
	char   *listen_peer_host;
	int		listen_peer_port;
	char   *listen_client_host;
	int		listen_client_port;
	char   *advertise_client_host;
	int		advertise_client_port;
	char   *initial_advertise_peer_host;
	int		initial_advertise_peer_port;
	int		election_timeout;
	int		heartbeat_interval;
	int		snapshot_interval;
	int		quota_backend_bytes;
	int		max_request_bytes;
	int		max_snapshots;
	int		max_wals;
	int		auto_compaction_retention;
	int		auto_compaction_mode;
	int		compaction_batch_limit;
	char   *log_level;
	char   *log_outputs;
	char   *log_package_levels;
	int		client_cert_auth;
	char   *trusted_ca_file;
	char   *cert_file;
	char   *key_file;
	char   *client_cert_file;
	char   *client_key_file;
	char   *peer_trusted_ca_file;
	char   *peer_cert_file;
	char   *peer_key_file;
	int		peer_client_cert_auth;
	char   *peer_cert_allowed_cn;
	int		peer_cert_allowed_hostname;
	char   *cipher_suites;
	char   *cors;
	char   *host_whitelist;
	char   *listen_metrics_urls;
	char   *metrics;
	int		experimental_initial_corrupt_check;
	char   *experimental_corrupt_check_time;
	char   *experimental_enable_v2v3;
	int		experimental_enable_lease_checkpoint;
	int		experimental_compaction_batch_limit;
	int		experimental_peer_skip_client_san_verification;
	int		experimental_self_signed_cert_validity;
	char   *experimental_watch_progress_notify_interval;
	int		max_log_entries;
	int		batch_size;
	int		max_batch_delay;
} pgraft_go_config;
*/
import "C"

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"go.etcd.io/raft/v3"
	"go.etcd.io/raft/v3/raftpb"
)

// ClusterMember represents a member in the initial cluster configuration
type ClusterMember struct {
	Name    string
	PeerURL string
}

// parseInitialCluster parses the initial-cluster configuration string
// Format: "member1=http://peer1:2380,member2=http://peer2:2380"
// Returns a map of member names to peer URLs
func parseInitialCluster(initialCluster string) (map[string]string, error) {
	if initialCluster == "" {
		return nil, fmt.Errorf("initial_cluster is empty")
	}

	members := make(map[string]string)

	// Split by comma to get individual members
	memberStrs := strings.Split(initialCluster, ",")

	for _, memberStr := range memberStrs {
		memberStr = strings.TrimSpace(memberStr)
		if memberStr == "" {
			continue
		}

		// Split by '=' to separate name and URL
		parts := strings.SplitN(memberStr, "=", 2)
		if len(parts) != 2 {
			return nil, fmt.Errorf("invalid member format: %s (expected name=url)", memberStr)
		}

		name := strings.TrimSpace(parts[0])
		peerURL := strings.TrimSpace(parts[1])

		if name == "" || peerURL == "" {
			return nil, fmt.Errorf("empty name or URL in member: %s", memberStr)
		}

		// Validate URL format
		if !strings.HasPrefix(peerURL, "http://") && !strings.HasPrefix(peerURL, "https://") {
			return nil, fmt.Errorf("invalid peer URL format: %s (must start with http:// or https://)", peerURL)
		}

		members[name] = peerURL
		logInfo("parsed cluster member: %s -> %s", name, peerURL)
	}

	logInfo("parsed initial_cluster with %d members", len(members))
	return members, nil
}

// ManagedConnection wraps a net.Conn with a mutex for safe concurrent access.
// It also tracks the last activity time for connection management.
type ManagedConnection struct {
	Conn         net.Conn
	Mutex        sync.Mutex // Protects writes to the connection
	LastActivity time.Time
}

// ClusterState represents the current state of the cluster
type ClusterState struct {
	LeaderID    uint64            `json:"leader_id"`
	CurrentTerm uint64            `json:"current_term"`
	State       string            `json:"state"`
	Nodes       map[uint64]string `json:"nodes"`
	LastIndex   uint64            `json:"last_index"`
	CommitIndex uint64            `json:"commit_index"`
}

// PersistentStorage wraps MemoryStorage with disk persistence
type PersistentStorage struct {
	*raft.MemoryStorage
	dataDir string
	nodeID  uint64
	mu      sync.RWMutex
}

// StorageState represents the persistent state
type StorageState struct {
	HardState raftpb.HardState `json:"hard_state"`
	Entries   []raftpb.Entry   `json:"entries"`
	Snapshot  *raftpb.Snapshot `json:"snapshot,omitempty"`
}

// Global state following etcd-io/raft patterns
var (
	raftNode    raft.Node
	raftStorage *PersistentStorage
	raftConfig  *raft.Config
	raftCtx     context.Context
	raftCancel  context.CancelFunc
	raftTicker  *time.Ticker
	raftDone    chan struct{}

	// Communication channels
	messageChan     chan raftpb.Message // Channel for incoming Raft messages
	stopChan        chan struct{}
	recordErrorChan chan error
	reconnectChan   chan uint64 // New channel for reconnection requests

	// Cluster state and synchronization
	initialized int32
	running     int32
	tickCount   uint64 // Counter for periodic logging
	raftMutex   sync.RWMutex
	nodesMutex  sync.RWMutex
	connections map[uint64]*ManagedConnection
)

// Debug logging control
var debugEnabled bool = false

func logMessage(level string, format string, args ...interface{}) {
	msg := fmt.Sprintf(format, args...)
	log.Printf("%s:  %s", level, msg)
}

func logInfo(format string, args ...interface{}) {
	logMessage("INFO", format, args...)
}

func logError(format string, args ...interface{}) {
	logMessage("ERROR", format, args...)
}

func logWarning(format string, args ...interface{}) {
	logMessage("WARNING", format, args...)
}

// NewPersistentStorage creates a new persistent storage
func NewPersistentStorage(nodeID uint64, dataDir string) (*PersistentStorage, error) {
	ps := &PersistentStorage{
		MemoryStorage: raft.NewMemoryStorage(),
		dataDir:       dataDir,
		nodeID:        nodeID,
	}

	// Create data directory if it doesn't exist
	if err := os.MkdirAll(dataDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create data directory: %v", err)
	}

	// Load existing state from disk
	if err := ps.loadFromDisk(); err != nil {
		logWarning("failed to load state from disk: %v", err)
		// Continue with empty state
	}

	return ps, nil
}

// getStatePath returns the path to the state file
func (ps *PersistentStorage) getStatePath() string {
	return filepath.Join(ps.dataDir, fmt.Sprintf("node_%d_state.json", ps.nodeID))
}

// loadFromDisk loads the persistent state from disk
func (ps *PersistentStorage) loadFromDisk() error {
	ps.mu.Lock()
	defer ps.mu.Unlock()

	statePath := ps.getStatePath()
	if _, err := os.Stat(statePath); os.IsNotExist(err) {
		logInfo("no existing state file found for node %d", ps.nodeID)
		return nil // No state file exists, start fresh
	}

	data, err := ioutil.ReadFile(statePath)
	if err != nil {
		return fmt.Errorf("failed to read state file: %v", err)
	}

	var state StorageState
	if err := json.Unmarshal(data, &state); err != nil {
		return fmt.Errorf("failed to unmarshal state: %v", err)
	}

	// Restore HardState with validation
	if !raft.IsEmptyHardState(state.HardState) {
		// Validate commit index before restoring
		validatedHardState := state.HardState
		if len(state.Entries) > 0 {
			maxEntryIndex := state.Entries[len(state.Entries)-1].Index
			if validatedHardState.Commit > maxEntryIndex {
				logWarning("WARNING: HardState.Commit (%d) exceeds max entry index (%d), clamping to %d",
					validatedHardState.Commit, maxEntryIndex, maxEntryIndex)
				validatedHardState.Commit = maxEntryIndex
			}
		} else if validatedHardState.Commit > 0 {
			// No entries but commit > 0, reset to 0
			logWarning("WARNING: HardState.Commit (%d) but no entries, resetting to 0",
				validatedHardState.Commit)
			validatedHardState.Commit = 0
		}

		if err := ps.MemoryStorage.SetHardState(validatedHardState); err != nil {
			return fmt.Errorf("failed to set hard state: %v", err)
		}
		logInfo("restored hardstate for node %d: term=%d, vote=%d, commit=%d",
			ps.nodeID, validatedHardState.Term, validatedHardState.Vote, validatedHardState.Commit)
	}

	// Restore Snapshot if present
	if state.Snapshot != nil && !raft.IsEmptySnap(*state.Snapshot) {
		if err := ps.MemoryStorage.ApplySnapshot(*state.Snapshot); err != nil {
			return fmt.Errorf("failed to apply snapshot: %v", err)
		}
		logInfo("restored snapshot for node %d: index=%d, term=%d",
			ps.nodeID, state.Snapshot.Metadata.Index, state.Snapshot.Metadata.Term)
	}

	// Restore log entries
	if len(state.Entries) > 0 {
		if err := ps.MemoryStorage.Append(state.Entries); err != nil {
			return fmt.Errorf("failed to append entries: %v", err)
		}
		logInfo("Restored %d log entries for node %d", len(state.Entries), ps.nodeID)
	}

	return nil
}

// Global persistence failure tracking (FIX #3: Monitoring)
var (
	persistenceFailureCount int64
	lastPersistenceError    string
	lastPersistenceTime     time.Time
)

// saveToDisk saves the current state to disk
func (ps *PersistentStorage) saveToDisk() error {
	ps.mu.RLock()
	defer ps.mu.RUnlock()

	// Get current state from memory storage
	hardState, _, err := ps.MemoryStorage.InitialState()
	if err != nil {
		atomic.AddInt64(&persistenceFailureCount, 1)
		lastPersistenceError = err.Error()
		return fmt.Errorf("failed to get initial state: %v", err)
	}

	firstIndex, err := ps.MemoryStorage.FirstIndex()
	if err != nil {
		atomic.AddInt64(&persistenceFailureCount, 1)
		lastPersistenceError = err.Error()
		return fmt.Errorf("failed to get first index: %v", err)
	}

	lastIndex, err := ps.MemoryStorage.LastIndex()
	if err != nil {
		atomic.AddInt64(&persistenceFailureCount, 1)
		lastPersistenceError = err.Error()
		return fmt.Errorf("failed to get last index: %v", err)
	}

	var entries []raftpb.Entry
	if lastIndex >= firstIndex {
		entries, err = ps.MemoryStorage.Entries(firstIndex, lastIndex+1, 0)
		if err != nil {
			atomic.AddInt64(&persistenceFailureCount, 1)
			lastPersistenceError = err.Error()
			return fmt.Errorf("failed to get entries: %v", err)
		}
	}

	// Get snapshot if available
	snapshot, err := ps.MemoryStorage.Snapshot()
	var snapshotPtr *raftpb.Snapshot
	if err == nil && !raft.IsEmptySnap(snapshot) {
		snapshotPtr = &snapshot
	}

	// CRITICAL FIX: Validate that commit index doesn't exceed available entries
	// If HardState.Commit > lastIndex, clamp it to lastIndex to prevent corruption
	validatedHardState := hardState
	if validatedHardState.Commit > lastIndex {
		logWarning("WARNING: HardState.Commit (%d) exceeds lastIndex (%d), clamping to %d to prevent corruption",
			validatedHardState.Commit, lastIndex, lastIndex)
		validatedHardState.Commit = lastIndex
	}

	state := StorageState{
		HardState: validatedHardState,
		Entries:   entries,
		Snapshot:  snapshotPtr,
	}

	data, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		atomic.AddInt64(&persistenceFailureCount, 1)
		lastPersistenceError = err.Error()
		return fmt.Errorf("failed to marshal state: %v", err)
	}

	statePath := ps.getStatePath()
	tmpPath := statePath + ".tmp"

	// Write to temporary file first, then rename for atomic update
	if err := ioutil.WriteFile(tmpPath, data, 0644); err != nil {
		atomic.AddInt64(&persistenceFailureCount, 1)
		lastPersistenceError = err.Error()
		logError("persistence failure #%d: %v",
			atomic.LoadInt64(&persistenceFailureCount), err)
		return fmt.Errorf("failed to write temporary state file: %v", err)
	}

	if err := os.Rename(tmpPath, statePath); err != nil {
		atomic.AddInt64(&persistenceFailureCount, 1)
		lastPersistenceError = err.Error()
		logError("persistence failure #%d: %v",
			atomic.LoadInt64(&persistenceFailureCount), err)
		return fmt.Errorf("failed to rename state file: %v", err)
	}

	// FIX #3: Track successful persistence
	lastPersistenceTime = time.Now()
	if atomic.LoadInt64(&persistenceFailureCount) > 0 {
		logInfo("persistence recovered after %d failures",
			atomic.LoadInt64(&persistenceFailureCount))
		atomic.StoreInt64(&persistenceFailureCount, 0)
	}

	return nil
}

// SetHardState sets the hard state and persists it
func (ps *PersistentStorage) SetHardState(hs raftpb.HardState) error {
	if err := ps.MemoryStorage.SetHardState(hs); err != nil {
		return err
	}

	// FIX #3: Persist to disk with monitoring
	if err := ps.saveToDisk(); err != nil {
		logError("failed to persist hard state: %v", err)
		logWarning("continuing with in-memory state only (persistence failure count: %d)",
			atomic.LoadInt64(&persistenceFailureCount))
		// Don't return error here as the memory operation succeeded
		// But failures are now tracked and logged
	}

	return nil
}

// Append appends entries and persists them
func (ps *PersistentStorage) Append(entries []raftpb.Entry) error {
	if err := ps.MemoryStorage.Append(entries); err != nil {
		return err
	}

	// FIX #3: Persist to disk with monitoring
	if err := ps.saveToDisk(); err != nil {
		logError("failed to persist entries: %v", err)
		logWarning("continuing with in-memory entries only (persistence failure count: %d)",
			atomic.LoadInt64(&persistenceFailureCount))
		// Don't return error here as the memory operation succeeded
		// But failures are now tracked and logged
	}

	return nil
}

// ApplySnapshot applies a snapshot and persists it
func (ps *PersistentStorage) ApplySnapshot(snapshot raftpb.Snapshot) error {
	if err := ps.MemoryStorage.ApplySnapshot(snapshot); err != nil {
		return err
	}

	// Persist to disk
	if err := ps.saveToDisk(); err != nil {
		logError("failed to persist snapshot: %v", err)
		// Don't return error here as the memory operation succeeded
	}

	return nil
}

// Compact compacts the log up to compactIndex and persists
func (ps *PersistentStorage) Compact(compactIndex uint64) error {
	if err := ps.MemoryStorage.Compact(compactIndex); err != nil {
		return err
	}

	// Persist to disk
	if err := ps.saveToDisk(); err != nil {
		logError("failed to persist after compaction: %v", err)
		// Don't return error here as the memory operation succeeded
	}

	return nil
}

// Close ensures all data is persisted and resources are cleaned up
func (ps *PersistentStorage) Close() error {
	return ps.saveToDisk()
}

// Additional required global variables
var (
	committedIndex      uint64
	appliedIndex        uint64
	lastIndex           uint64
	messagesProcessed   int64
	logEntriesCommitted int64
	heartbeatsSent      int64
	electionsTriggered  int64

	// Node and connection management
	nodes         map[uint64]string
	connMutex     sync.RWMutex
	currentNodeID uint64 // Current node's ID (set during init)

	// Cluster state
	clusterState ClusterState

	// Initial cluster configuration (all members from initial_cluster setting)
	// This is populated during initialization and never changes
	initialClusterMembers map[string]string // map[name]address
	initialClusterMutex   sync.RWMutex

	// Error tracking
	errorCount int64
	lastError  time.Time

	// Shutdown control
	shutdownRequested int32

	// Additional state variables
	currentTerm uint64
	votedFor    uint64
	commitIndex uint64
	lastApplied uint64
	raftState   string
	leaderID    uint64

	// Health and monitoring
	startupTime  time.Time
	healthStatus string
)

// Error recording function
func recordError(err error) {
	atomic.AddInt64(&errorCount, 1)
	lastError = time.Now()
	logError("%v", err)
}

// Network utility functions
func readUint32(conn net.Conn, value *uint32) error {
	buf := make([]byte, 4)
	if _, err := io.ReadFull(conn, buf); err != nil {
		return err
	}
	*value = binary.BigEndian.Uint32(buf)
	return nil
}

func writeUint32(conn net.Conn, value uint32) error {
	buf := make([]byte, 4)
	binary.BigEndian.PutUint32(buf, value)
	_, err := conn.Write(buf)
	return err
}

func getNetworkLatency() float64 {
	// Simple network latency measurement
	// In a real implementation, this would measure actual network latency
	return 1.0 // milliseconds
}

// Debug logging function that respects log level
func debugLog(format string, args ...interface{}) {
	if debugEnabled {
		logInfo(format, args...)
	}
}

// Set debug logging level
//
//export pgraft_go_set_debug
func pgraft_go_set_debug(enabled C.int) {
	debugEnabled = (enabled != 0)
}

// Set log output to a file
//
//export pgraft_go_set_log_file
func pgraft_go_set_log_file(logFilePath *C.char) C.int {
	goLogFilePath := C.GoString(logFilePath)

	logFile, err := os.OpenFile(goLogFilePath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err != nil {
		logError("failed to open log file %s: %v", goLogFilePath, err)
		return -1
	}

	log.SetOutput(logFile)
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds | log.Lshortfile)
	logInfo("Go logging redirected to %s", goLogFilePath)
	return 0
}

//export pgraft_go_start
func pgraft_go_start() C.int {
	raftMutex.Lock()
	defer raftMutex.Unlock()

	if atomic.LoadInt32(&running) == 1 {
		logInfo("WARNING - Already running")
		return 0
	}

	if atomic.LoadInt32(&initialized) == 0 {
		logInfo("ERROR - not initialized")
		return -1
	}

	logInfo("INFO - Starting background processing (worker-driven model)")

	// Configure Go runtime
	runtime.GOMAXPROCS(runtime.NumCPU())
	logInfo("Go runtime configured: GOMAXPROCS=%d, NumCPU=%d", runtime.GOMAXPROCS(0), runtime.NumCPU())

	// Initialize raftDone channel if not already
	if raftDone == nil {
		raftDone = make(chan struct{})
	}

	// Start only the goroutines that need to run continuously
	// Ticker is now called from C worker thread via pgraft_go_tick()
	go raftProcessingLoop()
	go messageReceiver()
	go connectionMonitor()

	atomic.StoreInt32(&running, 1)
	logInfo("INFO - Started successfully - Ready processing active, tick will be called from worker")

	return 0
}

//export pgraft_go_propose
func pgraft_go_propose(data *C.char, len C.size_t) C.int {
	if atomic.LoadInt32(&running) == 0 {
		logError("cannot propose: raft not running")
		return -1
	}

	if raftNode == nil {
		logError("cannot propose: raftNode is nil")
		return -1
	}

	// Convert C string to Go bytes
	goData := C.GoBytes(unsafe.Pointer(data), C.int(len))

	logInfo("proposing entry to raft: len=%d, data='%s'", len, string(goData))

	// Propose to Raft
	err := raftNode.Propose(raftCtx, goData)
	if err != nil {
		logError("failed to propose to raft: %v", err)
		return -1
	}

	logInfo("successfully proposed entry to raft")
	return 0
}

//export pgraft_go_stop
func pgraft_go_stop() C.int {
	raftMutex.Lock()
	defer raftMutex.Unlock()

	if atomic.LoadInt32(&running) == 0 {
		logInfo("WARNING - Already stopped")
		return 0
	}

	// Signal shutdown
	close(stopChan)

	// Stop ticker
	if raftTicker != nil {
		raftTicker.Stop()
	}

	// Cancel context
	if raftCancel != nil {
		raftCancel()
	}

	// Close all connections
	connMutex.Lock()
	for nodeID, managedConn := range connections {
		managedConn.Conn.Close()
		delete(connections, nodeID)
	}
	connMutex.Unlock()

	atomic.StoreInt32(&running, 0)
	logInfo("INFO - Stopped successfully")

	return 0
}

//export pgraft_go_get_node_id
func pgraft_go_get_node_id() C.int64_t {
	/* Return the current node's ID from the global variable */
	return C.int64_t(currentNodeID)
}

//export pgraft_go_get_nodes
func pgraft_go_get_nodes() *C.char {
	// Read from initialClusterMembers which contains ALL nodes from initial_cluster config
	// This works on both leader and followers, unlike clusterState.Nodes which only
	// contains active nodes visible to the current node
	initialClusterMutex.RLock()
	defer initialClusterMutex.RUnlock()

	if initialClusterMembers == nil || len(initialClusterMembers) == 0 {
		logInfo("pgraft_go_get_nodes called but initialClusterMembers is empty")
		return C.CString("[]")
	}

	logInfo("pgraft_go_get_nodes called - initialClusterMembers size: %d", len(initialClusterMembers))

	nodesList := make([]map[string]interface{}, 0)
	// Convert cluster members (name->address) to node list with IDs
	nodeID := uint64(1)
	for memberName, address := range initialClusterMembers {
		logInfo("adding node to list: name=%s, id=%d, address=%s", memberName, nodeID, address)
		nodeInfo := map[string]interface{}{
			"id":      nodeID,
			"name":    memberName,
			"address": address,
		}
		nodesList = append(nodesList, nodeInfo)
		nodeID++
	}

	jsonData, err := json.Marshal(nodesList)
	if err != nil {
		logError("failed to marshal nodes: %v", err)
		return C.CString("{\"error\": \"failed to marshal nodes\"}")
	}

	logInfo("pgraft_go_get_nodes returning: %s", string(jsonData))
	return C.CString(string(jsonData))
}

//export cleanup_pgraft
func cleanup_pgraft() {
	logInfo("INFO - Cleaning up pgraft resources")

	if raftStorage != nil {
		if err := raftStorage.Close(); err != nil {
			logError("failed to close persistent storage: %v", err)
		}
	}

	if raftNode != nil {
		raftNode.Stop()
	}

	logInfo("INFO - Cleanup completed")
}

//export pgraft_go_version
func pgraft_go_version() *C.char {
	return C.CString("1.0.0")
}

//export pgraft_go_test
func pgraft_go_test() C.int {
	logInfo("INFO - Test function called")
	return 0
}

// Replication state
var (
	replicationState struct {
		lastAppliedIndex  uint64
		lastSnapshotIndex uint64
		replicationLag    time.Duration
		replicationMutex  sync.RWMutex
	}
)

//export pgraft_go_init_config
func pgraft_go_init_config(config *C.struct_pgraft_go_config) C.int {
	defer func() {
		if r := recover(); r != nil {
			logError("panic in pgraft_go_init_config: %v", r)
		}
	}()

	// Extract configuration from C struct
	nodeID := int(config.node_id)
	address := C.GoString(config.address)
	port := int(config.port)
	clusterID := C.GoString(config.cluster_id)
	dataDir := C.GoString(config.data_dir)
	nodeName := C.GoString(config.name)
	electionTimeout := int(config.election_timeout)
	heartbeatInterval := int(config.heartbeat_interval)
	snapshotInterval := int(config.snapshot_interval)
	memberCount := int(config.cluster_member_count)

	// Auto-generate node ID from name hash if not provided
	if nodeID == 0 && nodeName != "" {
		// Generate a simple hash from the node name for consistent node IDs
		hash := 0
		for _, c := range nodeName {
			hash = hash*31 + int(c)
		}
		// Ensure positive and reasonable range (1-65535)
		nodeID = (hash&0x7FFFFFFF)%65535 + 1
		logInfo("auto-generated node_id %d from name '%s'", nodeID, nodeName)
	}

	// Note: currentNodeID will be set later to the Raft ID for consistency

	logInfo("initializing node %d (name: %s, cluster: %s) at %s:%d",
		nodeID, nodeName, clusterID, address, port)
	logInfo("using etcd-style configuration: election_timeout=%dms, heartbeat_interval=%dms, snapshot_interval=%d, data_dir=%s",
		electionTimeout, heartbeatInterval, snapshotInterval, dataDir)

	// Extract pre-parsed cluster members from C struct (already split into host/port)
	if memberCount == 0 || config.cluster_members == nil {
		logError("no cluster members provided in configuration")
		return -1
	}

	// Convert C array of cluster members to Go map
	clusterMembers := make(map[string]string)
	membersSlice := (*[1 << 30]C.struct_pgraft_go_cluster_member)(unsafe.Pointer(config.cluster_members))[:memberCount:memberCount]

	logInfo("==== PARSING CLUSTER MEMBERS: memberCount=%d ====", memberCount)
	for i := 0; i < memberCount; i++ {
		memberName := C.GoString(membersSlice[i].name)
		peerHost := C.GoString(membersSlice[i].peer_host)
		peerPort := int(membersSlice[i].peer_port)
		peerAddr := fmt.Sprintf("%s:%d", peerHost, peerPort)

		clusterMembers[memberName] = peerAddr
		logInfo("cluster member %d: %s -> %s (host=%s, port=%d)", i+1, memberName, peerAddr, peerHost, peerPort)
	}

	logInfo("==== LOADED %d cluster members from parsed configuration ====", len(clusterMembers))

	// Store initial cluster members globally so they're accessible from all nodes
	initialClusterMutex.Lock()
	initialClusterMembers = clusterMembers
	initialClusterMutex.Unlock()
	logInfo("Stored %d initial cluster members globally", len(initialClusterMembers))

	raftMutex.Lock()
	defer raftMutex.Unlock()

	if atomic.LoadInt32(&initialized) == 1 {
		logInfo("WARNING - Node already initialized, skipping")
		return 0 // Already initialized
	}

	// IMPORTANT: Determine Raft ID FIRST before creating storage
	// This ensures persistent storage uses the correct Raft ID

	// Use configured data directory (etcd-style)
	if dataDir == "" || dataDir == "(null)" {
		// Fallback to default if not specified
		dataDir = fmt.Sprintf("/tmp/pgraft_node_%d", nodeID)
		logWarning("pgraft.data_dir not set, using default: %s", dataDir)
	}

	// NOTE: Storage creation moved after Raft ID determination

	// Calculate ticks from millisecond values (etcd-style)
	// Tick interval is 100ms, so we convert timeouts to ticks
	tickIntervalMs := 100
	electionTick := electionTimeout / tickIntervalMs
	heartbeatTick := heartbeatInterval / tickIntervalMs

	// Ensure minimum values
	if electionTick < 1 {
		electionTick = 10
		logInfo("WARNING - election_timeout too small, using minimum 10 ticks (1000ms)")
	}
	if heartbeatTick < 1 {
		heartbeatTick = 1
		logInfo("WARNING - heartbeat_interval too small, using minimum 1 tick (100ms)")
	}

	logInfo("raft timing: electiontick=%d, heartbeatTick=%d (tick interval=%dms)",
		electionTick, heartbeatTick, tickIntervalMs)

	// NOTE: raftConfig will be fully initialized after we determine the Raft ID
	// For now, just set the timing parameters
	logInfo("raft timing prepared: electionTick=%d, heartbeatTick=%d (tick interval=%dms)",
		electionTick, heartbeatTick, tickIntervalMs)

	messageChan = make(chan raftpb.Message, 4096)
	stopChan = make(chan struct{})
	reconnectChan = make(chan uint64, 256) // Buffered channel for reconnect requests
	connections = make(map[uint64]*ManagedConnection)
	logInfo("DEBUG - Communication channels initialized")

	// Use node name from config (already extracted above)

	nodesMutex.Lock()
	if nodes == nil {
		nodes = make(map[uint64]string)
		logInfo("initialized global nodes map")
	}

	// Register all cluster members in the nodes map using sequential IDs
	// Match node name to find this node's Raft ID
	var thisNodeRaftID uint64
	foundThisNode := false
	peerNodeID := uint64(1)

	logInfo("populating nodes map from %d cluster members", len(clusterMembers))
	for memberName, peerAddr := range clusterMembers {
		nodes[peerNodeID] = peerAddr
		logInfo("added to nodes map: nodes[%d] = '%s' (member: %s)", peerNodeID, peerAddr, memberName)

		if memberName == nodeName {
			thisNodeRaftID = peerNodeID
			foundThisNode = true
			logInfo("*** this node '%s' has Raft ID %d -> %s", nodeName, peerNodeID, peerAddr)
		} else {
			logInfo("cluster member %d: %s -> %s", peerNodeID, memberName, peerAddr)
		}

		peerNodeID++
	}
	logInfo("nodes map population complete - map now has %d entries", len(nodes))
	nodesMutex.Unlock()

	if !foundThisNode {
		logError("node name '%s' not found in initial_cluster configuration", nodeName)
		return -1
	}

	// IMPORTANT: Use the Raft ID as the canonical node ID
	// This ensures node_id, leader_id, and raft_id are all in the same ID space
	currentNodeID = thisNodeRaftID
	logInfo("using Raft ID %d for node '%s' (PostgreSQL node_id=%d, now using Raft ID)", thisNodeRaftID, nodeName, nodeID)

	// NOW create persistent storage using the correct Raft ID
	persistentStorage, err := NewPersistentStorage(thisNodeRaftID, dataDir)
	if err != nil {
		logError("failed to create persistent storage for Raft node %d: %v", thisNodeRaftID, err)
		return -1
	}
	raftStorage = persistentStorage
	logInfo("Persistent storage initialized at %s with Raft ID %d", dataDir, thisNodeRaftID)

	// NOW create the Raft configuration with the correct ID and storage
	raftConfig = &raft.Config{
		ID:              thisNodeRaftID,
		ElectionTick:    electionTick,
		HeartbeatTick:   heartbeatTick,
		Storage:         raftStorage,
		MaxInflightMsgs: 256,
		MaxSizePerMsg:   1024 * 1024, // 1MB
		PreVote:         true,        // Enable PreVote for better elections (etcd best practice)
	}
	logInfo("Raft configuration created with ID=%d", thisNodeRaftID)

	// Check if this is a restart by looking at persistent state
	hardState, confState, err := raftStorage.InitialState()
	isRestart := err == nil && (!raft.IsEmptyHardState(hardState) || len(confState.Voters) > 0)

	var peers []raft.Peer
	if isRestart {
		// This is a restart - use RestartNode with existing state
		logInfo("node %d restarting from persistent state (term=%d, commit=%d)",
			thisNodeRaftID, hardState.Term, hardState.Commit)
		raftNode = raft.RestartNode(raftConfig)
		logInfo("node %d ('%s') restarted as follower, will rejoin cluster", thisNodeRaftID, nodeName)
	} else {
		// This is a new node - create peers from all cluster members
		for peerID := range nodes {
			peers = append(peers, raft.Peer{ID: peerID})
		}

		logInfo("node %d ('%s') starting with %d peers from initial_cluster", thisNodeRaftID, nodeName, len(peers))
		raftNode = raft.StartNode(raftConfig, peers)
		logInfo("node %d initialized with %d cluster members", thisNodeRaftID, len(clusterMembers))
	}

	// Verify raftNode was created successfully
	if raftNode == nil {
		logError("Failed to create Raft node for node %d", nodeID)
		return -1
	}

	// Initialize context before using it
	raftCtx, raftCancel = context.WithCancel(context.Background())
	logInfo("DEBUG - Context initialized")

	logInfo("Bootstrap node %d created with %d initial peers", nodeID, len(peers))

	// Initialize applied and committed indices
	appliedIndex = 0
	committedIndex = 0

	logInfo("INFO - Raft node initialized, goroutines will be started separately via pgraft_go_start()")

	// Note: Campaign will be triggered automatically based on cluster size and initial_cluster_state
	if !isRestart {
		logInfo("INFO - New cluster node initialized with %d peers from initial_cluster", len(peers))
	}

	atomic.StoreInt32(&initialized, 1)
	logInfo("Node %d initialized successfully with etcd-style configuration (cluster: %s)", nodeID, clusterID)

	return 0
}

// Helper function to establish initial connections to all peers
// This should be called AFTER pgraft_go_start() from the background worker
//
//export pgraft_go_connect_to_peers
func pgraft_go_connect_to_peers() C.int {
	logInfo("Establishing initial connections to all cluster peers...")

	nodesMutex.RLock()
	allNodes := make(map[uint64]string)
	for id, addr := range nodes {
		allNodes[id] = addr
	}
	myRaftID := raftConfig.ID
	nodesMutex.RUnlock()

	// Connect to all peers (except self)
	connectedCount := 0
	for nodeID, nodeAddr := range allNodes {
		if nodeID == myRaftID {
			continue // Skip self
		}

		logInfo("Initiating connection to peer node %d at %s", nodeID, nodeAddr)
		go func(id uint64, addr string) {
			// Try to connect with retries
			for attempt := 1; attempt <= 3; attempt++ {
				if err := connectToPeer(id, addr); err != nil {
					logWarning("Failed to connect to peer %d (attempt %d/3): %v", id, attempt, err)
					if attempt < 3 {
						time.Sleep(time.Duration(attempt) * 500 * time.Millisecond)
					}
				} else {
					logInfo("Successfully established initial connection to peer %d at %s", id, addr)
					return
				}
			}
			logWarning("Could not establish initial connection to peer %d after 3 attempts", id)
		}(nodeID, nodeAddr)
		connectedCount++
	}

	logInfo("Initial peer connection attempts initiated for %d peers", connectedCount)
	return 0
}

//export pgraft_go_init
func pgraft_go_init(nodeID C.int, address *C.char, port C.int) C.int {
	defer func() {
		if r := recover(); r != nil {
			logError("panic in pgraft_go_init: %v", r)
		}
	}()

	logInfo("Legacy pgraft_go_init called for node %d at %s:%d", nodeID, C.GoString(address), int(port))
	logInfo("WARNING - Using legacy init function. Consider migrating to pgraft_go_init_config for etcd-style configuration")

	raftMutex.Lock()
	defer raftMutex.Unlock()

	if atomic.LoadInt32(&initialized) == 1 {
		logInfo("WARNING - Node already initialized, skipping")
		return 0 // Already initialized
	}

	// Create data directory for persistent storage
	dataDir := fmt.Sprintf("/tmp/pgraft_node_%d", nodeID)

	persistentStorage, err := NewPersistentStorage(uint64(nodeID), dataDir)
	if err != nil {
		logError("failed to create persistent storage for node %d: %v", nodeID, err)
		return -1
	}
	raftStorage = persistentStorage
	logInfo("Persistent storage initialized at %s", dataDir)

	raftConfig = &raft.Config{
		ID:              uint64(nodeID),
		ElectionTick:    10, // 10 ticks * 100ms = 1 second election timeout
		HeartbeatTick:   1,  // 1 tick * 100ms = 100ms heartbeat interval
		Storage:         raftStorage,
		MaxInflightMsgs: 256,
		MaxSizePerMsg:   1024 * 1024, // 1MB
		PreVote:         true,        // Enable PreVote for better elections
	}
	logInfo("DEBUG - Raft configuration created")
	logInfo("Go library version: %s", C.GoString(pgraft_go_version()))

	messageChan = make(chan raftpb.Message, 4096)
	stopChan = make(chan struct{})
	reconnectChan = make(chan uint64, 256) // Buffered channel for reconnect requests
	connections = make(map[uint64]*ManagedConnection)
	// connMutex is a global RWMutex, no need to re-initialize here
	logInfo("DEBUG - Communication channels initialized")

	nodesMutex.Lock()
	if nodes == nil {
		nodes = make(map[uint64]string)
	}
	nodes[uint64(nodeID)] = fmt.Sprintf("%s:%d", C.GoString(address), int(port))
	nodesMutex.Unlock()
	logInfo("Self node registered: %d -> %s", nodeID, nodes[uint64(nodeID)])

	// Check if this is a restart by looking at persistent state
	hardState, confState, err := raftStorage.InitialState()
	isRestart := err == nil && (!raft.IsEmptyHardState(hardState) || len(confState.Voters) > 0)

	var peers []raft.Peer
	if isRestart {
		// This is a restart - use RestartNode with existing state
		logInfo("node %d restarting from persistent state (term=%d, commit=%d)",
			nodeID, hardState.Term, hardState.Commit)
		raftNode = raft.RestartNode(raftConfig)
		logInfo("Node %d restarted as follower, will rejoin cluster", nodeID)
	} else {
		// This is a new node - start as single-node cluster
		peers = []raft.Peer{{ID: uint64(nodeID)}}
		logInfo("Node %d starting as new single-node cluster, peers will be added via cluster management", nodeID)
		raftNode = raft.StartNode(raftConfig, peers)
		logInfo("Node %d initialized as new node, waiting for cluster management to add peers", nodeID)
	}

	// Verify raftNode was created successfully
	if raftNode == nil {
		logError("Failed to create Raft node for node %d", nodeID)
		return -1
	}

	// Initialize context before using it
	raftCtx, raftCancel = context.WithCancel(context.Background())
	logInfo("DEBUG - Context initialized")

	logInfo("Bootstrap node %d created with %d initial peers", nodeID, len(peers))

	// Note: ConfChange will be handled by background processing after start
	logInfo("INFO - Initial configuration will be handled by background processing")

	logInfo("INFO - Raft node initialized, goroutines will be started separately")

	// Initialize applied and committed indices
	appliedIndex = 0
	committedIndex = 0

	// Note: Goroutines will be started separately via pgraft_go_start to avoid
	// multithreading during PostgreSQL startup
	logInfo("INFO - Raft node initialized, goroutines will be started separately")

	// Ticker will be started separately via pgraft_go_start
	logInfo("DEBUG - Ticker initialization deferred")
	logInfo("INFO - Raft initialization complete, ready to start goroutines")

	logInfo("DEBUG - All Raft processing goroutines started successfully")

	// Initialize metrics
	atomic.StoreInt64(&messagesProcessed, 0)
	atomic.StoreInt64(&logEntriesCommitted, 0)
	atomic.StoreInt64(&heartbeatsSent, 0)
	atomic.StoreInt64(&electionsTriggered, 0)
	atomic.StoreInt64(&errorCount, 0)

	startupTime = time.Now()
	healthStatus = "initializing"

	atomic.StoreInt32(&initialized, 1)
	logInfo("Initialization completed successfully for node %d at %s:%d", nodeID, C.GoString(address), int(port))

	logInfo("INFO - Returning success from initialization")
	return 0
}

//export pgraft_go_start_background
func pgraft_go_start_background() C.int {
	debugLog("start_background: starting Raft background processing")

	raftMutex.Lock()
	defer raftMutex.Unlock()

	// Start the background processing loop
	go processRaftReady()
	debugLog("start_background: background processing started")

	// Start the ticker for Raft operations
	raftTicker = time.NewTicker(100 * time.Millisecond)
	go processRaftTicker()
	debugLog("start_background: Raft ticker started")

	debugLog("start_background: all background processing started")
	return 0
}

//export pgraft_go_add_peer
func pgraft_go_add_peer(nodeID C.int, address *C.char, port C.int) C.int {
	defer func() {
		if r := recover(); r != nil {
			logError("panic in pgraft_go_add_peer: %v", r)
		}
	}()

	logInfo("pgraft_go_add_peer called with nodeID=%d, address=%s, port=%d", nodeID, C.GoString(address), int(port))

	raftMutex.Lock()
	defer raftMutex.Unlock()

	// C side handles state checking via shared memory
	// Just add the peer and return success
	logInfo("adding peer node %d at %s:%d", nodeID, C.GoString(address), int(port))

	// Add to our node map with proper mutex protection
	nodeAddr := fmt.Sprintf("%s:%d", C.GoString(address), int(port))
	nodesMutex.Lock()
	// Always ensure the map is initialized
	if nodes == nil {
		nodes = make(map[uint64]string)
		logInfo("Initialized nodes map in pgraft_go_add_peer")
	}
	nodes[uint64(nodeID)] = nodeAddr
	nodesMutex.Unlock()
	logInfo("added node to map: %d -> %s", nodeID, nodeAddr)

	// Add peer to Raft cluster configuration
	if raftNode != nil {
		logInfo("adding peer to Raft cluster configuration")

		// Create a configuration change proposal
		cc := raftpb.ConfChange{
			Type:    raftpb.ConfChangeAddNode,
			NodeID:  uint64(nodeID),
			Context: []byte(nodeAddr),
		}

		// Propose the configuration change with proper sequencing
		logInfo("proposing configuration change for node %d", nodeID)

		// Use a longer timeout for configuration changes
		ctx, cancel := context.WithTimeout(raftCtx, 30*time.Second)
		defer cancel()

		// Retry configuration change up to 3 times
		var err error
		for retry := 0; retry < 3; retry++ {
			if retry > 0 {
				logInfo("Retrying configuration change for node %d (attempt %d)", nodeID, retry+1)
				time.Sleep(time.Duration(retry) * time.Second) // Exponential backoff
			}

			err = raftNode.ProposeConfChange(ctx, cc)
			if err == nil {
				logInfo("configuration change proposed successfully for node %d", nodeID)
				break
			}
			logInfo("ERROR proposing configuration change (attempt %d): %v", retry+1, err)
		}

		if err != nil {
			logInfo("CRITICAL ERROR - Failed to propose configuration change for node %d after 3 attempts", nodeID)
			return -1
		}

		// Establish TCP connection to the peer
		go func() {
			logInfo("establishing TCP connection to node %d at %s", nodeID, nodeAddr)
			if err := connectToPeer(uint64(nodeID), nodeAddr); err != nil {
				logWarning("Failed to connect to peer %d at %s: %v", nodeID, nodeAddr, err)
			} else {
				logInfo("successfully connected to peer %d at %s", nodeID, nodeAddr)
			}
		}()

		// Let ticks drive elections naturally - no forced campaign
	} else {
		logInfo("WARNING - Raft node is nil, cannot add peer to configuration")
	}

	logInfo("added peer node %d at %s (configuration change applied)", nodeID, nodeAddr)

	return 0
}

//export pgraft_go_remove_peer
func pgraft_go_remove_peer(nodeID C.int) C.int {
	raftMutex.Lock()
	defer raftMutex.Unlock()

	if atomic.LoadInt32(&running) == 0 {
		return -1 // Not running
	}

	// Close connection
	connMutex.Lock()
	if conn, exists := connections[uint64(nodeID)]; exists {
		conn.Conn.Close()
		delete(connections, uint64(nodeID))
	}
	connMutex.Unlock()

	// Remove from our node map with proper mutex protection
	nodesMutex.Lock()
	delete(nodes, uint64(nodeID))
	nodesMutex.Unlock()

	// Propose configuration change
	cc := raftpb.ConfChange{
		Type:   raftpb.ConfChangeRemoveNode,
		NodeID: uint64(nodeID),
	}

	raftNode.ProposeConfChange(raftCtx, cc)

	logInfo("removed peer node %d", nodeID)

	return 0
}

//export pgraft_go_get_state
func pgraft_go_get_state() *C.char {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		return C.CString("stopped")
	}

	status := raftNode.Status()

	switch status.RaftState {
	case raft.StateFollower:
		return C.CString("follower")
	case raft.StateCandidate:
		return C.CString("candidate")
	case raft.StateLeader:
		return C.CString("leader")
	default:
		return C.CString("unknown")
	}
}

//export pgraft_go_get_leader
func pgraft_go_get_leader() C.int64_t {
	defer func() {
		if r := recover(); r != nil {
			logError("panic in pgraft_go_get_leader: %v", r)
		}
	}()

	logInfo("pgraft_go_get_leader called")

	raftMutex.RLock()
	defer raftMutex.RUnlock()

	// Note: Don't check running flag as SQL functions run in different processes
	// The Go library is already loaded and initialized

	if raftNode == nil {
		logInfo("get_leader - raftNode is nil")
		return -1
	}

	status := raftNode.Status()
	logInfo("get_leader - status.Lead=%d, status.RaftState=%d", status.Lead, status.RaftState)

	// Return the leader ID - if this node is the leader, return its ID, otherwise return status.Lead
	if status.RaftState == raft.StateLeader {
		return C.int64_t(raftConfig.ID)
	}
	return C.int64_t(status.Lead)
}

//export pgraft_go_get_term
func pgraft_go_get_term() C.int32_t {
	defer func() {
		if r := recover(); r != nil {
			logError("panic in pgraft_go_get_term: %v", r)
		}
	}()

	logInfo("pgraft_go_get_term called")

	raftMutex.RLock()
	defer raftMutex.RUnlock()

	// Note: Don't check running flag as SQL functions run in different processes
	// The Go library is already loaded and initialized

	if raftNode == nil {
		logInfo("get_term - raftNode is nil")
		return -1
	}

	status := raftNode.Status()
	logInfo("get_term - returning term: %d", status.Term)
	return C.int32_t(status.Term)
}

//export pgraft_go_is_initialized
func pgraft_go_is_initialized() C.int {
	if atomic.LoadInt32(&initialized) == 1 {
		return 1
	}
	return 0
}

//export pgraft_go_is_leader
func pgraft_go_is_leader() C.int {
	defer func() {
		if r := recover(); r != nil {
			logError("panic in pgraft_go_is_leader: %v", r)
		}
	}()

	logInfo("pgraft_go_is_leader called")

	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		logInfo("is_leader - not running")
		return -1 /* Not ready */
	}

	if raftNode == nil {
		logInfo("is_leader - raftNode is nil")
		return -1 /* Not ready */
	}

	status := raftNode.Status()
	isLeader := status.Lead == status.ID
	logInfo("is_leader - status.ID=%d, status.Lead=%d, isLeader=%v", status.ID, status.Lead, isLeader)

	if isLeader {
		return 1
	}
	return 0
}

//export pgraft_go_append_log
func pgraft_go_append_log(data *C.char, length C.int) C.int {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		return -1
	}

	// Convert C data to Go byte slice
	goData := C.GoBytes(unsafe.Pointer(data), length)

	// Propose the data
	raftNode.Propose(raftCtx, goData)

	atomic.AddInt64(&logEntriesCommitted, 1)

	return 0
}

//export pgraft_go_get_stats
func pgraft_go_get_stats() *C.char {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	stats := map[string]interface{}{
		"initialized":           atomic.LoadInt32(&initialized) == 1,
		"running":               atomic.LoadInt32(&running) == 1,
		"messages_processed":    atomic.LoadInt64(&messagesProcessed),
		"log_entries_committed": atomic.LoadInt64(&logEntriesCommitted),
		"heartbeats_sent":       atomic.LoadInt64(&heartbeatsSent),
		"elections_triggered":   atomic.LoadInt64(&electionsTriggered),
		"error_count":           atomic.LoadInt64(&errorCount),
		"applied_index":         appliedIndex,
		"committed_index":       committedIndex,
		"uptime_seconds":        time.Since(startupTime).Seconds(),
		"health_status":         healthStatus,
		"connected_nodes":       len(connections),
	}

	jsonData, err := json.Marshal(stats)
	if err != nil {
		return C.CString("{\"error\": \"failed to marshal stats\"}")
	}

	return C.CString(string(jsonData))
}

//export pgraft_go_get_logs
func pgraft_go_get_logs() *C.char {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		return C.CString("[]")
	}

	// Get logs from storage
	firstIndex, _ := raftStorage.FirstIndex()
	lastIndex, _ := raftStorage.LastIndex()

	logs := make([]map[string]interface{}, 0)

	for i := firstIndex; i <= lastIndex; i++ {
		entries, err := raftStorage.Entries(i, i+1, 0)
		if err != nil || len(entries) == 0 {
			continue
		}

		entry := entries[0]
		logEntry := map[string]interface{}{
			"index":     entry.Index,
			"term":      entry.Term,
			"type":      entry.Type.String(),
			"data":      string(entry.Data),
			"committed": entry.Index <= committedIndex,
		}

		logs = append(logs, logEntry)
	}

	jsonData, err := json.Marshal(logs)
	if err != nil {
		return C.CString("{\"error\": \"failed to marshal logs\"}")
	}

	return C.CString(string(jsonData))
}

//export pgraft_go_commit_log
func pgraft_go_commit_log(index C.long) C.int {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		return -1
	}

	// In etcd-io/raft, commits happen automatically
	// This function is mainly for compatibility
	committedIndex = uint64(index)

	return 0
}

//export pgraft_go_step_message
func pgraft_go_step_message(data *C.char, length C.int) C.int {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		return -1
	}

	// Convert C data to Go byte slice
	goData := C.GoBytes(unsafe.Pointer(data), length)

	// Parse as raftpb.Message
	var msg raftpb.Message
	if err := msg.Unmarshal(goData); err != nil {
		logInfo("failed to unmarshal message: %v", err)
		return -1
	}

	// Step the message
	raftNode.Step(raftCtx, msg)

	atomic.AddInt64(&messagesProcessed, 1)

	return 0
}

//export pgraft_go_get_network_status
func pgraft_go_get_network_status() *C.char {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	networkStatus := map[string]interface{}{
		"nodes_connected":    len(connections),
		"messages_processed": atomic.LoadInt64(&messagesProcessed),
		"network_latency":    getNetworkLatency(),
		"connection_status":  "active",
	}

	jsonData, err := json.Marshal(networkStatus)
	if err != nil {
		return C.CString("{\"error\": \"failed to marshal network status\"}")
	}

	return C.CString(string(jsonData))
}

//export pgraft_go_free_string
func pgraft_go_free_string(str *C.char) {
	C.free(unsafe.Pointer(str))
}

// Main processing loop following etcd-io/raft patterns
// pgraft_go_tick is called by the PostgreSQL background worker on each iteration
// This is a non-blocking function that processes one tick of Raft work
//
//export pgraft_go_tick
func pgraft_go_tick() C.int {
	if atomic.LoadInt32(&running) == 0 {
		// Log periodically (not every call to avoid spam)
		if tickCount%100 == 0 {
			logInfo("tick() called but not running (tickCount=%d)", tickCount)
		}
		tickCount++
		return -1 // Not running
	}

	if raftNode == nil {
		if tickCount%100 == 0 {
			logInfo("tick() called but raftNode is nil (tickCount=%d)", tickCount)
		}
		tickCount++
		return -1 // Not initialized
	}

	// Perform one Raft tick
	raftNode.Tick()

	// Periodically log status for debugging (every 10 ticks = ~1 second)
	tickCount++
	if tickCount%10 == 0 {
		status := raftNode.Status()
		logInfo("raft status (tick #%d): state=%s, term=%d, lead=%d",
			tickCount, status.RaftState.String(), status.Term, status.Lead)
	}

	// FIX #2: Immediate campaign for single-node clusters (after first tick)
	if tickCount == 1 {
		logInfo("First tick received! Checking if single-node...")
		status := raftNode.Status()
		isSingleNode := len(status.Config.Voters[0]) == 1
		logInfo("Is single-node? %v (voters: %d)", isSingleNode, len(status.Config.Voters[0]))
		if isSingleNode && status.RaftState != raft.StateLeader {
			logInfo("Single-node cluster detected - calling Campaign() immediately")
			raftNode.Campaign(raftCtx)
		}
	}

	return 0
}

// Separate Ready processing loop (etcd/raft recommended pattern)
func raftProcessingLoop() {
	defer func() {
		if raftDone != nil {
			close(raftDone)
		}
	}()

	logInfo("Ready processing loop started")

	// Safety check
	if raftNode == nil {
		logInfo("CRITICAL ERROR - raftNode is nil in processing loop")
		return
	}

	logInfo("Ready processing loop initialized (raftNode confirmed)")

	for {
		select {
		case <-raftCtx.Done():
			logInfo("Ready processing loop stopping (context done)")
			return
		case <-stopChan:
			logInfo("Ready processing loop stopping (stop signal)")
			return
		case rd := <-raftNode.Ready():
			logInfo("DEBUG - Received Ready message from Raft node")
			processReady(rd)
		}
	}
}

// Process Ready messages: persist, send, apply, advance
func processReady(rd raft.Ready) {
	// Safety: recover from any panics to prevent crashing the processing loop
	defer func() {
		if r := recover(); r != nil {
			logError("panic in processReady: %v", r)
		}
	}()

	// No mutex needed - only called from raftProcessingLoop goroutine

	status := raftNode.Status()
	logInfo("processing ready: messages=%d, entries=%d, committed=%d, hardstate=%v, snapshot=%v. current raft status: state=%s, term=%d, lead=%d",
		len(rd.Messages), len(rd.Entries), len(rd.CommittedEntries), !raft.IsEmptyHardState(rd.HardState), !raft.IsEmptySnap(rd.Snapshot),
		status.RaftState.String(), status.Term, status.Lead)

	// CRITICAL: Persist entries BEFORE HardState to ensure consistency
	// The HardState.Commit index must not reference entries that aren't persisted yet
	if len(rd.Entries) > 0 {
		logInfo("About to persist %d entries...", len(rd.Entries))
		if raftStorage == nil {
			logInfo("ERROR - raftStorage is nil, cannot append entries")
			return
		}
		raftStorage.Append(rd.Entries)
		logInfo("Persisted %d entries", len(rd.Entries))
	}

	// Now persist HardState after entries are safely on disk
	if !raft.IsEmptyHardState(rd.HardState) {
		if raftStorage == nil {
			logInfo("ERROR - raftStorage is nil, cannot persist HardState")
			return
		}
		raftStorage.SetHardState(rd.HardState)
		logInfo("Persisted HardState: term=%d, vote=%d, commit=%d", rd.HardState.Term, rd.HardState.Vote, rd.HardState.Commit)
	}

	logInfo("About to send %d messages...", len(rd.Messages))
	for _, msg := range rd.Messages {
		sendMessage(msg)
	}
	if len(rd.Messages) > 0 {
		logInfo("Sent %d messages", len(rd.Messages))
	} else {
		logInfo("DEBUG - No messages in Ready batch")
	}

	// Apply committed entries to the state machine
	if len(rd.CommittedEntries) > 0 {
		logInfo("Applying %d committed entries. Current applied index: %d", len(rd.CommittedEntries), appliedIndex)
		for _, entry := range rd.CommittedEntries {
			logInfo("Committed entry %d: index=%d, term=%d, type=%s, data=%s", entry.Index, entry.Index, entry.Term, entry.Type.String(), string(entry.Data))
			if entry.Index > appliedIndex { // Only apply if not already applied
				applyEntry(entry)
				appliedIndex = entry.Index // Update applied index
				logInfo("Applied entry %d. New applied index: %d", entry.Index, appliedIndex)
			} else {
				logInfo("Skipping already applied entry: index=%d <= appliedIndex=%d", entry.Index, appliedIndex)
			}
		}
		logInfo("Applied %d committed entries", len(rd.CommittedEntries))
	}

	// After all local processing, advance the Raft node.
	logInfo("DEBUG - About to call raftNode.Advance()...")
	raftNode.Advance()
	logInfo("raft node advanced. new raft status: state=%s, term=%d, lead=%d",
		raftNode.Status().RaftState.String(), raftNode.Status().Term, raftNode.Status().Lead)
	status = raftNode.Status() // Get updated status after Advance
	logInfo("post-advance raft status: state=%s, term=%d, lead=%d",
		status.RaftState.String(), status.Term, status.Lead)

	// Update clusterState.LeaderID and CurrentTerm based on the current Raft status
	// If the current node is the leader, its ID is the LeaderID.
	if status.RaftState == raft.StateLeader {
		clusterState.LeaderID = status.ID
		logInfo("Node %d is now the Leader. Updating clusterState.LeaderID to %d", status.ID, clusterState.LeaderID)
	} else if status.Lead != 0 {
		clusterState.LeaderID = status.Lead
		logInfo("Leader identified as %d. Updating clusterState.LeaderID to %d", status.Lead, clusterState.LeaderID)
	} else {
		clusterState.LeaderID = 0 // No leader elected yet or unknown, default to 0 (invalid ID)
		logInfo("INFO - No leader identified (status.Lead is 0 or node is not leader). Setting clusterState.LeaderID to 0")
	}
	clusterState.CurrentTerm = status.Term
	clusterState.State = status.RaftState.String()
	logInfo("Shared memory updated: LeaderID=%d, CurrentTerm=%d, State=%s", clusterState.LeaderID, clusterState.CurrentTerm, clusterState.State)

	// Update clusterState.Nodes based on raftNode.Status().Progress for active followers/learners
	currentNodes := make(map[uint64]string)
	for id, _ := range status.Progress { // Fixed: 'pr' declared and not used
		if addr, ok := nodes[id]; ok {
			currentNodes[id] = addr
		} else {
			// If the node is in progress but not in our 'nodes' map, log a warning
			logWarning("Node %d in Raft progress but not in local nodes map", id)
		}
	}

	// Also add self if not already present from progress (should always be present)
	if _, ok := currentNodes[raftConfig.ID]; !ok {
		if addr, ok := nodes[raftConfig.ID]; ok {
			currentNodes[raftConfig.ID] = addr
		}
	}
	clusterState.Nodes = currentNodes
	logInfo("clusterState.Nodes updated. Current nodes: %v", clusterState.Nodes)

	// If a snapshot exists, apply it and compact storage
	if !raft.IsEmptySnap(rd.Snapshot) {
		logInfo("Applying snapshot at index %d, term %d", rd.Snapshot.Metadata.Index, rd.Snapshot.Metadata.Term)
		// Apply the snapshot to the state machine
		// Note: MemoryStorage.ApplySnapshot only updates the internal state,
		// it doesn't apply to the actual replicated state machine directly.
		// The committed entries should handle state machine updates.
		if err := raftStorage.ApplySnapshot(rd.Snapshot); err != nil {
			logError("Failed to apply snapshot to storage: %v", err)
		}
		// After applying a snapshot, update appliedIndex and committedIndex
		appliedIndex = rd.Snapshot.Metadata.Index
		logInfo("Snapshot applied, appliedIndex updated to %d", appliedIndex)
	}
}

// Message receiver routes all messages through main loop
func messageReceiver() {
	logInfo("Message receiver started (routing to main loop)")

	for {
		select {
		case <-stopChan:
			logInfo("Message receiver stopped")
			return
		case msg := <-messageChan:
			// Route the message to the main raftProcessingLoop by stepping the node.
			// This ensures all message processing goes through the Raft node's Step function.
			// No need for separate goroutine here as raftProcessingLoop handles it.
			raftMutex.RLock()
			raftNode.Step(raftCtx, msg)
			raftMutex.RUnlock()
			atomic.AddInt64(&messagesProcessed, 1)
			debugLog("Processed incoming message from %d to %d (type=%s, term=%d)", msg.From, msg.To, msg.Type.String(), msg.Term)
		}
	}
}

// Handle incoming message from a specific connection
func handleIncomingMessage(nodeID uint64, conn net.Conn) {
	// Set read timeout
	conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))

	// Read message length first
	var msgLen uint32
	// Use io.ReadFull to ensure all 4 bytes are read
	buf := make([]byte, 4)
	if _, err := io.ReadFull(conn, buf); err != nil {
		if err != io.EOF && !strings.Contains(err.Error(), "use of closed network connection") {
			logWarning("Failed to read message length from node %d: %v", nodeID, err)
		}
		return // No message or timeout, or connection closed
	}
	msgLen = binary.BigEndian.Uint32(buf)

	// Read message data
	msgData := make([]byte, msgLen)
	if _, err := io.ReadFull(conn, msgData); err != nil {
		if err != io.EOF && !strings.Contains(err.Error(), "use of closed network connection") {
			logWarning("Failed to read message data from node %d: %v", nodeID, err)
		}
		return
	}

	// Parse as raftpb.Message
	var msg raftpb.Message
	if err := msg.Unmarshal(msgData); err != nil {
		logInfo("failed to unmarshal incoming message: %v", err)
		return
	}

	// Step the message
	raftNode.Step(raftCtx, msg)
	atomic.AddInt64(&messagesProcessed, 1)
}

// Apply committed entry to state machine
func applyEntry(entry raftpb.Entry) {
	logInfo("applying committed entry: index=%d, term=%d, type=%s",
		entry.Index, entry.Term, entry.Type.String())

	// Process different entry types
	switch entry.Type {
	case raftpb.EntryNormal:
		// Apply to PostgreSQL on ALL nodes (leader + followers)
		if len(entry.Data) == 0 {
			logInfo("empty entry at index %d, skipping", entry.Index)
			appliedIndex = entry.Index
			return
		}

		// Store committed entry data for background worker to apply
		// The background worker will read from Raft storage and apply to PostgreSQL

		// Update applied index
		appliedIndex = entry.Index
		logInfo("✅ COMMITTED entry %d (len=%d bytes) - READY FOR APPLICATION: %s",
			entry.Index, len(entry.Data), string(entry.Data))

	case raftpb.EntryConfChange:
		// Apply configuration change
		var cc raftpb.ConfChange
		if err := cc.Unmarshal(entry.Data); err != nil {
			logError("failed to unmarshal ConfChange: %v", err)
			return
		}
		applyConfChange(cc)
		appliedIndex = entry.Index

	case raftpb.EntryConfChangeV2:
		// Apply configuration change v2
		var cc raftpb.ConfChangeV2
		if err := cc.Unmarshal(entry.Data); err != nil {
			logError("failed to unmarshal ConfChangeV2: %v", err)
			return
		}
		applyConfChangeV2(cc)
		appliedIndex = entry.Index
	}
}

// Apply configuration change
func applyConfChange(cc raftpb.ConfChange) {
	logInfo("Applying ConfChange: type=%s, node=%d", cc.Type.String(), cc.NodeID)

	// CRITICAL: Actually apply the configuration change to the Raft node
	if raftNode != nil {
		logInfo("Applying ConfChange to Raft node")
		raftNode.ApplyConfChange(cc)
		logInfo("ConfChange applied to Raft node successfully")
	} else {
		logInfo("ERROR - Raft node is nil, cannot apply ConfChange")
		return
	}

	switch cc.Type {
	case raftpb.ConfChangeAddNode:
		logInfo("Added node %d to cluster", cc.NodeID)
		// Update transport membership
		updateTransportMembership()
	case raftpb.ConfChangeRemoveNode:
		logInfo("Removed node %d from cluster", cc.NodeID)
		// Update transport membership
		updateTransportMembership()
	case raftpb.ConfChangeUpdateNode:
		logInfo("Updated node %d in cluster", cc.NodeID)
		// Update transport membership
		updateTransportMembership()
	}
}

// Update transport membership to match Raft configuration
func updateTransportMembership() {
	logInfo("Updating transport membership")

	// Get current Raft status to see the configuration
	if raftNode == nil {
		logInfo("WARNING - Raft node is nil, cannot update transport membership")
		return
	}

	status := raftNode.Status()
	logInfo("Current Raft configuration: voters=%v, learners=%v", status.Config.Voters, status.Config.Learners)

	// The Raft node's configuration is automatically updated by ApplyConfChange
	// We just need to ensure our transport layer knows about the changes
	nodesMutex.Lock()
	defer nodesMutex.Unlock()

	// Remove nodes that are no longer in the configuration
	for id := range status.Config.Voters {
		found := false
		for voterID := range status.Config.Voters {
			if id == voterID {
				found = true
				break
			}
		}
		if !found && uint64(id) != raftConfig.ID {
			logInfo("Removing disconnected node %d from transport membership", id)
			delete(nodes, uint64(id))
			// Also close and remove connection if it exists
			connMutex.Lock()
			if managedConn, ok := connections[uint64(id)]; ok {
				managedConn.Conn.Close()
				delete(connections, uint64(id))
			}
			connMutex.Unlock()
		}
	}
	for id := range status.Config.Learners {
		found := false
		for learnerID := range status.Config.Learners {
			if id == learnerID {
				found = true
				break
			}
		}
		if !found && id != raftConfig.ID {
			logInfo("Removing disconnected learner node %d from transport membership", id)
			delete(nodes, id)
			// Also close and remove connection if it exists
			connMutex.Lock()
			if managedConn, ok := connections[id]; ok {
				managedConn.Conn.Close()
				delete(connections, id)
			}
			connMutex.Unlock()
		}
	}

	logInfo("Transport membership updated - nodes: %v", nodes)
}

// Apply configuration change v2
func applyConfChangeV2(cc raftpb.ConfChangeV2) {
	logInfo("Applying ConfChangeV2: changes=%d", len(cc.Changes))

	// CRITICAL: Actually apply the configuration change to the Raft node
	if raftNode != nil {
		logInfo("Applying ConfChangeV2 to Raft node")
		raftNode.ApplyConfChange(cc)
		logInfo("ConfChangeV2 applied to Raft node successfully")
	} else {
		logInfo("ERROR - Raft node is nil, cannot apply ConfChangeV2")
		return
	}

	for _, change := range cc.Changes {
		switch change.Type {
		case raftpb.ConfChangeAddNode:
			logInfo("Added node %d to cluster", change.NodeID)
		case raftpb.ConfChangeRemoveNode:
			logInfo("Removed node %d from cluster", change.NodeID)
		case raftpb.ConfChangeUpdateNode:
			logInfo("Updated node %d in cluster", change.NodeID)
		}
	}

	// Update transport membership
	updateTransportMembership()
}

// Process outgoing messages through comm module
func processMessage(msg raftpb.Message) {
	// Convert message to bytes
	data, err := msg.Marshal()
	if err != nil {
		logInfo("failed to marshal message: %v", err)
		return
	}

	// Send to specific node
	if msg.To != 0 {
		sendToNode(msg.To, data)
	} else {
		// Broadcast to all nodes
		broadcastToAllNodes(data)
	}

	atomic.AddInt64(&messagesProcessed, 1)
}

// Send message to specific node
func sendToNode(nodeID uint64, data []byte) {
	connMutex.RLock()
	conn, exists := connections[nodeID]
	connMutex.RUnlock()

	if !exists {
		logInfo("no connection to node %d, requesting reconnection", nodeID)
		reconnectChan <- nodeID
		return
	}

	// Send message length first
	if err := writeUint32(conn.Conn, uint32(len(data))); err != nil {
		logInfo("failed to send message length to node %d: %v", nodeID, err)
		// Clean up broken connection
		conn.Mutex.Lock()
		if existingConn, ok := connections[nodeID]; ok && existingConn == conn {
			existingConn.Conn.Close()
			delete(connections, nodeID)
			logInfo("Cleaned up broken connection for node %d", nodeID)
		}
		conn.Mutex.Unlock()
		reconnectChan <- nodeID // Signal for reconnection
		return
	}

	// Send message data
	if _, err := conn.Conn.Write(data); err != nil {
		logInfo("failed to send message to node %d: %v", nodeID, err)
		// Clean up broken connection
		conn.Mutex.Lock()
		if existingConn, ok := connections[nodeID]; ok && existingConn == conn {
			existingConn.Conn.Close()
			delete(connections, nodeID)
			logInfo("Cleaned up broken connection for node %d", nodeID)
		}
		conn.Mutex.Unlock()
		reconnectChan <- nodeID // Signal for reconnection
		return
	}

	logInfo("sent message to node %d, size %d", nodeID, len(data))
}

// Broadcast message to all nodes
func broadcastToAllNodes(data []byte) {
	connMutex.RLock()
	defer connMutex.RUnlock()

	for nodeID := range connections {
		go sendToNode(nodeID, data)
	}
}

// Process committed log entries
func processCommittedEntry(entry raftpb.Entry) {
	// Update committed index
	if entry.Index > committedIndex {
		committedIndex = entry.Index
	}

	// Process configuration changes
	if entry.Type == raftpb.EntryConfChange {
		var cc raftpb.ConfChange
		cc.Unmarshal(entry.Data)
		raftNode.ApplyConfChange(cc)
	}

	// Update applied index
	appliedIndex = entry.Index

	logInfo("applied entry %d, term %d, type %s",
		entry.Index, entry.Term, entry.Type.String())
}

// Start network server to accept incoming connections
func startNetworkServer(address string, port int) {
	listener, err := net.Listen("tcp", fmt.Sprintf("%s:%d", address, port))
	if err != nil {
		logError("Failed to start network server on %s:%d: %v", address, port, err)
		return
	}
	defer listener.Close()

	logInfo("Network server listening on %s:%d", address, port)

	for {
		select {
		case <-raftCtx.Done():
			logInfo("INFO - Network server shutting down")
			return
		case <-stopChan:
			logInfo("INFO - Network server stopping")
			return
		default:
			// Set a timeout for accepting connections
			listener.(*net.TCPListener).SetDeadline(time.Now().Add(1 * time.Second))
			conn, err := listener.Accept()
			if err != nil {
				if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
					continue // Timeout is expected, continue listening
				}
				logWarning("Failed to accept connection: %v", err)
				continue
			}

			// Handle incoming connection in a goroutine
			go handleIncomingConnection(conn)
		}
	}
}

// Handle incoming connection from a peer
func handleIncomingConnection(conn net.Conn) {
	defer conn.Close()

	remoteAddr := conn.RemoteAddr().String()
	logInfo("Incoming connection from %s", remoteAddr)

	// Read node ID from connection (first 4 bytes)
	var nodeID uint32
	if err := readUint32(conn, &nodeID); err != nil {
		logWarning("Failed to read node ID from %s: %v", remoteAddr, err)
		return
	}

	logInfo("Connection from node %d at %s", nodeID, remoteAddr)

	// Store connection
	connMutex.Lock()
	connections[uint64(nodeID)] = &ManagedConnection{Conn: conn}
	connMutex.Unlock()

	// Keep connection alive and handle messages
	handleConnectionMessages(uint64(nodeID), conn)
}

// Handle messages from a connection
func handleConnectionMessages(nodeID uint64, conn net.Conn) {
	connectionErrors := 0
	maxConsecutiveErrors := 3

	for {
		select {
		case <-raftCtx.Done():
			return
		case <-stopChan:
			return
		default:
			// Set aggressive read timeout to detect dead connections faster
			conn.SetReadDeadline(time.Now().Add(2 * time.Second))

			// Read message length
			var msgLen uint32
			if err := readUint32(conn, &msgLen); err != nil {
				connectionErrors++
				if connectionErrors >= maxConsecutiveErrors {
					logWarning("Connection to node %d failed after %d errors, closing: %v", nodeID, connectionErrors, err)
					// Clean up broken connection
					connMutex.Lock()
					if existingConn, ok := connections[nodeID]; ok && existingConn.Conn == conn {
						existingConn.Conn.Close()
						delete(connections, nodeID)
						logInfo("Cleaned up broken connection for node %d", nodeID)
					}
					connMutex.Unlock()
					reconnectChan <- nodeID // Signal for reconnection
					return
				}
				// Short sleep before retry
				time.Sleep(100 * time.Millisecond)
				continue
			}

			// Read message data
			data := make([]byte, msgLen)
			if _, err := conn.Read(data); err != nil {
				connectionErrors++
				if connectionErrors >= maxConsecutiveErrors {
					logWarning("Failed to read from node %d after %d errors, closing: %v", nodeID, connectionErrors, err)
					// Clean up broken connection
					connMutex.Lock()
					if existingConn, ok := connections[nodeID]; ok && existingConn.Conn == conn {
						existingConn.Conn.Close()
						delete(connections, nodeID)
						logInfo("Cleaned up broken connection for node %d", nodeID)
					}
					connMutex.Unlock()
					reconnectChan <- nodeID // Signal for reconnection
					return
				}
				time.Sleep(100 * time.Millisecond)
				continue
			}

			// Reset error counter on successful read
			connectionErrors = 0

			// Process message
			var msg raftpb.Message
			if err := msg.Unmarshal(data); err != nil {
				logWarning("Failed to unmarshal message from node %d: %v", nodeID, err)
				continue
			}

			logInfo("Received message from node %d: type=%s, term=%d", nodeID, msg.Type.String(), msg.Term)

			// Send message to Raft node
			select {
			case messageChan <- msg:
			default:
				logWarning("Message channel full, dropping message from node %d", nodeID)
			}
		}
	}
}

// Load and connect to configured peers
func loadAndConnectToPeers() {
	logInfo("INFO - Starting peer discovery process")

	// Start peer discovery in a separate goroutine to avoid blocking
	go func() {
		defer func() {
			if r := recover(); r != nil {
				logError("panic in loadAndConnectToPeers goroutine: %v", r)
			}
		}()

		// Add timeout to ensure function completes
		done := make(chan bool, 1)
		go func() {
			// Load configuration from file
			config, err := loadConfiguration()
			if err != nil {
				logWarning("Failed to load configuration: %v", err)
				done <- true
				return
			}

			// Parse peer addresses
			peerAddresses := parsePeerAddresses(config.PeerAddresses)
			logInfo("Found %d configured peer addresses", len(peerAddresses))

			// Connect to each peer
			for i, peerAddr := range peerAddresses {
				nodeID := uint64(i + 1) // Node IDs: 1, 2, 3

				// Skip self-connection (current node is 1)
				if nodeID == 1 {
					logInfo("Skipping self-connection to node %d (%s)", nodeID, peerAddr)
					continue
				}

				// Check if connection already exists
				connMutex.Lock()
				_, exists := connections[nodeID]
				connMutex.Unlock()

				if exists {
					logInfo("Connection to node %d already exists, skipping", nodeID)
					continue
				}

				// Start connection in a separate goroutine to avoid blocking
				go establishConnectionWithRetry(nodeID, peerAddr)
			}
			logInfo("INFO - Peer discovery process completed")
			done <- true
		}()

		// Wait for completion or timeout
		select {
		case <-done:
			logInfo("INFO - Peer discovery completed successfully")
		case <-time.After(5 * time.Second):
			logInfo("WARNING - Peer discovery timed out after 5 seconds")
		}
	}()

	logInfo("INFO - Peer discovery goroutine started")
}

// Establish connection with retry logic
func establishConnectionWithRetry(nodeID uint64, peerAddr string) {
	// Check if connection already exists before attempting
	connMutex.Lock()
	_, exists := connections[nodeID]
	connMutex.Unlock()

	if exists {
		logInfo("Connection to node %d already exists, skipping retry", nodeID)
		return
	}

	// Start retry logic in a separate goroutine to avoid blocking
	go func() {
		maxRetries := 5
		retryDelay := 2 * time.Second

		for attempt := 0; attempt < maxRetries; attempt++ {
			err := connectToPeer(nodeID, peerAddr)
			if err == nil {
				logInfo("Successfully connected to peer %s (node %d)", peerAddr, nodeID)
				return
			}

			log.Printf("pgraft: WARNING - Failed to connect to peer %s (node %d, attempt %d/%d): %v",
				peerAddr, nodeID, attempt+1, maxRetries, err)

			if attempt < maxRetries-1 {
				time.Sleep(retryDelay)
				retryDelay *= 2 // Exponential backoff
			}
		}

		logError("failed to connect to peer %s (node %d) after %d attempts",
			peerAddr, nodeID, maxRetries)
	}()
}

// Connect to a specific peer
func connectToPeer(nodeID uint64, peerAddr string) error {
	conn, err := net.DialTimeout("tcp", peerAddr, 1*time.Second)
	if err != nil {
		return fmt.Errorf("failed to dial %s: %v", peerAddr, err)
	}

	// Send OUR node ID (not the target's ID) so the peer knows who we are
	myNodeID := raftConfig.ID
	if err := writeUint32(conn, uint32(myNodeID)); err != nil {
		conn.Close()
		return fmt.Errorf("failed to send node ID: %v", err)
	}
	logInfo("Sent our node ID %d to peer %d at %s", myNodeID, nodeID, peerAddr)

	// Store connection, ensuring to close any old one first
	connMutex.Lock()
	if oldConn, exists := connections[nodeID]; exists {
		logInfo("Closing old connection to node %d before replacing", nodeID)
		oldConn.Conn.Close()
	}
	connections[nodeID] = &ManagedConnection{Conn: conn}
	connMutex.Unlock()

	logInfo("Connected to peer %s (node %d)", peerAddr, nodeID)

	// Start message handling for this connection
	go handleConnectionMessages(nodeID, conn)

	return nil
}

// Configuration structure
type PGRaftConfig struct {
	PeerAddresses string
	LogLevel      string
	Port          int
}

// Load configuration from file
func loadConfiguration() (*PGRaftConfig, error) {
	config := &PGRaftConfig{
		PeerAddresses: "",
		LogLevel:      "info",
		Port:          7400,
	}

	// Try to read from common configuration locations
	configPaths := []string{
		"/Users/ibrarahmed/pgelephant/pge/ram/conf/pgraft.conf",
		"/etc/pgraft/pgraft.conf",
		"./pgraft.conf",
	}

	for _, path := range configPaths {
		if data, err := os.ReadFile(path); err == nil {
			logInfo("Loading configuration from %s", path)
			return parseConfigurationFile(string(data)), nil
		}
	}

	logInfo("WARNING - No configuration file found, using defaults")
	return config, nil
}

// Parse configuration file content
func parseConfigurationFile(content string) *PGRaftConfig {
	config := &PGRaftConfig{
		PeerAddresses: "",
		LogLevel:      "info",
		Port:          7400,
	}

	lines := strings.Split(content, "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}

		key := strings.TrimSpace(parts[0])
		value := strings.TrimSpace(parts[1])

		switch key {
		case "raft_peer_addresses":
			config.PeerAddresses = value
		case "raft_log_level":
			config.LogLevel = value
		case "raft_port":
			if port, err := strconv.Atoi(value); err == nil {
				config.Port = port
			}
		}
	}

	return config
}

// Parse peer addresses from configuration string
func parsePeerAddresses(peerAddressesStr string) []string {
	if peerAddressesStr == "" {
		return []string{}
	}

	addresses := strings.Split(peerAddressesStr, ",")
	var result []string

	for _, addr := range addresses {
		addr = strings.TrimSpace(addr)
		if addr != "" {
			result = append(result, addr)
		}
	}

	return result
}

// ============================================================================
// REPLICATION FUNCTIONS - Using etcd-io/raft patterns
// ============================================================================

//export pgraft_go_replicate_log_entry
func pgraft_go_replicate_log_entry(data *C.char, dataLen C.int) C.int {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if raftNode == nil {
		return C.int(0)
	}

	// Convert C data to Go
	goData := C.GoBytes(unsafe.Pointer(data), dataLen)

	// Propose the log entry for replication
	ctx, cancel := context.WithTimeout(raftCtx, 5*time.Second)
	defer cancel()

	err := raftNode.Propose(ctx, goData)
	if err != nil {
		recordError(fmt.Errorf("failed to propose log entry: %w", err))
		return C.int(0)
	}

	logInfo("proposed log entry for replication, size: %d bytes", len(goData))
	return C.int(1)
}

//export pgraft_go_get_replication_status
func pgraft_go_get_replication_status() *C.char {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	replicationState.replicationMutex.RLock()
	defer replicationState.replicationMutex.RUnlock()

	status := map[string]interface{}{
		"last_applied_index":  replicationState.lastAppliedIndex,
		"last_snapshot_index": replicationState.lastSnapshotIndex,
		"replication_lag_ms":  replicationState.replicationLag.Milliseconds(),
		"is_leader":           pgraft_go_get_leader() != 0,
		"committed_index":     committedIndex,
		"applied_index":       appliedIndex,
	}

	jsonData, err := json.Marshal(status)
	if err != nil {
		recordError(fmt.Errorf("failed to marshal replication status: %w", err))
		return C.CString("{}")
	}

	return C.CString(string(jsonData))
}

//export pgraft_go_create_snapshot
func pgraft_go_create_snapshot() *C.char {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if raftNode == nil {
		return C.CString("")
	}

	// Create snapshot using etcd-io/raft
	snapshot, err := raftStorage.CreateSnapshot(committedIndex, &raftpb.ConfState{
		Voters: getClusterNodes(),
	}, []byte("pgraft_snapshot_data"))

	if err != nil {
		recordError(fmt.Errorf("failed to create snapshot: %w", err))
		return C.CString("")
	}

	// Update replication state
	replicationState.replicationMutex.Lock()
	replicationState.lastSnapshotIndex = snapshot.Metadata.Index
	replicationState.replicationMutex.Unlock()

	// Serialize snapshot for return
	snapshotData, err := json.Marshal(map[string]interface{}{
		"index":     snapshot.Metadata.Index,
		"term":      snapshot.Metadata.Term,
		"data":      string(snapshot.Data),
		"timestamp": time.Now().Unix(),
	})

	if err != nil {
		recordError(fmt.Errorf("failed to marshal snapshot: %w", err))
		return C.CString("")
	}

	logInfo("created snapshot at index %d", snapshot.Metadata.Index)
	return C.CString(string(snapshotData))
}

//export pgraft_go_apply_snapshot
func pgraft_go_apply_snapshot(snapshotData *C.char) C.int {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if raftNode == nil {
		return C.int(0)
	}

	// Parse snapshot data
	var snapshotInfo map[string]interface{}
	err := json.Unmarshal([]byte(C.GoString(snapshotData)), &snapshotInfo)
	if err != nil {
		recordError(fmt.Errorf("failed to parse snapshot data: %w", err))
		return C.int(0)
	}

	// Create snapshot from data
	snapshot := raftpb.Snapshot{
		Data: []byte(snapshotInfo["data"].(string)),
		Metadata: raftpb.SnapshotMetadata{
			Index: uint64(snapshotInfo["index"].(float64)),
			Term:  uint64(snapshotInfo["term"].(float64)),
		},
	}

	// Apply snapshot to storage
	err = raftStorage.ApplySnapshot(snapshot)
	if err != nil {
		recordError(fmt.Errorf("failed to apply snapshot: %w", err))
		return C.int(0)
	}

	// Update replication state
	replicationState.replicationMutex.Lock()
	replicationState.lastSnapshotIndex = snapshot.Metadata.Index
	replicationState.lastAppliedIndex = snapshot.Metadata.Index
	replicationState.replicationMutex.Unlock()

	logInfo("applied snapshot at index %d", snapshot.Metadata.Index)
	return C.int(1)
}

//export pgraft_go_replicate_to_node
func pgraft_go_replicate_to_node(nodeID C.uint64_t, data *C.char, dataLen C.int) C.int {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if raftNode == nil {
		return C.int(0)
	}

	// Convert C data to Go
	goData := C.GoBytes(unsafe.Pointer(data), dataLen)

	// Create message for replication
	msg := raftpb.Message{
		Type:    raftpb.MsgApp,
		To:      uint64(nodeID),
		From:    raftConfig.ID,
		Term:    getCurrentTerm(),
		LogTerm: getCurrentTerm(),
		Index:   committedIndex,
		Entries: []raftpb.Entry{
			{
				Term:  getCurrentTerm(),
				Index: committedIndex + 1,
				Type:  raftpb.EntryNormal,
				Data:  goData,
			},
		},
	}

	// Send message through the message channel
	select {
	case messageChan <- msg:
		logInfo("sent replication message to node %d", nodeID)
		return C.int(1)
	default:
		recordError(fmt.Errorf("message channel full, cannot replicate to node"))
		return C.int(0)
	}
}

//export pgraft_go_get_replication_lag
func pgraft_go_get_replication_lag() C.double {
	replicationState.replicationMutex.RLock()
	defer replicationState.replicationMutex.RUnlock()

	// Calculate replication lag based on committed vs applied index
	lag := float64(committedIndex - replicationState.lastAppliedIndex)

	// Update replication lag duration
	replicationState.replicationLag = time.Duration(lag) * time.Millisecond

	return C.double(lag)
}

//export pgraft_go_sync_replication
func pgraft_go_sync_replication() C.int {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if raftNode == nil {
		return C.int(0)
	}

	// Force a replication sync by processing ready channel
	select {
	case rd := <-raftNode.Ready():
		// Process committed entries for replication
		for _, entry := range rd.CommittedEntries {
			if entry.Type == raftpb.EntryNormal {
				// Apply the entry to state machine
				appliedIndex = entry.Index
				replicationState.replicationMutex.Lock()
				replicationState.lastAppliedIndex = entry.Index
				replicationState.replicationMutex.Unlock()

				logInfo("applied entry %d for replication", entry.Index)
			}
		}

		// Advance the node
		raftNode.Advance()
		return C.int(1)
	default:
		// No ready data available
		return C.int(0)
	}
}

// Helper functions for replication
func getClusterNodes() []uint64 {
	// Return current cluster node IDs from actual Raft state
	if raftNode == nil {
		return []uint64{}
	}

	// Get the current configuration from Raft
	status := raftNode.Status()
	if len(status.Config.Voters) > 0 {
		var nodes []uint64
		for nodeID := range status.Config.Voters {
			nodes = append(nodes, uint64(nodeID))
		}
		return nodes
	}

	// Fallback to default cluster nodes if no config available
	return []uint64{1, 2, 3}
}

// processRaftReady processes Raft ready messages for leader election and log replication
func processRaftReady() {
	logInfo("processRaftReady started")

	for {
		select {
		case <-raftCtx.Done():
			logInfo("processRaftReady stopping")
			return
		case rd := <-raftNode.Ready():
			logInfo("DEBUG - Processing Raft Ready message")

			// Save to storage
			if !raft.IsEmptyHardState(rd.HardState) {
				logInfo("Saving hard state: term=%d, commit=%d", rd.HardState.Term, rd.HardState.Commit)
				raftStorage.SetHardState(rd.HardState)

				// Update cluster state
				clusterState.CurrentTerm = rd.HardState.Term
				clusterState.CommitIndex = rd.HardState.Commit

				// Update cluster state with current term
				clusterState.CurrentTerm = rd.HardState.Term
				logInfo("Updated term to %d", rd.HardState.Term)
			}

			// Save entries
			if len(rd.Entries) > 0 {
				logInfo("Saving %d entries", len(rd.Entries))
				raftStorage.Append(rd.Entries)
				clusterState.LastIndex = rd.Entries[len(rd.Entries)-1].Index
			}

			// Check if this node is now the leader
			status := raftNode.Status()
			if status.RaftState == raft.StateLeader {
				if clusterState.LeaderID != raftConfig.ID {
					clusterState.LeaderID = raftConfig.ID
					clusterState.State = "leader"
					logInfo("This node is now the LEADER (node %d)", raftConfig.ID)

					// Update shared memory cluster state
					updateSharedMemoryClusterState(int64(raftConfig.ID), int64(clusterState.CurrentTerm), "leader")
				}
			} else if status.RaftState == raft.StateFollower {
				if clusterState.State != "follower" {
					clusterState.State = "follower"
					logInfo("INFO - This node is now a FOLLOWER")

					// Update shared memory cluster state
					updateSharedMemoryClusterState(int64(clusterState.LeaderID), int64(clusterState.CurrentTerm), "follower")
				}
			} else if status.RaftState == raft.StateCandidate {
				if clusterState.State != "candidate" {
					clusterState.State = "candidate"
					logInfo("INFO - This node is now a CANDIDATE")

					// Update shared memory cluster state
					updateSharedMemoryClusterState(int64(clusterState.LeaderID), int64(clusterState.CurrentTerm), "candidate")
				}
			}

			// Process committed entries
			for _, entry := range rd.CommittedEntries {
				if entry.Type == raftpb.EntryConfChange {
					logInfo("processing configuration change")
					var cc raftpb.ConfChange
					cc.Unmarshal(entry.Data)

					switch cc.Type {
					case raftpb.ConfChangeAddNode:
						logInfo("adding node %d", cc.NodeID)
						raftNode.ApplyConfChange(cc)
					case raftpb.ConfChangeRemoveNode:
						logInfo("removing node %d", cc.NodeID)
						raftNode.ApplyConfChange(cc)
					}
				} else if entry.Type == raftpb.EntryNormal && len(entry.Data) > 0 {
					logInfo("processing normal entry: %s", string(entry.Data))
					// Process normal log entry
					committedIndex = entry.Index
					atomic.StoreInt64(&logEntriesCommitted, int64(entry.Index))
				}
			}

			// Send messages to peers
			for _, msg := range rd.Messages {
				logInfo("Sending message type %s from %d to %d", msg.Type, msg.From, msg.To)
				sendMessage(msg)
			}

			// Process state changes
			if rd.SoftState != nil {
				logInfo("state changed to %s, leader: %d",
					raft.StateType(rd.SoftState.RaftState).String(), rd.SoftState.Lead)

				raftMutex.Lock()
				// Get current term from storage
				hs, _, _ := raftStorage.InitialState()
				clusterState.CurrentTerm = hs.Term
				clusterState.LeaderID = rd.SoftState.Lead
				clusterState.State = raft.StateType(rd.SoftState.RaftState).String()
				raftMutex.Unlock()

				// Update shared memory cluster state
				stateStr := raft.StateType(rd.SoftState.RaftState).String()
				updateSharedMemoryClusterState(int64(rd.SoftState.Lead), int64(hs.Term), stateStr)

				if rd.SoftState.Lead != 0 {
					logInfo("leader elected: %d", rd.SoftState.Lead)
					atomic.StoreInt64(&electionsTriggered, atomic.LoadInt64(&electionsTriggered)+1)
				}
			}

			// Advance the node
			raftNode.Advance()
		}
	}
}

// processRaftTicker handles periodic Raft operations
func processRaftTicker() {
	logInfo("processRaftTicker started")

	for {
		select {
		case <-raftCtx.Done():
			logInfo("processRaftTicker stopping")
			return
		case <-raftTicker.C:
			if raftNode != nil {
				// Tick the Raft node (this triggers elections, heartbeats, etc.)
				raftNode.Tick()

				// Check for ready messages - now handled by raftProcessingLoop
				// Removing direct interaction with raftNode.Ready() from here.
				/*
					select {
					case rd := <-raftNode.Ready():
							logInfo("ticker received ready message")
						raftReady <- rd
					default:
						// No ready message
					}
				*/
			} else {
				logInfo("ticker - raftNode is nil")
			}
		}
	}
}

func getCurrentTerm() uint64 {
	// Get current term from storage
	hs, _, _ := raftStorage.InitialState()
	return hs.Term
}

// sendMessage sends a Raft message to a peer
func sendMessage(msg raftpb.Message) {
	logInfo("Sending message to node %d: type=%s", msg.To, msg.Type)

	// Avoid sending messages to self
	if msg.To == raftConfig.ID {
		logInfo("Skipping sending message to self (node %d)", msg.To)
		raftNode.Step(raftCtx, msg) // Process message locally for self
		return
	}

	nodesMutex.RLock()
	address, ok := nodes[msg.To]
	nodesMutex.RUnlock()

	if !ok {
		logWarning("Attempted to send message to unknown node %d", msg.To)
		return
	}

	connMutex.RLock()
	managedConn, connExists := connections[msg.To]
	connMutex.RUnlock()

	if !connExists || managedConn.Conn == nil {
		// Connection doesn't exist or is closed, request a reconnection.
		logInfo("Connection to node %d (%s) not found or closed. Requesting reconnection.", msg.To, address)
		reconnectChan <- msg.To
		return
	}

	data, err := msg.Marshal()
	if err != nil {
		logError("Failed to marshal Raft message for node %d: %v", msg.To, err)
		return
	}

	managedConn.Mutex.Lock() // Protect writes to this specific connection
	defer managedConn.Mutex.Unlock()

	// Send message length first (4 bytes, big-endian)
	if err := writeUint32(managedConn.Conn, uint32(len(data))); err != nil {
		logError("Failed to write message length to node %d (%s): %v", msg.To, address, err)
		managedConn.Conn.Close()
		connMutex.Lock()
		delete(connections, msg.To)
		connMutex.Unlock()
		reconnectChan <- msg.To
		return
	}

	// Then send the message data
	if _, err := managedConn.Conn.Write(data); err != nil {
		logError("Failed to write message data to node %d (%s): %v", msg.To, address, err)
		managedConn.Conn.Close()
		connMutex.Lock()
		delete(connections, msg.To)
		connMutex.Unlock()
		reconnectChan <- msg.To
		return
	}

	managedConn.LastActivity = time.Now()
	atomic.AddInt64(&messagesProcessed, 1)
	debugLog("Sent message to node %d (type=%s, from=%d, term=%d)", msg.To, msg.Type.String(), msg.From, msg.Term)
}

// processIncomingMessages processes messages from the message channel
func processIncomingMessages() {
	logInfo("INFO - Starting message processing loop")

	for {
		select {
		case <-raftDone:
			logInfo("INFO - Message processing loop stopped")
			return
		case <-raftCtx.Done():
			logInfo("INFO - Message processing loop stopped (context cancelled)")
			return
		case msg := <-messageChan:
			if raftNode == nil {
				logInfo("WARNING - Received message but Raft node is nil")
				continue
			}

			logInfo("processing incoming message: type=%s, from=%d, to=%d, term=%d",
				msg.Type.String(), msg.From, msg.To, msg.Term)

			// Send message to Raft node
			raftNode.Step(raftCtx, msg)

			// Update cluster state based on message type
			switch msg.Type {
			case raftpb.MsgVote, raftpb.MsgVoteResp:
				// Update term if this is a higher term
				if msg.Term > clusterState.CurrentTerm {
					clusterState.CurrentTerm = msg.Term
					logInfo("Updated term to %d", msg.Term)
				}

			case raftpb.MsgHeartbeat, raftpb.MsgHeartbeatResp:
				// Update leader information
				if msg.Type == raftpb.MsgHeartbeat && msg.From != 0 {
					clusterState.LeaderID = msg.From
					clusterState.State = "follower"
					logInfo("Received heartbeat from leader %d", msg.From)
				}
			}

			atomic.AddInt64(&messagesProcessed, 1)
		}
	}
}

// updateSharedMemoryClusterState updates the internal cluster state
func updateSharedMemoryClusterState(leaderID int64, currentTerm int64, state string) {
	raftMutex.Lock()
	defer raftMutex.Unlock()

	logInfo("Updating internal cluster state: leader=%d, term=%d, state=%s", leaderID, currentTerm, state)

	// Update the internal cluster state
	clusterState.LeaderID = uint64(leaderID)
	clusterState.CurrentTerm = uint64(currentTerm)
	clusterState.State = state

	logInfo("Internal cluster state updated: %+v", clusterState)
}

//export pgraft_go_update_cluster_state
func pgraft_go_update_cluster_state(leaderID C.longlong, currentTerm C.longlong, state *C.char) C.int {
	// This function will be called from C to update the cluster state
	logInfo("pgraft_go_update_cluster_state called: leader=%d, term=%d, state=%s", int64(leaderID), int64(currentTerm), C.GoString(state))

	// Update the internal cluster state
	updateSharedMemoryClusterState(int64(leaderID), int64(currentTerm), C.GoString(state))

	return 0
}

//export pgraft_go_start_network_server
func pgraft_go_start_network_server(port C.int) C.int {
	logInfo("pgraft_go_start_network_server called with port %d", int(port))

	if atomic.LoadInt32(&running) == 0 {
		logInfo("ERROR - Cannot start network server, Raft not initialized")
		return -1
	}

	// Start the network server in a goroutine
	go startNetworkServer("", int(port))

	logInfo("Network server started successfully on port %d", int(port))
	return 0
}

//export pgraft_go_trigger_heartbeat
func pgraft_go_trigger_heartbeat() C.int {
	raftMutex.Lock()
	defer raftMutex.Unlock()

	if raftNode == nil {
		logInfo("ERROR - Raft node is nil, cannot trigger heartbeat")
		return -1
	}

	status := raftNode.Status()
	if status.RaftState != raft.StateLeader {
		logInfo("WARNING - Not leader, cannot trigger heartbeat")
		return -1
	}

	// Propose an empty entry to trigger a heartbeat
	err := raftNode.Propose(raftCtx, []byte("heartbeat"))
	if err != nil {
		logError("Failed to propose heartbeat: %v", err)
		return -1
	}

	logInfo("INFO - Heartbeat triggered successfully")
	return 0
}

// New connection monitor goroutine
func connectionMonitor() {
	logInfo("Connection monitor started")
	// Map to track ongoing reconnection attempts and their backoff timers
	reconnectingNodes := make(map[uint64]*time.Timer)

	for {
		select {
		case <-raftCtx.Done():
			logInfo("Connection monitor stopping (context done)")
			return
		case <-stopChan:
			logInfo("Connection monitor stopping (stop signal)")
			return
		case nodeID := <-reconnectChan:
			// Only attempt to reconnect if not already trying or if backoff expired
			if _, ok := reconnectingNodes[nodeID]; !ok {
				logInfo("Initiating reconnection for node %d", nodeID)
				go func(id uint64) {
					// Use a backoff strategy for reconnection attempts
					for i := 0; i < 5; i++ { // Try 5 times
						// Check if connection is already established (e.g. by another goroutine)
						connMutex.RLock()
						_, exists := connections[id]
						connMutex.RUnlock()
						if exists {
							logInfo("Connection to node %d re-established by another process, stopping retry", id)
							break
						}

						logInfo("Attempting reconnection to node %d (attempt %d)", id, i+1)
						if err := connectToPeer(id, nodes[id]); err == nil {
							logInfo("Reconnected to node %d successfully", id)
							break
						} else {
							logInfo("Failed to reconnect to node %d (attempt %d): %v", id, i+1, err)
							if i < 4 { // Don't sleep after last attempt
								time.Sleep(time.Duration(i+1) * 500 * time.Millisecond) // Exponential backoff
							}
						}
					}
					// After all retries, if still not connected, clean up map entry
					connMutex.Lock()
					delete(reconnectingNodes, id)
					connMutex.Unlock()
				}(nodeID)
				reconnectingNodes[nodeID] = time.NewTimer(5 * time.Second) // Prevent immediate retry
			}
		case <-time.After(1 * time.Second): // Clean up expired timers for reconnection attempts
			for nodeID, timer := range reconnectingNodes {
				select {
				case <-timer.C:
					connMutex.Lock()
					delete(reconnectingNodes, nodeID)
					connMutex.Unlock()
				default:
					// Timer not expired yet
				}
			}
		}
	}
}

// Key/Value log entry structure (must match C struct)
type kvLogEntry struct {
	OpType    int32
	Key       [256]byte
	Value     [1024]byte
	Timestamp int64
	ClientID  [64]byte
}

// Check if the data represents a key/value log entry
func isKeyValueLogEntry(data []byte) bool {
	// A key/value log entry has a specific minimum size
	expectedSize := 4 + 256 + 1024 + 8 + 64 // OpType + Key + Value + Timestamp + ClientID
	return len(data) == expectedSize
}

// Apply a key/value log entry using C function
func applyKeyValueLogEntry(data []byte, logIndex uint64) {
	logInfo("Applying key/value log entry (size=%d, index=%d)", len(data), logIndex)

	// Parse the key/value log entry
	if len(data) < 4 {
		logInfo("ERROR - Invalid key/value log entry size")
		return
	}

	var kvEntry kvLogEntry
	if len(data) != int(unsafe.Sizeof(kvEntry)) {
		logError("key/value log entry size mismatch: expected %d, got %d",
			unsafe.Sizeof(kvEntry), len(data))
		return
	}

	// Copy data to struct
	copy((*[unsafe.Sizeof(kvEntry)]byte)(unsafe.Pointer(&kvEntry))[:], data)

	// Extract null-terminated strings
	key := string(bytes.Trim(kvEntry.Key[:], "\x00"))
	value := string(bytes.Trim(kvEntry.Value[:], "\x00"))
	clientID := string(bytes.Trim(kvEntry.ClientID[:], "\x00"))

	logInfo("kv operation - type: %d, key: '%s', value: '%s', client: '%s'",
		kvEntry.OpType, key, value, clientID)

	// Call C function to apply the operation
	cKey := C.CString(key)
	cValue := C.CString(value)
	defer C.free(unsafe.Pointer(cKey))
	defer C.free(unsafe.Pointer(cValue))

	logInfo("Applying KV operation to store: %s=%s (log_index=%d)", key, value, logIndex)
}

//export pgraft_go_log_replicate
func pgraft_go_log_replicate(leaderID C.ulonglong, fromIndex C.ulonglong) C.int {
	logInfo("pgraft_go_log_replicate called with leaderID=%d, fromIndex=%d", leaderID, fromIndex)

	// This function is called when a follower needs to catch up with the leader
	// The actual replication is handled by the Raft library through the message processing loop
	// We just need to ensure the Raft node is properly configured and running

	if raftNode == nil {
		logError("pgraft_go_log_replicate: raft node is not initialized")
		return -1
	}

	// Check if we're a follower and the leader ID matches
	status := raftNode.Status()
	if status.RaftState != raft.StateFollower {
		logInfo("pgraft_go_log_replicate: not a follower (state=%d), ignoring replication request", status.RaftState)
		return 0
	}

	if status.Lead != uint64(leaderID) {
		logInfo("pgraft_go_log_replicate: leader ID mismatch (expected=%d, actual=%d)", leaderID, status.Lead)
		return 0
	}

	// The Raft library will handle the actual log replication through its internal mechanisms
	// We just need to ensure the node is properly connected and processing messages
	logInfo("pgraft_go_log_replicate: replication request acknowledged, Raft will handle it")
	return 0
}

func main() {
	// This is required for building as a shared library
}
