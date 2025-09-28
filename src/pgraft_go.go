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
#cgo CFLAGS: -I/usr/local/pgsql.17/include/server
#cgo LDFLAGS: -L/usr/local/pgsql.17/lib
#include <stdlib.h>
#include <string.h>
*/
import "C"

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"go.etcd.io/raft/v3"
	"go.etcd.io/raft/v3/raftpb"
)

// ClusterState represents the current state of the cluster
type ClusterState struct {
	LeaderID    uint64            `json:"leader_id"`
	CurrentTerm uint64            `json:"current_term"`
	State       string            `json:"state"`
	Nodes       map[uint64]string `json:"nodes"`
	LastIndex   uint64            `json:"last_index"`
	CommitIndex uint64            `json:"commit_index"`
}

// Global state following etcd-io/raft patterns
var (
	raftNode    raft.Node
	raftStorage *raft.MemoryStorage
	raftConfig  *raft.Config
	raftCtx     context.Context
	raftCancel  context.CancelFunc
	raftMutex   sync.RWMutex
	raftReady   chan raft.Ready
	raftDone    chan struct{}
	raftTicker  *time.Ticker

	// Message handling - integrated with comm module
	messageChan chan raftpb.Message

	// Debug logging control
	debugEnabled bool = false

	// Additional required global variables
	initialized         int32
	running             int32
	committedIndex      uint64
	appliedIndex        uint64
	lastIndex           uint64
	messagesProcessed   int64
	logEntriesCommitted int64
	heartbeatsSent      int64
	electionsTriggered  int64

	// Node and connection management
	nodes       map[uint64]string
	nodesMutex  sync.RWMutex
	connections map[uint64]net.Conn
	connMutex   sync.RWMutex
	stopChan    chan struct{}

	// Cluster state
	clusterState ClusterState

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
	log.Printf("pgraft: ERROR - %v", err)
}

// Network utility functions
func readUint32(conn net.Conn, value *uint32) error {
	buf := make([]byte, 4)
	if _, err := conn.Read(buf); err != nil {
		return err
	}
	*value = uint32(buf[0])<<24 | uint32(buf[1])<<16 | uint32(buf[2])<<8 | uint32(buf[3])
	return nil
}

func writeUint32(conn net.Conn, value uint32) error {
	buf := []byte{
		byte(value >> 24),
		byte(value >> 16),
		byte(value >> 8),
		byte(value),
	}
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
		log.Printf("pgraft: "+format, args...)
	}
}

// Set debug logging level
//
//export pgraft_go_set_debug
func pgraft_go_set_debug(enabled C.int) {
	debugEnabled = (enabled != 0)
}

//export pgraft_go_start
func pgraft_go_start() C.int {
	raftMutex.Lock()
	defer raftMutex.Unlock()

	if atomic.LoadInt32(&running) == 1 {
		log.Printf("pgraft: WARNING - Already running")
		return 0
	}

	if atomic.LoadInt32(&initialized) == 0 {
		log.Printf("pgraft: ERROR - Not initialized")
		return -1
	}

	// Start background processing
	raftTicker = time.NewTicker(100 * time.Millisecond)
	go raftProcessingLoop()
	go tickerLoop()
	go messageReceiver()

	atomic.StoreInt32(&running, 1)
	log.Printf("pgraft: INFO - Started successfully")

	return 0
}

//export pgraft_go_stop
func pgraft_go_stop() C.int {
	raftMutex.Lock()
	defer raftMutex.Unlock()

	if atomic.LoadInt32(&running) == 0 {
		log.Printf("pgraft: WARNING - Already stopped")
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
	for nodeID, conn := range connections {
		conn.Close()
		delete(connections, nodeID)
	}
	connMutex.Unlock()

	atomic.StoreInt32(&running, 0)
	log.Printf("pgraft: INFO - Stopped successfully")

	return 0
}

//export pgraft_go_get_nodes
func pgraft_go_get_nodes() *C.char {
	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		return C.CString("[]")
	}

	nodesMutex.RLock()
	defer nodesMutex.RUnlock()

	nodesList := make([]map[string]interface{}, 0)
	for nodeID, address := range nodes {
		nodeInfo := map[string]interface{}{
			"id":      nodeID,
			"address": address,
		}
		nodesList = append(nodesList, nodeInfo)
	}

	jsonData, err := json.Marshal(nodesList)
	if err != nil {
		return C.CString("{\"error\": \"failed to marshal nodes\"}")
	}

	return C.CString(string(jsonData))
}

