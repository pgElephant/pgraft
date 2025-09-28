#!/usr/bin/env python3
"""
pgraft_cluster.py - Modular PostgreSQL pgraft Cluster Management Script

This script provides a clean, modular interface for managing a three-node
PostgreSQL cluster with pgraft consensus. It supports initialization,
verification, destruction, and status monitoring.

Usage:
    python pgraft_cluster.py --init     # Create three-node cluster
    python pgraft_cluster.py --verify   # Verify cluster health
    python pgraft_cluster.py --destroy  # Destroy cluster
    python pgraft_cluster.py --status   # Show cluster status

Author: pgElephant Team
License: MIT
"""

import os
import sys
import time
import signal
import subprocess
import argparse
import json
import psycopg2
import getpass
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from contextlib import contextmanager


@dataclass
class NodeConfig:
    """Configuration for a PostgreSQL node with pgraft"""
    name: str
    port: int
    pgraft_port: int
    data_dir: str
    config_file: str
    metrics_port: int


class PgraftClusterManager:
    """Modular PostgreSQL pgraft cluster management"""
    
    def __init__(self, base_dir: str = "/tmp/pgraft", verbose: int = 0):
        self.base_dir = Path(base_dir)
        # Create log directory in current working directory
        self.log_dir = Path.cwd() / "logs"
        self.verbose = verbose
        self.current_user = getpass.getuser()
        self.nodes = {
            'primary1': NodeConfig(
                name='primary1',
                port=5432,
                pgraft_port=7001,
                data_dir=str(self.base_dir / 'primary1'),
                config_file='primary1.conf',
                metrics_port=9091
            ),
            'replica1': NodeConfig(
                name='replica1',
                port=5433,
                pgraft_port=7002,
                data_dir=str(self.base_dir / 'replica1'),
                config_file='replica1.conf',
                metrics_port=9092
            ),
            'replica2': NodeConfig(
                name='replica2',
                port=5434,
                pgraft_port=7003,
                data_dir=str(self.base_dir / 'replica2'),
                config_file='replica2.conf',
                metrics_port=9093
            )
        }
        self.processes: Dict[str, subprocess.Popen] = {}
        self.script_dir = Path(__file__).parent
        
    def log(self, message: str, level: str = "INFO", verbose_level: int = 0) -> None:
        """Log message with timestamp and verbose level control"""
        if verbose_level > self.verbose:
            return
        
        # Skip warnings in non-verbose mode
        if level == "WARN" and self.verbose == 0:
            return
        
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        
        # ANSI color codes
        GREEN = '\033[92m'
        RED = '\033[91m'
        YELLOW = '\033[93m'
        RESET = '\033[0m'
        
        if self.verbose == 0:
            # Verbose level 0: Show color-coded symbols with datetime
            if level == "INFO":
                symbol = f"{GREEN}✓{RESET}"  # Green for success
            elif level == "ERROR":
                symbol = f"{RED}✗{RESET}"  # Red for errors
            elif level == "WARN":
                symbol = f"{YELLOW}⚠{RESET}"  # Yellow for warnings (but skipped in non-verbose)
            else:
                symbol = "•"
            print(f"{symbol} {timestamp}: {message}")
        else:
            # Verbose level 1+: Show all logs with level indicators
            if verbose_level > 0:
                print(f"[{timestamp}] [{level}] [V{verbose_level}] {message}")
            else:
                print(f"[{timestamp}] [{level}] {message}")
    
    def check_prerequisites(self) -> None:
        """Check if required PostgreSQL tools are available"""
        required_commands = ['pg_ctl', 'initdb', 'psql']
        
        for cmd in required_commands:
            try:
                result = subprocess.run(['which', cmd], capture_output=True, text=True)
                if result.returncode != 0:
                    self.log(f"✗ {cmd} not found in PATH", "ERROR")
                    self.log("Please install PostgreSQL and ensure it's in your PATH", "ERROR")
                    sys.exit(1)
                self.log(f"✓ Found {cmd}: {result.stdout.strip()}", verbose_level=1)
            except Exception as e:
                self.log(f"✗ {cmd} not found: {e}", "ERROR")
                self.log("Please install PostgreSQL and ensure it's in your PATH", "ERROR")
                sys.exit(1)
        
        # Check PostgreSQL version
        try:
            result = subprocess.run(['pg_ctl', '--version'], capture_output=True, text=True)
            if result.returncode == 0:
                self.log(f"PostgreSQL version: {result.stdout.strip()}", verbose_level=1)
        except Exception as e:
            self.log(f"Warning: Could not get PostgreSQL version: {e}", "WARN")
    
    def check_port_availability(self) -> None:
        """Check if required ports are available"""
        self.log("Checking port availability...")
        
        # Check all primary ports first
        primary_node = self.nodes['primary1']
        if self.is_port_in_use(primary_node.port):
            self.log(f"✗ [primary1] - Port {primary_node.port} is already in use (PostgreSQL)", "ERROR")
            self.log(f"Please stop any PostgreSQL instance running on port {primary_node.port}", "ERROR")
            sys.exit(1)
        else:
            self.log(f"[primary1] - Port {primary_node.port} available PostgreSQL")
        
        if self.is_port_in_use(primary_node.pgraft_port):
            self.log(f"✗ [primary1] - Port {primary_node.pgraft_port} is already in use (pgraft)", "ERROR")
            sys.exit(1)
        else:
            self.log(f"[primary1] - Port {primary_node.pgraft_port} available pgraft")
        
        # Check all replica ports
        for node_name, node in self.nodes.items():
            if "replica" in node_name:
                if self.is_port_in_use(node.port):
                    self.log(f"✗ [{node_name}] - Port {node.port} is already in use (PostgreSQL)", "ERROR")
                    sys.exit(1)
                else:
                    self.log(f"[{node_name}] - Port {node.port} available PostgreSQL")
                
                if self.is_port_in_use(node.pgraft_port):
                    self.log(f"✗ [{node_name}] - Port {node.pgraft_port} is already in use (pgraft)", "ERROR")
                    sys.exit(1)
                else:
                    self.log(f"[{node_name}] - Port {node.pgraft_port} available pgraft")
    
    def is_port_in_use(self, port: int) -> bool:
        """Check if a port is in use"""
        import socket
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(1)
                result = s.connect_ex(('localhost', port))
                return result == 0
        except Exception:
            return False

    def run_command(self, cmd: List[str], check: bool = True, capture: bool = False) -> subprocess.CompletedProcess:
        """Run shell command with error handling"""
        self.log(f"Running: {' '.join(cmd)}", verbose_level=1)
        try:
            # Set environment variables to prevent threading issues
            env = os.environ.copy()
            env['LC_ALL'] = 'C'
            env['LANG'] = 'C'
            
            result = subprocess.run(
                cmd, 
                check=check, 
                capture_output=capture,
                text=True,
                env=env
            )
            return result
        except subprocess.CalledProcessError as e:
            # Don't log here - let the calling method handle the error logging
            if e.stdout and self.verbose > 0:
                self.log(f"STDOUT: {e.stdout}", "ERROR", verbose_level=1)
            if e.stderr and self.verbose > 0:
                self.log(f"STDERR: {e.stderr}", "ERROR", verbose_level=1)
            raise
    
    def create_directories(self) -> None:
        """Create necessary directories for cluster"""
        # Remove existing log directory if it exists and create fresh
        if self.log_dir.exists():
            import shutil
            shutil.rmtree(self.log_dir)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        
        for node in self.nodes.values():
            # Create data directory
            Path(node.data_dir).mkdir(parents=True, exist_ok=True)
            
            # Create node-specific log directory in main logs directory
            node_log_dir = self.log_dir / node.name
            node_log_dir.mkdir(parents=True, exist_ok=True)
            
            # Create empty log files
            postgresql_log = node_log_dir / 'postgresql.log'
            pgraft_log = node_log_dir / 'pgraft.log'
            postgresql_log.touch()
            pgraft_log.touch()
            
            # Create pgraft directory
            pgraft_dir = Path(node.data_dir).parent / node.name
            pgraft_dir.mkdir(parents=True, exist_ok=True)
            
        self.log("Directories created successfully")
    
    def init_database(self, node: NodeConfig) -> None:
        """Initialize PostgreSQL database for a node"""
        self.log(f"[{node.name}] - Initializing database...")
        
        # Remove existing data directory if it exists
        data_path = Path(node.data_dir)
        if data_path.exists():
            self.log(f"Removing existing data directory: {node.data_dir}", verbose_level=1)
            self.run_command(['rm', '-rf', str(data_path)])
        
        # Run initdb
        initdb_cmd = [
            'initdb',
            '-D', node.data_dir,
            '--auth-local=trust',
            '--auth-host=trust',
            '--encoding=UTF-8',
            '--locale=C'
        ]
        
        # Capture initdb output and only show in verbose mode
        if self.verbose > 0:
            self.run_command(initdb_cmd)
        else:
            self.run_command(initdb_cmd, capture=True)
        
        # Copy configuration file and update log paths
        config_src = self.script_dir / node.config_file
        config_dst = Path(node.data_dir) / 'postgresql.conf'
        
        if config_src.exists():
            # Read the original config
            with open(config_src, 'r') as f:
                config_content = f.read()
            
            # Update log directory paths to use our centralized log directory
            node_log_dir = self.log_dir / node.name
            config_content = config_content.replace(
                f'/tmp/pgraft/{node.name}/logs',
                str(node_log_dir)
            )
            config_content = config_content.replace(
                f'/tmp/pgraft/{node.name}/pgraft.log',
                str(node_log_dir / 'pgraft.log')
            )
            
            # Write the updated config
            with open(config_dst, 'w') as f:
                f.write(config_content)
        else:
            self.log(f"[{node.name}] - Warning: Configuration file {config_src} not found", "WARN")
    
    def start_node(self, node: NodeConfig) -> None:
        """Start a PostgreSQL node"""
        self.log(f"[{node.name}] - Starting...")
        
        # Start PostgreSQL with log file in centralized log directory
        node_log_dir = self.log_dir / node.name
        log_file = node_log_dir / 'postgresql.log'
        
        pg_ctl_cmd = [
            'pg_ctl',
            '-D', node.data_dir,
            '-l', str(log_file),
            'start'
        ]
        
        try:
            # Capture output to avoid showing pg_ctl messages in non-verbose mode
            if self.verbose > 0:
                self.run_command(pg_ctl_cmd)
            else:
                self.run_command(pg_ctl_cmd, capture=True)
            
            # Wait a moment for PostgreSQL to start
            import time
            time.sleep(5)
            
            # Check if PostgreSQL is actually running
            if self.is_port_in_use(node.port):
                self.log(f"[{node.name}] - Started successfully")
                # Wait for node to be ready
                self.wait_for_node(node)
            else:
                # PostgreSQL didn't start, check logs for errors
                error_message = "PostgreSQL failed to start"
                log_file = self.log_dir / node.name / 'postgresql.log'
                if log_file.exists():
                    try:
                        with open(log_file, 'r') as f:
                            lines = f.readlines()
                            for line in lines:
                                if 'FATAL:' in line or 'ERROR:' in line:
                                    # Extract just the error message part
                                    if 'FATAL:' in line:
                                        error_message = line.split('FATAL:')[1].strip()
                                    elif 'ERROR:' in line:
                                        error_message = line.split('ERROR:')[1].strip()
                                    break
                    except Exception:
                        pass
                
                self.log(f"[{node.name}] - Failed to start: {error_message}", "ERROR")
                sys.exit(1)
            
        except subprocess.CalledProcessError as e:
            # pg_ctl failed, but check if PostgreSQL is actually running
            if self.is_port_in_use(node.port):
                self.log(f"[{node.name}] - Started successfully (pg_ctl returned {e.returncode} but server is running)")
                # Wait for node to be ready
                self.wait_for_node(node)
            else:
                # Show first error from PostgreSQL log
                error_message = f"pg_ctl failed with exit code {e.returncode}"
                log_file = self.log_dir / node.name / 'postgresql.log'
                if log_file.exists():
                    try:
                        with open(log_file, 'r') as f:
                            lines = f.readlines()
                            for line in lines:
                                if 'FATAL:' in line or 'ERROR:' in line:
                                    # Extract just the error message part
                                    if 'FATAL:' in line:
                                        error_message = line.split('FATAL:')[1].strip()
                                    elif 'ERROR:' in line:
                                        error_message = line.split('ERROR:')[1].strip()
                                    break
                    except Exception:
                        pass
                
                self.log(f"[{node.name}] - Failed to start: {error_message}", "ERROR")
                sys.exit(1)
    
    def stop_node(self, node: NodeConfig) -> None:
        """Stop a PostgreSQL node"""
        # Check if PostgreSQL is running on this port
        if not self.is_port_in_use(node.port):
            self.log(f"[{node.name}] - Not running on port {node.port}, skipping stop")
            return
        
        self.log(f"[{node.name}] - Stopping...")
        
        pg_ctl_cmd = [
            'pg_ctl',
            '-D', node.data_dir,
            'stop'
        ]
        
        try:
            # Capture output to avoid showing pg_ctl messages in non-verbose mode
            if self.verbose > 0:
                self.run_command(pg_ctl_cmd)
            else:
                self.run_command(pg_ctl_cmd, capture=True)
            self.log(f"[{node.name}] - Stopped successfully")
        except subprocess.CalledProcessError as e:
            # Skip if stop fails - node might not be running or already stopped
            self.log(f"[{node.name}] - Stop failed, skipping (node may not be running)", "WARN")
            self.log(f"[{node.name}] - Command: {' '.join(pg_ctl_cmd)}", verbose_level=1)
            self.log(f"[{node.name}] - Exit code: {e.returncode}", verbose_level=1)
    
    def wait_for_node(self, node: NodeConfig, timeout: int = 30) -> bool:
        """Wait for node to be ready to accept connections"""
        self.log(f"[{node.name}] - Waiting to be ready...")
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                conn = psycopg2.connect(
                    host='localhost',
                    port=node.port,
                    user=self.current_user,
                    database='postgres'
                )
                conn.close()
                self.log(f"[{node.name}] - Ready")
                return True
            except psycopg2.OperationalError:
                time.sleep(1)
        
        self.log(f"[{node.name}] - Failed to become ready within {timeout}s", "ERROR")
        return False
    
    def setup_replication(self) -> None:
        """Setup streaming replication between nodes"""
        self.log("[primary1] - Setting up streaming replication...")
        
        # Connect to primary and create replication slots
        try:
            conn = psycopg2.connect(
                host='localhost',
                port=self.nodes['primary1'].port,
                user=self.current_user,
                database='postgres'
            )
            conn.autocommit = True
            cursor = conn.cursor()
            
            # Create replication slots for replicas
            for replica_name in ['replica1', 'replica2']:
                slot_name = f"{replica_name}_slot"
                cursor.execute(f"SELECT pg_create_physical_replication_slot('{slot_name}')")
                self.log(f"[primary1] - Created replication slot: {slot_name}")
            
            cursor.close()
            conn.close()
            
        except psycopg2.Error as e:
            self.log(f"Failed to setup replication slots: {e}", "ERROR")
            raise
    
    def setup_pgraft_extension(self) -> None:
        """Create and load pgraft extension on all nodes"""
        self.log("Setting up pgraft extension on all nodes...")
        
        for node_name, node in self.nodes.items():
            try:
                conn = psycopg2.connect(
                    host='localhost',
                    port=node.port,
                    user=self.current_user,
                    database='postgres'
                )
                conn.autocommit = True
                cursor = conn.cursor()
                
                # Create pgraft extension if it doesn't exist
                cursor.execute("CREATE EXTENSION IF NOT EXISTS pgraft")
                self.log(f"[{node_name}] - pgraft extension created/loaded")
                
                # Initialize pgraft (gets configuration from GUC variables)
                cursor.execute("SELECT pgraft_init()")
                result = cursor.fetchone()[0]
                if result:
                    self.log(f"[{node_name}] - pgraft initialized successfully")
                else:
                    self.log(f"[{node_name}] - pgraft initialization failed", "ERROR")
                    cursor.close()
                    conn.close()
                    sys.exit(1)
                
                # Background worker will handle consensus process automatically
                self.log(f"[{node_name}] - pgraft consensus process will start automatically")
                self.log(f"[{node_name}] - pgraft network worker started automatically")
                
                # Wait for background worker to be RUNNING
                self.log(f"[{node_name}] - Waiting for background worker to be RUNNING...")
                max_wait_time = 30  # 30 seconds timeout
                wait_interval = 1   # Check every 1 second
                waited_time = 0
                
                while waited_time < max_wait_time:
                    try:
                        cursor.execute("SELECT pgraft_get_worker_state()")
                        worker_state = cursor.fetchone()[0]
                        self.log(f"[{node_name}] - Worker state: {worker_state}")
                        
                        if worker_state == "RUNNING":
                            self.log(f"[{node_name}] - Background worker is RUNNING")
                            break
                        elif worker_state == "ERROR":
                            self.log(f"[{node_name}] - Background worker error", "ERROR")
                            break
                            
                        time.sleep(wait_interval)
                        waited_time += wait_interval
                        
                    except Exception as e:
                        self.log(f"[{node_name}] - Error checking worker state: {e}", "ERROR")
                        break
                
                if waited_time >= max_wait_time:
                    self.log(f"[{node_name}] - Timeout waiting for worker to be RUNNING", "ERROR")
                    cursor.close()
                    conn.close()
                    sys.exit(1)
                
                # pgraft consensus system is now initialized and ready
                self.log(f"[{node_name}] - pgraft consensus system ready")
                
                cursor.close()
                conn.close()
                
            except psycopg2.Error as e:
                self.log(f"[{node_name}] - Failed to setup pgraft extension: {e}", "ERROR")
                sys.exit(1)
        
        # Configure cluster peers after all nodes are set up
        self.configure_cluster_peers()
    
    def configure_cluster_peers(self):
        """Configure cluster peers by adding nodes to each other"""
        self.log("Configuring cluster peers...")
        
        # Define peer relationships based on configuration files
        peer_configs = {
            'primary1': [
                {'node_id': 2, 'address': '127.0.0.1', 'port': 7002},  # replica1
                {'node_id': 3, 'address': '127.0.0.1', 'port': 7003}   # replica2
            ],
            'replica1': [
                {'node_id': 1, 'address': '127.0.0.1', 'port': 7001},  # primary1
                {'node_id': 3, 'address': '127.0.0.1', 'port': 7003}   # replica2
            ],
            'replica2': [
                {'node_id': 1, 'address': '127.0.0.1', 'port': 7001},  # primary1
                {'node_id': 2, 'address': '127.0.0.1', 'port': 7002}   # replica1
            ]
        }
        
        for node_name, node in self.nodes.items():
            if node_name not in peer_configs:
                continue
                
            try:
                conn = psycopg2.connect(
                    host='localhost',
                    port=node.port,
                    user=self.current_user,
                    database='postgres'
                )
                cursor = conn.cursor()
                
                # Add each peer to this node
                for peer in peer_configs[node_name]:
                    cursor.execute(
                        "SELECT pgraft_add_node(%s, %s, %s)",
                        (peer['node_id'], peer['address'], peer['port'])
                    )
                    result = cursor.fetchone()[0]
                    if result:
                        self.log(f"[{node_name}] - Added peer node {peer['node_id']} at {peer['address']}:{peer['port']}")
                    else:
                        self.log(f"[{node_name}] - Failed to add peer node {peer['node_id']}", "WARNING")
                
                cursor.close()
                conn.close()
                
            except psycopg2.Error as e:
                self.log(f"[{node_name}] - Failed to configure peers: {e}", "ERROR")
                sys.exit(1)

    def verify_cluster(self) -> bool:
        """Verify cluster health and connectivity"""
        self.log("Verifying cluster health on all nodes...")
        
        for node_name, node in self.nodes.items():
            try:
                conn = psycopg2.connect(
                    host='localhost',
                    port=node.port,
                    user=self.current_user,
                    database='postgres'
                )
                cursor = conn.cursor()
                
                # Check if pgraft extension is loaded
                cursor.execute("SELECT 1 FROM pg_extension WHERE extname = 'pgraft'")
                if cursor.fetchone():
                    self.log(f"✓ [{node_name}]: pgraft extension loaded")
                else:
                    self.log(f"✗ [{node_name}]: pgraft extension not loaded", "ERROR")
                    cursor.close()
                    conn.close()
                    sys.exit(1)
                
                # Check pgraft status
                try:
                    cursor.execute("SELECT * FROM pgraft_get_cluster_status()")
                    result = cursor.fetchone()
                    if result:
                        node_id, current_term, leader_id, state, num_nodes, messages_processed, heartbeats_sent, elections_triggered = result
                        self.log(f"✓ [{node_name}]: pgraft state = Node ID: {node_id}, Term: {current_term}, Leader: {leader_id}, State: {state}, Nodes: {num_nodes}")
                    else:
                        self.log(f"✗ [{node_name}]: pgraft not responding", "ERROR")
                        cursor.close()
                        conn.close()
                        sys.exit(1)
                except psycopg2.Error:
                    self.log(f"✗ [{node_name}]: pgraft not responding", "ERROR")
                    cursor.close()
                    conn.close()
                    sys.exit(1)
                
                cursor.close()
                conn.close()
                
            except psycopg2.Error as e:
                self.log(f"✗ [{node_name}]: Connection failed - {e}", "ERROR")
                sys.exit(1)
        
        self.log("✓ Cluster verification successful")
        return True
    
    def get_cluster_status(self) -> Dict:
        """Get detailed cluster status"""
        status = {
            'nodes': {},
            'cluster_health': 'unknown',
            'timestamp': time.time()
        }
        
        for node_name, node in self.nodes.items():
            node_status = {
                'name': node_name,
                'port': node.port,
                'pgraft_port': node.pgraft_port,
                'status': 'unknown',
                'pgraft_state': 'unknown',
                'replication_lag': 'unknown'
            }
            
            try:
                conn = psycopg2.connect(
                    host='localhost',
                    port=node.port,
                    user=self.current_user,
                    database='postgres'
                )
                cursor = conn.cursor()
                
                # Check if node is accepting connections
                cursor.execute("SELECT 1")
                node_status['status'] = 'running'
                
                # Get pgraft state
                try:
                    cursor.execute("SELECT * FROM pgraft_get_cluster_status()")
                    result = cursor.fetchone()
                    if result:
                        node_id, current_term, leader_id, state, num_nodes, messages_processed, heartbeats_sent, elections_triggered = result
                        node_status['pgraft_state'] = f"Node ID: {node_id}\nCurrent Term: {current_term}\nLeader ID: {leader_id}\nState: {state}\nNumber of Nodes: {num_nodes}\nMessages Processed: {messages_processed}\nHeartbeats Sent: {heartbeats_sent}\nElections Triggered: {elections_triggered}"
                    else:
                        node_status['pgraft_state'] = "No data"
                except psycopg2.Error:
                    node_status['pgraft_state'] = 'error'
                
                # Get replication lag (for replicas)
                if node_name != 'primary':
                    try:
                        cursor.execute("SELECT pg_last_wal_receive_lsn(), pg_last_wal_replay_lsn()")
                        result = cursor.fetchone()
                        if result and result[0] and result[1]:
                            cursor.execute("SELECT pg_wal_lsn_diff(%s, %s)", result)
                            lag = cursor.fetchone()[0]
                            node_status['replication_lag'] = f"{lag} bytes"
                    except psycopg2.Error:
                        node_status['replication_lag'] = 'unknown'
                
                cursor.close()
                conn.close()
                
            except psycopg2.Error as e:
                node_status['status'] = 'error'
                node_status['error'] = str(e)
            
            status['nodes'][node_name] = node_status
        
        # Determine overall cluster health
        running_nodes = sum(1 for node in status['nodes'].values() if node['status'] == 'running')
        if running_nodes == len(self.nodes):
            status['cluster_health'] = 'healthy'
        elif running_nodes > 0:
            status['cluster_health'] = 'degraded'
        else:
            status['cluster_health'] = 'down'
        
        return status
    
    def print_status(self) -> None:
        """Print formatted cluster status"""
        status = self.get_cluster_status()
        
        print("\n" + "="*60)
        print("PostgreSQL pgraft Cluster Status")
        print("="*60)
        print(f"Cluster Health: {status['cluster_health'].upper()}")
        print(f"Timestamp: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(status['timestamp']))}")
        print(f"Log Directory: {self.log_dir}")
        print()
        
        for node_name, node_status in status['nodes'].items():
            print(f"Node: {node_name}")
            print(f"  Port: {node_status['port']}")
            print(f"  Status: {node_status['status']}")
            print(f"  pgraft State: {node_status['pgraft_state']}")
            if 'replication_lag' in node_status:
                print(f"  Replication Lag: {node_status['replication_lag']}")
            if 'error' in node_status:
                print(f"  Error: {node_status['error']}")
            
            # Show log file locations
            node_log_dir = self.log_dir / node_name
            postgresql_log = node_log_dir / 'postgresql.log'
            pgraft_log = node_log_dir / 'pgraft.log'
            print(f"  Logs:")
            print(f"    PostgreSQL: {postgresql_log}")
            print(f"    pgraft: {pgraft_log}")
            print()
    
    def cleanup(self) -> None:
        """Clean up cluster resources"""
        self.log("Cleaning up cluster...")
        
        # Stop all nodes (gracefully handle failures)
        for node in self.nodes.values():
            try:
                self.stop_node(node)
            except Exception as e:
                self.log(f"[{node.name}] - Cleanup stop failed: {str(e)}", "WARN")
        
        # Remove directories
        if self.base_dir.exists():
            try:
                self.run_command(['rm', '-rf', str(self.base_dir)])
                self.log("Cluster directories removed")
            except Exception as e:
                self.log(f"Failed to remove directories: {str(e)}", "WARN")
    
    def init_cluster(self) -> None:
        """Initialize the entire cluster"""
        self.log("Initializing PostgreSQL pgraft cluster...")
        
        # Check prerequisites first
        self.check_prerequisites()
        
        # Create directories
        self.create_directories()
        
        # Check port availability
        self.check_port_availability()
        
        # Initialize all databases
        for node in self.nodes.values():
            self.init_database(node)
        
        # Start all nodes
        # Start primary first
        self.start_node(self.nodes['primary1'])
        
        # Setup replication
        self.setup_replication()
        
        # Start replicas
        for replica_name in ['replica1', 'replica2']:
            self.start_node(self.nodes[replica_name])
        
        # Wait a moment for cluster to stabilize
        time.sleep(5)
        
        # Setup pgraft extension on all nodes
        self.setup_pgraft_extension()
        
        # Verify cluster
        self.verify_cluster()
        self.log("✓ Cluster initialization completed successfully")
        self.print_status()
    
    def destroy_cluster(self) -> None:
        """Destroy the entire cluster"""
        self.log("Destroying PostgreSQL pgraft cluster...")
        self.cleanup()
        self.log("✓ Cluster destroyed successfully")


