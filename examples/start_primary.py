#!/usr/bin/env python3
"""
start_primary.py - Simplified Primary PostgreSQL Node Startup with pgraft

This script focuses on starting just the primary node with proper:
1. Parse configuration from GUC variables
2. Validate configuration
3. Fill structured config
4. Start PostgreSQL primary

Usage:
    python start_primary.py

Author: pgElephant Team
License: MIT
"""

import os
import sys
import time
import signal
import subprocess
from pathlib import Path
from typing import Dict, Optional
from dataclasses import dataclass

# Set library paths for psycopg2
os.environ['DYLD_LIBRARY_PATH'] = '/usr/local/pgsql.17/lib:/opt/homebrew/lib:' + os.environ.get('DYLD_LIBRARY_PATH', '')

try:
    import psycopg2
except ImportError as e:
    print(f"Error importing psycopg2: {e}")
    print("Make sure PostgreSQL libraries are available")
    sys.exit(1)


@dataclass
class PrimaryConfig:
    """Configuration for primary PostgreSQL node with pgraft"""
    name: str = "primary1"
    port: int = 5432
    pgraft_port: int = 2380
    data_dir: str = "/tmp/pgraft/primary1"
    config_file: str = "/tmp/pgraft/primary1/postgresql.conf"
    metrics_port: int = 9091
    go_library_path: str = "/usr/local/pgsql.17/lib/pgraft_go.dylib"
    
    # etcd-compatible cluster configuration
    initial_cluster: str = "primary1=http://127.0.0.1:2380"
    initial_cluster_state: str = "new"
    initial_cluster_token: str = "pgraft-cluster-token"
    listen_peer_urls: str = "http://127.0.0.1:2380"
    listen_client_urls: str = "http://127.0.0.1:2379"
    advertise_client_urls: str = "http://127.0.0.1:2379"
    initial_advertise_peer_urls: str = "http://127.0.0.1:2380"
    
    # Consensus settings
    election_timeout: int = 1000
    heartbeat_interval: int = 100
    snapshot_count: int = 10000
    quota_backend_bytes: int = 2147483647
    max_request_bytes: int = 1572864
    
    # Logging settings
    log_level: str = "info"
    log_outputs: str = "default"
    log_package_levels: str = ""
    
    # Storage settings
    max_snapshots: int = 5
    max_wals: int = 5
    auto_compaction_retention: str = "1h"
    auto_compaction_mode: str = "periodic"
    compaction_batch_limit: int = 1000
    
    # Security settings (disabled for simplicity)
    client_cert_auth: bool = False
    trusted_ca_file: str = ""
    cert_file: str = ""
    key_file: str = ""
    client_cert_file: str = ""
    client_key_file: str = ""
    peer_trusted_ca_file: str = ""
    peer_cert_file: str = ""
    peer_key_file: str = ""
    peer_client_cert_auth: bool = False
    peer_cert_allowed_cn: str = ""
    peer_cert_allowed_hostname: bool = True
    cipher_suites: str = ""
    cors: str = ""
    host_whitelist: str = ""
    
    # Monitoring settings
    listen_metrics_urls: str = "http://127.0.0.1:9091"
    metrics: str = "basic"