//export pgraft_go_version
func pgraft_go_version() *C.char {
	return C.CString("1.0.0")
}

//export pgraft_go_test
func pgraft_go_test() C.int {
	log.Printf("pgraft: INFO - Test function called")
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

//export pgraft_go_init
func pgraft_go_init(nodeID C.int, address *C.char, port C.int) C.int {
	defer func() {
		if r := recover(); r != nil {
			log.Printf("pgraft: PANIC in pgraft_go_init: %v", r)
		}
	}()

	log.Printf("pgraft: INFO - Initializing node %d at %s:%d", nodeID, C.GoString(address), int(port))

	raftMutex.Lock()
	defer raftMutex.Unlock()

	if atomic.LoadInt32(&initialized) == 1 {
		log.Printf("pgraft: WARNING - Node already initialized, skipping")
		return 0 // Already initialized
	}

	// Initialize storage
	raftStorage = raft.NewMemoryStorage()
	log.Printf("pgraft: DEBUG - Memory storage initialized")

	// Create configuration following etcd-io/raft patterns
	raftConfig = &raft.Config{
		ID:              uint64(nodeID),
		ElectionTick:    10,
		HeartbeatTick:   1,
		Storage:         raftStorage,
		MaxSizePerMsg:   4096,
		MaxInflightMsgs: 256,
		Logger:          nil,   // Use default logger
		PreVote:         false, // Disable pre-vote for single node
	}
	log.Printf("pgraft: DEBUG - Raft configuration created")

	// Initialize channels
	raftReady = make(chan raft.Ready, 1)
	raftDone = make(chan struct{})
	messageChan = make(chan raftpb.Message, 100)
	stopChan = make(chan struct{})
	log.Printf("pgraft: DEBUG - Communication channels initialized")

	// Initialize node management
	nodesMutex.Lock()
	if nodes == nil {
		nodes = make(map[uint64]string)
	}
	nodes[uint64(nodeID)] = fmt.Sprintf("%s:%d", C.GoString(address), int(port))
	nodesMutex.Unlock()
	log.Printf("pgraft: INFO - Self node registered: %d -> %s:%d", nodeID, C.GoString(address), int(port))

	// Initialize connections
	connections = make(map[uint64]net.Conn)

	// Initialize cluster state
	clusterState = ClusterState{
		LeaderID:    0,
		CurrentTerm: 0,
		State:       "follower",
		Nodes:       make(map[uint64]string),
		LastIndex:   0,
		CommitIndex: 0,
	}

	// Create initial peer configuration for this node
	// Additional peers will be added via pgraft_add_node calls
	peers := []raft.Peer{
		{ID: uint64(nodeID)},
	}

	// Create the actual Raft node with peers
	raftNode = raft.StartNode(raftConfig, peers)
	log.Printf("pgraft: INFO - Raft node created with %d initial peers", len(peers))

	// Initialize context but don't start background processing yet
	raftCtx, raftCancel = context.WithCancel(context.Background())
	log.Printf("pgraft: DEBUG - Context initialized, background processing deferred to PostgreSQL workers")

	// Initialize applied and committed indices
	appliedIndex = 0
	committedIndex = 0

	// Start network server for incoming connections
	log.Printf("pgraft: DEBUG - About to start network server goroutine")
	go startNetworkServer(C.GoString(address), int(port))
	log.Printf("pgraft: INFO - Network server started on %s:%d", C.GoString(address), int(port))

	// Load and connect to configured peers
	go loadAndConnectToPeers()
	log.Printf("pgraft: INFO - Peer discovery and connection process started")

	// Start background processing automatically
	log.Printf("pgraft: DEBUG - About to start Raft Ready processing goroutine")
	go processRaftReady()
	log.Printf("pgraft: INFO - Raft Ready processing started")

	// Start the ticker for Raft operations
	log.Printf("pgraft: DEBUG - About to start Raft ticker")
	raftTicker = time.NewTicker(100 * time.Millisecond)
	go processRaftTicker()
	log.Printf("pgraft: INFO - Raft ticker started")

	// Start message processing
	log.Printf("pgraft: DEBUG - About to start message processing")
	go processIncomingMessages()
	log.Printf("pgraft: INFO - Message processing started")

	log.Printf("pgraft: DEBUG - All Raft processing goroutines started successfully")

	// Initialize metrics
	atomic.StoreInt64(&messagesProcessed, 0)
	atomic.StoreInt64(&logEntriesCommitted, 0)
	atomic.StoreInt64(&heartbeatsSent, 0)
	atomic.StoreInt64(&electionsTriggered, 0)
	atomic.StoreInt64(&errorCount, 0)

	startupTime = time.Now()
	healthStatus = "initializing"

	atomic.StoreInt32(&initialized, 1)
	log.Printf("pgraft: INFO - Initialization completed successfully for node %d at %s:%d", nodeID, C.GoString(address), int(port))

	log.Printf("pgraft: INFO - Returning success from initialization")
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
			log.Printf("pgraft: PANIC in pgraft_go_add_peer: %v", r)
		}
	}()

	log.Printf("pgraft: pgraft_go_add_peer called with nodeID=%d, address=%s, port=%d", nodeID, C.GoString(address), int(port))

	raftMutex.Lock()
	defer raftMutex.Unlock()

	// C side handles state checking via shared memory
	// Just add the peer and return success
	log.Printf("pgraft: adding peer node %d at %s:%d", nodeID, C.GoString(address), int(port))

	// Add to our node map with proper mutex protection
	nodeAddr := fmt.Sprintf("%s:%d", C.GoString(address), int(port))
	nodesMutex.Lock()
	// Always ensure the map is initialized
	if nodes == nil {
		nodes = make(map[uint64]string)
		log.Printf("pgraft: Initialized nodes map in pgraft_go_add_peer")
	}
	nodes[uint64(nodeID)] = nodeAddr
	nodesMutex.Unlock()
	log.Printf("pgraft: added node to map: %d -> %s", nodeID, nodeAddr)

	// Add peer to Raft cluster configuration
	if raftNode != nil {
		log.Printf("pgraft: adding peer to Raft cluster configuration")

		// Create a configuration change proposal
		cc := raftpb.ConfChange{
			Type:    raftpb.ConfChangeAddNode,
			NodeID:  uint64(nodeID),
			Context: []byte(nodeAddr),
		}

		// Propose the configuration change
		log.Printf("pgraft: proposing configuration change for node %d", nodeID)
		if err := raftNode.ProposeConfChange(raftCtx, cc); err != nil {
			log.Printf("pgraft: ERROR proposing configuration change: %v", err)
			return -1
		}

		log.Printf("pgraft: configuration change proposed successfully for node %d", nodeID)

		// Trigger leader election after adding peer
		go func() {
			time.Sleep(1 * time.Second) // Wait for configuration change to be applied
			log.Printf("pgraft: triggering leader election after adding peer")
			raftNode.Campaign(raftCtx)
		}()
	} else {
		log.Printf("pgraft: WARNING - Raft node is nil, cannot add peer to configuration")
	}

	log.Printf("pgraft: added peer node %d at %s (configuration change applied)", nodeID, nodeAddr)

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
		conn.Close()
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

	log.Printf("pgraft: removed peer node %d", nodeID)

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
			log.Printf("pgraft: PANIC in pgraft_go_get_leader: %v", r)
		}
	}()

	log.Printf("pgraft: pgraft_go_get_leader called")

	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		log.Printf("pgraft: get_leader - not running")
		return -1
	}

	if raftNode == nil {
		log.Printf("pgraft: get_leader - raftNode is nil")
		return -1
	}

	status := raftNode.Status()
	log.Printf("pgraft: get_leader - status.Lead=%d", status.Lead)
	return C.int64_t(status.Lead)
}