def signal_handler(signum, frame):
    """Handle interrupt signals"""
    print("\nReceived interrupt signal. Cleaning up...")
    sys.exit(1)


def main():
    """Main entry point"""
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    parser = argparse.ArgumentParser(
        description="PostgreSQL pgraft Cluster Management",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python pgraft_cluster.py --init     # Initialize cluster
  python pgraft_cluster.py --verify   # Verify cluster health
  python pgraft_cluster.py --status   # Show cluster status
  python pgraft_cluster.py --destroy  # Destroy cluster
        """
    )
    
    parser.add_argument('--init', action='store_true', help='Initialize cluster')
    parser.add_argument('--verify', action='store_true', help='Verify cluster health')
    parser.add_argument('--destroy', action='store_true', help='Destroy cluster')
    parser.add_argument('--status', action='store_true', help='Show cluster status')
    parser.add_argument('--base-dir', default='/tmp/pgraft', help='Base directory for cluster data')
    parser.add_argument('-v', '--verbose', type=int, default=0, choices=[0, 1], 
                       help='Verbose level: 0=tick/cross with datetime, 1=all logs')
    
    args = parser.parse_args()
    
    if not any([args.init, args.verify, args.destroy, args.status]):
        parser.print_help()
        sys.exit(1)
    
    manager = PgraftClusterManager(args.base_dir, args.verbose)
    
    try:
        if args.init:
            manager.init_cluster()
        elif args.verify:
            success = manager.verify_cluster()
            sys.exit(0 if success else 1)
        elif args.destroy:
            manager.destroy_cluster()
        elif args.status:
            manager.print_status()
            
    except KeyboardInterrupt:
        print("\nOperation interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