class PrimaryNodeManager:
    """Simplified primary node management"""
    
    def __init__(self, config: PrimaryConfig):
        self.config = config
        self.pg_process = None
        self.base_dir = Path(config.data_dir).parent
        
    def validate_config(self) -> bool:
        """Validate primary configuration"""
        print("✓ Validating primary configuration...")
        
        # Check required paths
        if not os.path.exists(self.config.go_library_path):
            print(f"✗ Go library not found: {self.config.go_library_path}")
            return False
            
        # Validate initial_cluster format
        if not self.config.initial_cluster or "=" not in self.config.initial_cluster:
            print(f"✗ Invalid initial_cluster format: {self.config.initial_cluster}")
            return False
            
        # Validate cluster state
        if self.config.initial_cluster_state not in ["new", "existing"]:
            print(f"✗ Invalid cluster state: {self.config.initial_cluster_state}")
            return False
            
        # Validate ports
        if self.config.port == self.config.pgraft_port:
            print(f"✗ PostgreSQL port ({self.config.port}) cannot be same as pgraft port ({self.config.pgraft_port})")
            return False
            
        print("✓ Configuration validation passed")
        return True
    
    def create_directories(self):
        """Create necessary directories"""
        print("✓ Creating directories...")
        
        # Create data directory
        self.config.data_dir = str(self.base_dir / "primary1")
        os.makedirs(self.config.data_dir, exist_ok=True)
        
        # Create config directory
        config_dir = os.path.dirname(self.config.config_file)
        os.makedirs(config_dir, exist_ok=True)
        
        print(f"✓ Created directories: {self.config.data_dir}")
    
    def generate_postgresql_config(self):
        """Generate PostgreSQL configuration file with etcd-compatible GUCs"""
        print("✓ Generating PostgreSQL configuration...")
        
        config_content = f"""# PostgreSQL Configuration for Primary Node with pgraft
# Generated by start_primary.py - etcd-compatible configuration

# Basic PostgreSQL settings
listen_addresses = '*'
port = {self.config.port}
max_connections = 100
shared_buffers = 32MB
wal_level = replica
shared_preload_libraries = 'pgraft'

# pgraft cluster configuration (etcd-compatible)
pgraft.name = '{self.config.name}'
pgraft.data_dir = '{self.config.data_dir}'
pgraft.initial_cluster = '{self.config.initial_cluster}'
pgraft.initial_cluster_state = '{self.config.initial_cluster_state}'
pgraft.initial_cluster_token = '{self.config.initial_cluster_token}'
pgraft.initial_advertise_peer_urls = '{self.config.initial_advertise_peer_urls}'
pgraft.advertise_client_urls = '{self.config.advertise_client_urls}'
pgraft.listen_client_urls = '{self.config.listen_client_urls}'
pgraft.listen_peer_urls = '{self.config.listen_peer_urls}'

# Consensus settings
pgraft.election_timeout = {self.config.election_timeout}
pgraft.heartbeat_interval = {self.config.heartbeat_interval}
pgraft.snapshot_count = {self.config.snapshot_count}
pgraft.quota_backend_bytes = {self.config.quota_backend_bytes}
pgraft.max_request_bytes = {self.config.max_request_bytes}

# Logging settings
pgraft.log_level = '{self.config.log_level}'
pgraft.log_outputs = '{self.config.log_outputs}'
pgraft.log_package_levels = '{self.config.log_package_levels}'

# Performance settings
pgraft.max_snapshots = {self.config.max_snapshots}
pgraft.max_wals = {self.config.max_wals}
pgraft.auto_compaction_retention = '{self.config.auto_compaction_retention}'
pgraft.auto_compaction_mode = '{self.config.auto_compaction_mode}'
pgraft.compaction_batch_limit = {self.config.compaction_batch_limit}

# Security settings
pgraft.client_cert_auth = {str(self.config.client_cert_auth).lower()}
pgraft.trusted_ca_file = '{self.config.trusted_ca_file}'
pgraft.cert_file = '{self.config.cert_file}'
pgraft.key_file = '{self.config.key_file}'
pgraft.client_cert_file = '{self.config.client_cert_file}'
pgraft.client_key_file = '{self.config.client_key_file}'
pgraft.peer_trusted_ca_file = '{self.config.peer_trusted_ca_file}'
pgraft.peer_cert_file = '{self.config.peer_cert_file}'
pgraft.peer_key_file = '{self.config.peer_key_file}'
pgraft.peer_client_cert_auth = {str(self.config.peer_client_cert_auth).lower()}
pgraft.peer_cert_allowed_cn = '{self.config.peer_cert_allowed_cn}'
pgraft.peer_cert_allowed_hostname = {str(self.config.peer_cert_allowed_hostname).lower()}
pgraft.cipher_suites = '{self.config.cipher_suites}'
pgraft.cors = '{self.config.cors}'
pgraft.host_whitelist = '{self.config.host_whitelist}'

# Monitoring settings
pgraft.listen_metrics_urls = '{self.config.listen_metrics_urls}'
pgraft.metrics = '{self.config.metrics}'

# PostgreSQL-specific settings (not in etcd)
pgraft.node_id = 1
pgraft.pgraft_port = {self.config.pgraft_port}
pgraft.address = '127.0.0.1'
pgraft.go_library_path = '{self.config.go_library_path}'
pgraft.max_log_entries = 10000
pgraft.batch_size = 100
pgraft.max_batch_delay = 1000
"""
        
        with open(self.config.config_file, 'w') as f:
            f.write(config_content)
            
        print(f"✓ Generated configuration: {self.config.config_file}")
    
    def init_database(self):
        """Initialize PostgreSQL database"""
        print("✓ Initializing PostgreSQL database...")
        
        # Check if database already exists
        if os.path.exists(os.path.join(self.config.data_dir, "postgresql.conf")):
            print("✓ Database already initialized")
            return
            
        # Initialize database
        cmd = [
            "initdb",
            "-D", self.config.data_dir,
            "--auth-local=trust",
            "--auth-host=md5"
        ]
        
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            print("✓ Database initialized successfully")
        except subprocess.CalledProcessError as e:
            print(f"✗ Failed to initialize database: {e.stderr}")
            sys.exit(1)
    
    def start_postgresql(self):
        """Start PostgreSQL server"""
        print("✓ Starting PostgreSQL server...")
        
        # Check if already running
        if self.is_postgresql_running():
            print("✓ PostgreSQL already running")
            return True
            
        # Start PostgreSQL
        cmd = [
            "postgres",
            "-D", self.config.data_dir,
            "-c", f"config_file={self.config.config_file}"
        ]
        
        try:
            self.pg_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            # Wait for startup
            print("✓ Waiting for PostgreSQL to start...")
            time.sleep(3)
            
            if self.is_postgresql_running():
                print("✓ PostgreSQL started successfully")
                return True
            else:
                print("✗ PostgreSQL failed to start")
                return False
                
        except Exception as e:
            print(f"✗ Failed to start PostgreSQL: {e}")
            return False
    
    def is_postgresql_running(self) -> bool:
        """Check if PostgreSQL is running"""
        try:
            conn = psycopg2.connect(
                host="127.0.0.1",
                port=self.config.port,
                database="postgres",
                user="postgres"
            )
            conn.close()
            return True
        except:
            return False
    
    def test_pgraft_extension(self):
        """Test pgraft extension"""
        print("✓ Testing pgraft extension...")
        
        try:
            conn = psycopg2.connect(
                host="127.0.0.1",
                port=self.config.port,
                database="postgres",
                user="postgres"
            )
            
            with conn.cursor() as cur:
                # Test basic pgraft functions
                cur.execute("SELECT pgraft_get_nodes();")
                nodes = cur.fetchone()
                print(f"✓ pgraft nodes: {nodes}")
                
                # Test etcd-compatible views
                cur.execute("SELECT * FROM pgraft.member_list;")
                members = cur.fetchall()
                print(f"✓ etcd-compatible member list: {members}")
                
            conn.close()
            print("✓ pgraft extension working correctly")
            return True
            
        except Exception as e:
            print(f"✗ pgraft extension test failed: {e}")
            return False
    
    def stop_postgresql(self):
        """Stop PostgreSQL server"""
        print("✓ Stopping PostgreSQL server...")
        
        if self.pg_process:
            self.pg_process.terminate()
            self.pg_process.wait()
            self.pg_process = None
            
        print("✓ PostgreSQL stopped")
    
    def cleanup(self):
        """Cleanup resources"""
        print("✓ Cleaning up...")
        self.stop_postgresql()
    
    def run(self):
        """Main execution flow: parse, validate, fill config, start"""
        print("🚀 Starting Primary PostgreSQL Node with pgraft")
        print("=" * 50)
        
        try:
            # Step 1: Validate configuration
            if not self.validate_config():
                print("✗ Configuration validation failed")
                return False
            
            # Step 2: Create directories
            self.create_directories()
            
            # Step 3: Generate PostgreSQL config with etcd-compatible GUCs
            self.generate_postgresql_config()
            
            # Step 4: Initialize database
            self.init_database()
            
            # Step 5: Start PostgreSQL
            if not self.start_postgresql():
                print("✗ Failed to start PostgreSQL")
                return False
            
            # Step 6: Test pgraft extension
            if not self.test_pgraft_extension():
                print("✗ pgraft extension test failed")
                return False
            
            print("\n🎉 Primary PostgreSQL node started successfully!")
            print(f"   - PostgreSQL: postgresql://postgres@127.0.0.1:{self.config.port}/postgres")
            print(f"   - pgraft peer: {self.config.listen_peer_urls}")
            print(f"   - pgraft client: {self.config.listen_client_urls}")
            print(f"   - Metrics: {self.config.listen_metrics_urls}")
            
            # Keep running until interrupted
            print("\nPress Ctrl+C to stop...")
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                print("\n✓ Shutting down...")
                self.cleanup()
                
            return True
            
        except Exception as e:
            print(f"✗ Error: {e}")
            self.cleanup()
            return False


def main():
    """Main entry point"""
    print("pgraft Primary Node Startup")
    print("=" * 30)
    
    # Create configuration
    config = PrimaryConfig()
    
    # Create and run manager
    manager = PrimaryNodeManager(config)
    success = manager.run()
    
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