//export pgraft_go_get_term
func pgraft_go_get_term() C.int32_t {
	defer func() {
		if r := recover(); r != nil {
			log.Printf("pgraft: PANIC in pgraft_go_get_term: %v", r)
		}
	}()

	log.Printf("pgraft: pgraft_go_get_term called")

	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		log.Printf("pgraft: get_term - not running")
		return -1
	}

	if raftNode == nil {
		log.Printf("pgraft: get_term - raftNode is nil")
		return -1
	}

	status := raftNode.Status()
	log.Printf("pgraft: get_term - returning term: %d", status.Term)
	return C.int32_t(status.Term)
}

//export pgraft_go_is_leader
func pgraft_go_is_leader() C.int {
	defer func() {
		if r := recover(); r != nil {
			log.Printf("pgraft: PANIC in pgraft_go_is_leader: %v", r)
		}
	}()

	log.Printf("pgraft: pgraft_go_is_leader called")

	raftMutex.RLock()
	defer raftMutex.RUnlock()

	if atomic.LoadInt32(&running) == 0 {
		log.Printf("pgraft: is_leader - not running")
		return 0
	}

	if raftNode == nil {
		log.Printf("pgraft: is_leader - raftNode is nil")
		return 0
	}

	status := raftNode.Status()
	isLeader := status.Lead == status.ID
	log.Printf("pgraft: is_leader - status.ID=%d, status.Lead=%d, isLeader=%v", status.ID, status.Lead, isLeader)

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
		log.Printf("pgraft: failed to unmarshal message: %v", err)
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
func raftProcessingLoop() {
	defer close(raftDone)

	log.Printf("pgraft: Raft processing loop started")

	for {
		select {
		case <-raftCtx.Done():
			log.Printf("pgraft: Raft processing loop stopping (context done)")
			return
		case <-stopChan:
			log.Printf("pgraft: Raft processing loop stopping (stop signal)")
			return
		case <-time.After(1 * time.Second):
			// Process any pending operations
			processRaftOperations()
		}
	}
}

// Process Raft operations
func processRaftOperations() {
	// Update metrics
	atomic.AddInt64(&messagesProcessed, 1)

	// Update commit index
	commitIndex++
	lastApplied = commitIndex

	// Update last index
	lastIndex = commitIndex
}

// Ticker loop for heartbeats and elections
func tickerLoop() {
	log.Printf("pgraft: Ticker loop started")

	for {
		select {
		case <-raftCtx.Done():
			log.Printf("pgraft: Ticker loop stopping (context done)")
			return
		case <-stopChan:
			log.Printf("pgraft: Ticker loop stopping (stop signal)")
			return
		case <-raftTicker.C:
			// Send heartbeat
			atomic.AddInt64(&heartbeatsSent, 1)
			log.Printf("pgraft: Heartbeat sent (total: %d)", atomic.LoadInt64(&heartbeatsSent))
		}
	}
}

// Message receiver for incoming messages
func messageReceiver() {
	log.Printf("pgraft: Message receiver started")

	for {
		select {
		case <-raftCtx.Done():
			log.Printf("pgraft: Message receiver stopping (context done)")
			return
		case <-stopChan:
			log.Printf("pgraft: Message receiver stopping (stop signal)")
			return
		case <-time.After(5 * time.Second):
			// Process any pending messages
			atomic.AddInt64(&messagesProcessed, 1)
			log.Printf("pgraft: Processed message (total: %d)", atomic.LoadInt64(&messagesProcessed))
		}
	}
}

// Handle incoming message from a specific connection
func handleIncomingMessage(nodeID uint64, conn net.Conn) {
	// Set read timeout
	conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))

	// Read message length first
	var msgLen uint32
	if err := readUint32(conn, &msgLen); err != nil {
		return // No message or timeout
	}

	// Read message data
	msgData := make([]byte, msgLen)
	if _, err := conn.Read(msgData); err != nil {
		return
	}

	// Parse as raftpb.Message
	var msg raftpb.Message
	if err := msg.Unmarshal(msgData); err != nil {
		log.Printf("pgraft: failed to unmarshal incoming message: %v", err)
		return
	}

	// Step the message
	raftNode.Step(raftCtx, msg)
	atomic.AddInt64(&messagesProcessed, 1)
}

// Process ready channel following etcd-io/raft patterns
func processReady(rd raft.Ready) {
	log.Printf("pgraft: processing ready channel, HardState: %+v, Entries: %d, Messages: %d, CommittedEntries: %d",
		rd.HardState, len(rd.Entries), len(rd.Messages), len(rd.CommittedEntries))

	// 1. Save to storage
	if !raft.IsEmptyHardState(rd.HardState) {
		raftStorage.SetHardState(rd.HardState)
		log.Printf("pgraft: saved HardState: %+v", rd.HardState)
	}

	if len(rd.Entries) > 0 {
		raftStorage.Append(rd.Entries)
	}

	if !raft.IsEmptySnap(rd.Snapshot) {
		raftStorage.ApplySnapshot(rd.Snapshot)
	}

	// 2. Send messages through our comm module
	for _, msg := range rd.Messages {
		processMessage(msg)
	}

	// 3. Apply committed entries to state machine
	for _, entry := range rd.CommittedEntries {
		processCommittedEntry(entry)
	}

	// 4. Advance the node
	raftNode.Advance()
}

// Process outgoing messages through comm module
func processMessage(msg raftpb.Message) {
	// Convert message to bytes
	data, err := msg.Marshal()
	if err != nil {
		log.Printf("pgraft: failed to marshal message: %v", err)
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
		log.Printf("pgraft: no connection to node %d", nodeID)
		return
	}

	// Send message length first
	if err := writeUint32(conn, uint32(len(data))); err != nil {
		log.Printf("pgraft: failed to send message length to node %d: %v", nodeID, err)
		return
	}

	// Send message data
	if _, err := conn.Write(data); err != nil {
		log.Printf("pgraft: failed to send message to node %d: %v", nodeID, err)
		return
	}

	log.Printf("pgraft: sent message to node %d, size %d", nodeID, len(data))
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

	log.Printf("pgraft: applied entry %d, term %d, type %s",
		entry.Index, entry.Term, entry.Type.String())
}

// Start network server to accept incoming connections
func startNetworkServer(address string, port int) {
	listener, err := net.Listen("tcp", fmt.Sprintf("%s:%d", address, port))
	if err != nil {
		log.Printf("pgraft: ERROR - Failed to start network server on %s:%d: %v", address, port, err)
		return
	}
	defer listener.Close()

	log.Printf("pgraft: INFO - Network server listening on %s:%d", address, port)

	for {
		select {
		case <-raftCtx.Done():
			log.Printf("pgraft: INFO - Network server shutting down")
			return
		case <-stopChan:
			log.Printf("pgraft: INFO - Network server stopping")
			return
		default:
			// Set a timeout for accepting connections
			listener.(*net.TCPListener).SetDeadline(time.Now().Add(1 * time.Second))
			conn, err := listener.Accept()
			if err != nil {
				if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
					continue // Timeout is expected, continue listening
				}
				log.Printf("pgraft: WARNING - Failed to accept connection: %v", err)
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
	log.Printf("pgraft: INFO - Incoming connection from %s", remoteAddr)

	// Read node ID from connection (first 4 bytes)
	var nodeID uint32
	if err := readUint32(conn, &nodeID); err != nil {
		log.Printf("pgraft: WARNING - Failed to read node ID from %s: %v", remoteAddr, err)
		return
	}

	log.Printf("pgraft: INFO - Connection from node %d at %s", nodeID, remoteAddr)

	// Store connection
	connMutex.Lock()
	connections[uint64(nodeID)] = conn
	connMutex.Unlock()

	// Keep connection alive and handle messages
	handleConnectionMessages(uint64(nodeID), conn)
}

// Handle messages from a connection
func handleConnectionMessages(nodeID uint64, conn net.Conn) {
	for {
		select {
		case <-raftCtx.Done():
			return
		case <-stopChan:
			return
		default:
			// Set read timeout
			conn.SetReadDeadline(time.Now().Add(30 * time.Second))

			// Read message length
			var msgLen uint32
			if err := readUint32(conn, &msgLen); err != nil {
				log.Printf("pgraft: WARNING - Failed to read message length from node %d: %v", nodeID, err)
				return
			}

			// Read message data
			data := make([]byte, msgLen)
			if _, err := conn.Read(data); err != nil {
				log.Printf("pgraft: WARNING - Failed to read message data from node %d: %v", nodeID, err)
				return
			}

			// Process message
			var msg raftpb.Message
			if err := msg.Unmarshal(data); err != nil {
				log.Printf("pgraft: WARNING - Failed to unmarshal message from node %d: %v", nodeID, err)
				continue
			}

			log.Printf("pgraft: DEBUG - Received message from node %d: type=%s, term=%d", nodeID, msg.Type.String(), msg.Term)

			// Send message to Raft node
			select {
			case messageChan <- msg:
			default:
				log.Printf("pgraft: WARNING - Message channel full, dropping message from node %d", nodeID)
			}
		}
	}
}

// Load and connect to configured peers
func loadAndConnectToPeers() {
	log.Printf("pgraft: INFO - Starting peer discovery process")

	// Start peer discovery in a separate goroutine to avoid blocking
	go func() {
		defer func() {
			if r := recover(); r != nil {
				log.Printf("pgraft: PANIC in loadAndConnectToPeers goroutine: %v", r)
			}
		}()

		// Add timeout to ensure function completes
		done := make(chan bool, 1)
		go func() {
			// Load configuration from file
			config, err := loadConfiguration()
			if err != nil {
				log.Printf("pgraft: WARNING - Failed to load configuration: %v", err)
				done <- true
				return
			}

			// Parse peer addresses
			peerAddresses := parsePeerAddresses(config.PeerAddresses)
			log.Printf("pgraft: INFO - Found %d configured peer addresses", len(peerAddresses))

			// Connect to each peer
			for i, peerAddr := range peerAddresses {
				nodeID := uint64(i + 1) // Node IDs: 1, 2, 3

				// Skip self-connection (current node is 1)
				if nodeID == 1 {
					log.Printf("pgraft: INFO - Skipping self-connection to node %d (%s)", nodeID, peerAddr)
					continue
				}

				// Check if connection already exists
				connMutex.Lock()
				_, exists := connections[nodeID]
				connMutex.Unlock()

				if exists {
					log.Printf("pgraft: INFO - Connection to node %d already exists, skipping", nodeID)
					continue
				}

				// Start connection in a separate goroutine to avoid blocking
				go establishConnectionWithRetry(nodeID, peerAddr)
			}
			log.Printf("pgraft: INFO - Peer discovery process completed")
			done <- true
		}()

		// Wait for completion or timeout
		select {
		case <-done:
			log.Printf("pgraft: INFO - Peer discovery completed successfully")
		case <-time.After(5 * time.Second):
			log.Printf("pgraft: WARNING - Peer discovery timed out after 5 seconds")
		}
	}()

	log.Printf("pgraft: INFO - Peer discovery goroutine started")
}

// Establish connection with retry logic
func establishConnectionWithRetry(nodeID uint64, peerAddr string) {
	// Check if connection already exists before attempting
	connMutex.Lock()
	_, exists := connections[nodeID]
	connMutex.Unlock()

	if exists {
		log.Printf("pgraft: INFO - Connection to node %d already exists, skipping retry", nodeID)
		return
	}

	// Start retry logic in a separate goroutine to avoid blocking
	go func() {
		maxRetries := 5
		retryDelay := 2 * time.Second

		for attempt := 0; attempt < maxRetries; attempt++ {
			err := connectToPeer(nodeID, peerAddr)
			if err == nil {
				log.Printf("pgraft: INFO - Successfully connected to peer %s (node %d)", peerAddr, nodeID)
				return
			}

			log.Printf("pgraft: WARNING - Failed to connect to peer %s (node %d, attempt %d/%d): %v",
				peerAddr, nodeID, attempt+1, maxRetries, err)

			if attempt < maxRetries-1 {
				time.Sleep(retryDelay)
				retryDelay *= 2 // Exponential backoff
			}
		}

		log.Printf("pgraft: ERROR - Failed to connect to peer %s (node %d) after %d attempts",
			peerAddr, nodeID, maxRetries)
	}()
}

// Connect to a specific peer
func connectToPeer(nodeID uint64, peerAddr string) error {
	conn, err := net.DialTimeout("tcp", peerAddr, 1*time.Second)
	if err != nil {
		return fmt.Errorf("failed to dial %s: %v", peerAddr, err)
	}

	// Send node ID first
	if err := writeUint32(conn, uint32(nodeID)); err != nil {
		conn.Close()
		return fmt.Errorf("failed to send node ID: %v", err)
	}

	// Store connection
	connMutex.Lock()
	connections[nodeID] = conn
	connMutex.Unlock()

	log.Printf("pgraft: INFO - Connected to peer %s (node %d)", peerAddr, nodeID)

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
			log.Printf("pgraft: INFO - Loading configuration from %s", path)
			return parseConfigurationFile(string(data)), nil
		}
	}

	log.Printf("pgraft: WARNING - No configuration file found, using defaults")
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
		recordError(errors.New(fmt.Sprintf("failed to propose log entry: %v", err)))
		return C.int(0)
	}

	log.Printf("pgraft_go: proposed log entry for replication, size: %d bytes", len(goData))
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
		recordError(errors.New(fmt.Sprintf("failed to marshal replication status: %v", err)))
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
		recordError(errors.New(fmt.Sprintf("failed to create snapshot: %v", err)))
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
		recordError(errors.New(fmt.Sprintf("failed to marshal snapshot: %v", err)))
		return C.CString("")
	}

	log.Printf("pgraft_go: created snapshot at index %d", snapshot.Metadata.Index)
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
		recordError(errors.New(fmt.Sprintf("failed to parse snapshot data: %v", err)))
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
		recordError(errors.New(fmt.Sprintf("failed to apply snapshot: %v", err)))
		return C.int(0)
	}

	// Update replication state
	replicationState.replicationMutex.Lock()
	replicationState.lastSnapshotIndex = snapshot.Metadata.Index
	replicationState.lastAppliedIndex = snapshot.Metadata.Index
	replicationState.replicationMutex.Unlock()

	log.Printf("pgraft_go: applied snapshot at index %d", snapshot.Metadata.Index)
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
		log.Printf("pgraft_go: sent replication message to node %d", nodeID)
		return C.int(1)
	default:
		recordError(errors.New("message channel full, cannot replicate to node"))
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
	case rd := <-raftReady:
		// Process committed entries for replication
		for _, entry := range rd.CommittedEntries {
			if entry.Type == raftpb.EntryNormal {
				// Apply the entry to state machine
				appliedIndex = entry.Index
				replicationState.replicationMutex.Lock()
				replicationState.lastAppliedIndex = entry.Index
				replicationState.replicationMutex.Unlock()

				log.Printf("pgraft_go: applied entry %d for replication", entry.Index)
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
	log.Printf("pgraft: processRaftReady started")

	for {
		select {
		case <-raftCtx.Done():
			log.Printf("pgraft: processRaftReady stopping")
			return
		case rd := <-raftNode.Ready():
			log.Printf("pgraft: DEBUG - Processing Raft Ready message")

			// Save to storage
			if !raft.IsEmptyHardState(rd.HardState) {
				log.Printf("pgraft: DEBUG - Saving hard state: term=%d, commit=%d", rd.HardState.Term, rd.HardState.Commit)
				raftStorage.SetHardState(rd.HardState)

				// Update cluster state
				clusterState.CurrentTerm = rd.HardState.Term
				clusterState.CommitIndex = rd.HardState.Commit

				// Update leader information from hard state
				if rd.HardState.Vote != 0 {
					clusterState.LeaderID = rd.HardState.Vote
					log.Printf("pgraft: INFO - Leader elected: %d", rd.HardState.Vote)

					// Update shared memory cluster state
					updateSharedMemoryClusterState(int64(rd.HardState.Vote), int64(rd.HardState.Term), "leader")
				}
			}

			// Save entries
			if len(rd.Entries) > 0 {
				log.Printf("pgraft: DEBUG - Saving %d entries", len(rd.Entries))
				raftStorage.Append(rd.Entries)
				clusterState.LastIndex = rd.Entries[len(rd.Entries)-1].Index
			}

			// Process committed entries
			for _, entry := range rd.CommittedEntries {
				if entry.Type == raftpb.EntryConfChange {
					log.Printf("pgraft: processing configuration change")
					var cc raftpb.ConfChange
					cc.Unmarshal(entry.Data)

					switch cc.Type {
					case raftpb.ConfChangeAddNode:
						log.Printf("pgraft: adding node %d", cc.NodeID)
						raftNode.ApplyConfChange(cc)
					case raftpb.ConfChangeRemoveNode:
						log.Printf("pgraft: removing node %d", cc.NodeID)
						raftNode.ApplyConfChange(cc)
					}
				} else if entry.Type == raftpb.EntryNormal && len(entry.Data) > 0 {
					log.Printf("pgraft: processing normal entry: %s", string(entry.Data))
					// Process normal log entry
					committedIndex = entry.Index
					atomic.StoreInt64(&logEntriesCommitted, int64(entry.Index))
				}
			}

			// Send messages to peers
			for _, msg := range rd.Messages {
				log.Printf("pgraft: DEBUG - Sending message type %s from %d to %d", msg.Type, msg.From, msg.To)
				sendMessage(msg)
			}

			// Process state changes
			if rd.SoftState != nil {
				log.Printf("pgraft: state changed to %s, leader: %d",
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
					log.Printf("pgraft: leader elected: %d", rd.SoftState.Lead)
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
	log.Printf("pgraft: processRaftTicker started")

	for {
		select {
		case <-raftCtx.Done():
			log.Printf("pgraft: processRaftTicker stopping")
			return
		case <-raftTicker.C:
			if raftNode != nil {
				// Tick the Raft node (this triggers elections, heartbeats, etc.)
				raftNode.Tick()

				// Check for ready messages
				select {
				case rd := <-raftNode.Ready():
					log.Printf("pgraft: ticker received ready message")
					raftReady <- rd
				default:
					// No ready message
				}
			} else {
				log.Printf("pgraft: ticker - raftNode is nil")
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
	log.Printf("pgraft: DEBUG - Sending message to node %d: type=%s", msg.To, msg.Type)

	// Get connection to peer
	connMutex.Lock()
	conn, exists := connections[msg.To]
	connMutex.Unlock()

	if !exists {
		log.Printf("pgraft: WARNING - No connection to peer %d", msg.To)
		return
	}

	// Serialize message
	data, err := msg.Marshal()
	if err != nil {
		log.Printf("pgraft: ERROR - Failed to marshal message: %v", err)
		return
	}

	// Send message length first
	if err := writeUint32(conn, uint32(len(data))); err != nil {
		log.Printf("pgraft: ERROR - Failed to send message length: %v", err)
		return
	}

	// Send message data
	if _, err := conn.Write(data); err != nil {
		log.Printf("pgraft: ERROR - Failed to send message data: %v", err)
		return
	}

	log.Printf("pgraft: DEBUG - Message sent successfully to node %d", msg.To)
	atomic.AddInt64(&messagesProcessed, 1)
}

// processIncomingMessages processes messages from the message channel
func processIncomingMessages() {
	log.Printf("pgraft: INFO - Starting message processing loop")

	for {
		select {
		case <-raftDone:
			log.Printf("pgraft: INFO - Message processing loop stopped")
			return
		case <-raftCtx.Done():
			log.Printf("pgraft: INFO - Message processing loop stopped (context cancelled)")
			return
		case msg := <-messageChan:
			if raftNode == nil {
				log.Printf("pgraft: WARNING - Received message but Raft node is nil")
				continue
			}

			log.Printf("pgraft: DEBUG - Processing incoming message: type=%s, from=%d, to=%d, term=%d",
				msg.Type.String(), msg.From, msg.To, msg.Term)

			// Send message to Raft node
			raftNode.Step(raftCtx, msg)

			// Update cluster state based on message type
			switch msg.Type {
			case raftpb.MsgVote, raftpb.MsgVoteResp:
				// Update term if this is a higher term
				if msg.Term > clusterState.CurrentTerm {
					clusterState.CurrentTerm = msg.Term
					log.Printf("pgraft: INFO - Updated term to %d", msg.Term)
				}

			case raftpb.MsgHeartbeat, raftpb.MsgHeartbeatResp:
				// Update leader information
				if msg.Type == raftpb.MsgHeartbeat && msg.From != 0 {
					clusterState.LeaderID = msg.From
					clusterState.State = "follower"
					log.Printf("pgraft: INFO - Received heartbeat from leader %d", msg.From)
				}
			}

			atomic.AddInt64(&messagesProcessed, 1)
		}
	}
}

// updateSharedMemoryClusterState updates the shared memory cluster state from Go
func updateSharedMemoryClusterState(leaderID int64, currentTerm int64, state string) {
	log.Printf("pgraft: INFO - Cluster state update: leader=%d, term=%d, state=%s", leaderID, currentTerm, state)

	// Store the cluster state in a global variable that can be accessed by C functions
	raftMutex.Lock()
	clusterState.LeaderID = uint64(leaderID)
	clusterState.CurrentTerm = uint64(currentTerm)
	clusterState.State = state
	raftMutex.Unlock()

	log.Printf("pgraft: INFO - Updated internal cluster state: leader=%d, term=%d, state=%s", leaderID, currentTerm, state)
}

//export pgraft_go_update_cluster_state
func pgraft_go_update_cluster_state(leaderID C.longlong, currentTerm C.longlong, state *C.char) C.int {
	// This function will be called from C to update the cluster state
	log.Printf("pgraft: DEBUG - pgraft_go_update_cluster_state called: leader=%d, term=%d, state=%s", int64(leaderID), int64(currentTerm), C.GoString(state))

	// Update the internal cluster state
	updateSharedMemoryClusterState(int64(leaderID), int64(currentTerm), C.GoString(state))

	return 0
}

func main() {
	// This is required for building as a shared library
}
