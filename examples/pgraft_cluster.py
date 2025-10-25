#!/usr/bin/env python3
"""
pgraft_cluster.py - PostgreSQL pgraft Cluster Management Script with Docker Support

This script provides a clean interface for managing a PostgreSQL cluster with pgraft
consensus. It supports both local and Docker-based deployments.

Usage:
    # Docker deployments
    python pgraft_cluster.py --docker --init --nodes 3     # Create 3-node Docker cluster
    python pgraft_cluster.py --docker --status             # Show Docker cluster status
    python pgraft_cluster.py --docker --destroy            # Destroy Docker cluster
    
    # Local deployments (legacy)
    python pgraft_cluster.py --init --nodes 3              # Create 3-node local cluster
    python pgraft_cluster.py --status                      # Show local cluster status
    python pgraft_cluster.py --destroy                     # Destroy local cluster

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
    go_library_path: str
    container_name: str = None  # For Docker mode
    image: str = "postgres:17"  # Default Docker image


class PgraftClusterManager:
    """PostgreSQL pgraft cluster management with Docker support"""
    
    def __init__(self, base_dir: str = "/tmp/pgraft", verbose: int = 0, docker: bool = False):
        self.base_dir = Path(base_dir)
        self.log_dir = Path.cwd() / "logs"
        self.verbose = verbose
        self.docker_mode = docker
        self.current_user = getpass.getuser()
        
        # Docker configuration
        self.docker_network = "pgraft-network"
        self.docker_image = "postgres:17-alpine"  # Using Alpine for smaller size
        
        # PostgreSQL binary paths (for local mode)
        self.pg_bin_dir = "/usr/local/pgsql.17/bin"
        
        # Node configurations
        self.nodes = self._configure_nodes()
        self.processes: Dict[str, subprocess.Popen] = {}
        self.script_dir = Path(__file__).parent
        
    def _configure_nodes(self) -> Dict[str, NodeConfig]:
        """Configure nodes based on deployment mode"""
        nodes = {}
        
        base_configs = [
            {'name': 'primary1', 'node_id': 1, 'pg_port': 5432, 'raft_port': 7001, 'metrics': 9091},
            {'name': 'replica1', 'node_id': 2, 'pg_port': 5433, 'raft_port': 7002, 'metrics': 9092},
            {'name': 'replica2', 'node_id': 3, 'pg_port': 5434, 'raft_port': 7003, 'metrics': 9093},
            {'name': 'replica3', 'node_id': 4, 'pg_port': 5435, 'raft_port': 7004, 'metrics': 9094},
            {'name': 'replica4', 'node_id': 5, 'pg_port': 5436, 'raft_port': 7005, 'metrics': 9095},
        ]
        
        for config in base_configs:
            if self.docker_mode:
                # Docker mode: Internal port is always 5432, external is mapped
                nodes[config['name']] = NodeConfig(
                    name=config['name'],
                    port=config['pg_port'],
                    pgraft_port=config['raft_port'],
                    data_dir=f"/var/lib/postgresql/data",  # Internal Docker path
                    config_file=f"{config['name']}.conf",
                    metrics_port=config['metrics'],
                    go_library_path='/usr/local/lib/pgraft_go.so',
                    container_name=f"pgraft-{config['name']}",
                    image=self.docker_image
                )
            else:
                # Local mode: Use actual ports
                nodes[config['name']] = NodeConfig(
                    name=config['name'],
                    port=config['pg_port'] if config['name'] == 'primary1' else 5440 + config['node_id'],
                    pgraft_port=config['raft_port'],
                    data_dir=str(self.base_dir / config['name']),
                    config_file=f"{config['name']}.conf",
                    metrics_port=config['metrics'],
                    go_library_path='/usr/local/pgsql.17/lib/pgraft_go.dylib'
                )
        
        return nodes
    
    def log(self, message: str, level: str = "INFO", verbose_level: int = 0) -> None:
        """Log message with timestamp and color coding"""
        if verbose_level > self.verbose:
            return
        
        if level == "WARN" and self.verbose == 0:
            return
        
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        
        # ANSI color codes
        GREEN = '\033[92m'
        RED = '\033[91m'
        YELLOW = '\033[93m'
        BLUE = '\033[94m'
        RESET = '\033[0m'
        
        mode_prefix = f"{BLUE}[DOCKER]{RESET}" if self.docker_mode else f"{BLUE}[LOCAL]{RESET}"
        
        if self.verbose == 0:
            if level == "INFO":
                symbol = f"{GREEN}✓{RESET}"
            elif level == "ERROR":
                symbol = f"{RED}✗{RESET}"
            elif level == "WARN":
                symbol = f"{YELLOW}⚠{RESET}"
            else:
                symbol = "•"
            print(f"{mode_prefix} {symbol} {timestamp}: {message}")
        else:
            print(f"{mode_prefix} [{timestamp}] [{level}] {message}")
    
    # ========================================================================
    # Docker-specific methods
    # ========================================================================
    
    def docker_check_installed(self) -> bool:
        """Check if Docker is installed and running"""
        try:
            result = subprocess.run(['docker', 'version'], capture_output=True, text=True)
            if result.returncode != 0:
                self.log("Docker is not running. Please start Docker.", "ERROR")
                return False
            self.log("Docker is installed and running", verbose_level=1)
            return True
        except FileNotFoundError:
            self.log("Docker is not installed. Please install Docker.", "ERROR")
            return False

    def docker_create_network(self) -> None:
        """Create Docker network for cluster"""
        self.log(f"Creating Docker network: {self.docker_network}")
        
        # Check if network exists
            result = subprocess.run(
            ['docker', 'network', 'ls', '--filter', f'name={self.docker_network}', '--format', '{{.Name}}'],
            capture_output=True,
            text=True
        )
        
        if self.docker_network in result.stdout:
            self.log(f"Network {self.docker_network} already exists", verbose_level=1)
            return
        
        # Create network
        subprocess.run(
            ['docker', 'network', 'create', self.docker_network],
            check=True,
            capture_output=True
        )
        self.log(f"Network {self.docker_network} created")
    
    def docker_create_container(self, node: NodeConfig) -> None:
        """Create and start a Docker container for a node"""
        self.log(f"[{node.name}] Creating Docker container...")
        
        # Stop and remove existing container if it exists
        subprocess.run(['docker', 'stop', node.container_name], capture_output=True)
        subprocess.run(['docker', 'rm', node.container_name], capture_output=True)
        
        # Create directory for persistent storage
        host_data_dir = self.base_dir / "docker" / node.name / "data"
        host_config_dir = self.base_dir / "docker" / node.name / "config"
        host_data_dir.mkdir(parents=True, exist_ok=True)
        host_config_dir.mkdir(parents=True, exist_ok=True)
        
        # Create custom postgresql.conf
        config_content = self._generate_postgres_conf(node)
        config_file = host_config_dir / "postgresql.conf"
        with open(config_file, 'w') as f:
                f.write(config_content)
        
        # Docker run command
        docker_cmd = [
            'docker', 'run', '-d',
            '--name', node.container_name,
            '--network', self.docker_network,
            '-e', 'POSTGRES_PASSWORD=postgres',
            '-e', 'POSTGRES_USER=postgres',
            '-e', 'POSTGRES_DB=postgres',
            '-p', f'{node.port}:5432',  # PostgreSQL port
            '-p', f'{node.pgraft_port}:{node.pgraft_port}',  # Raft port
            '-p', f'{node.metrics_port}:{node.metrics_port}',  # Metrics port
            '-v', f'{host_data_dir}:/var/lib/postgresql/data',
            '-v', f'{host_config_dir}/postgresql.conf:/etc/postgresql/postgresql.conf',
            '-v', f'{self.script_dir.absolute()}:/pgraft',  # Mount pgraft source
            '--shm-size=256mb',
            node.image,
            'postgres',
            '-c', 'config_file=/etc/postgresql/postgresql.conf'
        ]
        
        try:
            result = subprocess.run(docker_cmd, capture_output=True, text=True, check=True)
            container_id = result.stdout.strip()[:12]
            self.log(f"[{node.name}] Container created: {container_id}")
            
            # Wait for PostgreSQL to start
            self.log(f"[{node.name}] Waiting for PostgreSQL to start...")
            time.sleep(10)
            
            if self.docker_container_is_running(node.container_name):
                self.log(f"[{node.name}] Container running successfully")
            else:
                self.log(f"[{node.name}] Container failed to start", "ERROR")
                # Show container logs
                logs = subprocess.run(['docker', 'logs', node.container_name], capture_output=True, text=True)
                self.log(f"[{node.name}] Logs: {logs.stdout}", verbose_level=1)
                sys.exit(1)
            
        except subprocess.CalledProcessError as e:
            self.log(f"[{node.name}] Failed to create container: {e.stderr}", "ERROR")
                sys.exit(1)
    
    def docker_container_is_running(self, container_name: str) -> bool:
        """Check if a Docker container is running"""
        result = subprocess.run(
            ['docker', 'ps', '--filter', f'name={container_name}', '--format', '{{.Names}}'],
            capture_output=True,
            text=True
        )
        return container_name in result.stdout
    
    def docker_exec(self, container_name: str, command: List[str]) -> subprocess.CompletedProcess:
        """Execute command in Docker container"""
        docker_cmd = ['docker', 'exec', container_name] + command
        return subprocess.run(docker_cmd, capture_output=True, text=True)
    
    def docker_install_pgraft(self, node: NodeConfig) -> None:
        """Install pgraft extension in Docker container"""
        self.log(f"[{node.name}] Installing pgraft extension...")
        
        # Install build dependencies
        self.log(f"[{node.name}] Installing build dependencies...")
        self.docker_exec(node.container_name, [
            'sh', '-c',
            'apk add --no-cache build-base postgresql-dev go json-c-dev'
        ])
        
        # Build pgraft
        self.log(f"[{node.name}] Building pgraft...")
        result = self.docker_exec(node.container_name, [
            'sh', '-c',
            'cd /pgraft && make clean && make PG_CONFIG=/usr/local/bin/pg_config'
        ])
        
        if result.returncode != 0:
            self.log(f"[{node.name}] Build failed: {result.stderr}", "ERROR")
            return
        
        # Install pgraft
        self.log(f"[{node.name}] Installing pgraft...")
        self.docker_exec(node.container_name, [
            'sh', '-c',
            'cd /pgraft && make install PG_CONFIG=/usr/local/bin/pg_config'
        ])
        
        # Restart container to load extension
        subprocess.run(['docker', 'restart', node.container_name], capture_output=True)
        time.sleep(5)
        
        self.log(f"[{node.name}] pgraft extension installed")
    
    def docker_stop_container(self, node: NodeConfig) -> None:
        """Stop Docker container"""
        self.log(f"[{node.name}] Stopping container...")
        subprocess.run(['docker', 'stop', node.container_name], capture_output=True)
        self.log(f"[{node.name}] Container stopped")
    
    def docker_remove_container(self, node: NodeConfig) -> None:
        """Remove Docker container"""
        subprocess.run(['docker', 'rm', '-f', node.container_name], capture_output=True)
    
    def docker_get_container_ip(self, container_name: str) -> str:
        """Get container IP address"""
        result = subprocess.run(
            ['docker', 'inspect', '-f', '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}', container_name],
            capture_output=True,
            text=True
        )
        return result.stdout.strip()
    
    # ========================================================================
    # Configuration generation
    # ========================================================================
    
    def _generate_postgres_conf(self, node: NodeConfig) -> str:
        """Generate postgresql.conf for a node"""
        node_id = int(node.name.replace('primary', '1').replace('replica', ''))
        
        # Build initial cluster string
        cluster_members = []
        for name, n in self.nodes.items():
            if self.docker_mode:
                # Use container name as hostname in Docker
                cluster_members.append(f"{name}=http://{n.container_name}:2380")
        else:
                cluster_members.append(f"{name}=http://127.0.0.1:{2380 + int(name.replace('primary', '0').replace('replica', ''))}")
        
        initial_cluster = ','.join(cluster_members[:3])  # First 3 nodes
        
        config = f"""
# PostgreSQL Configuration for {node.name}
# Generated by pgraft_cluster.py

# Connection Settings
listen_addresses = '*'
port = {'5432' if self.docker_mode else str(node.port)}
max_connections = 100

# Memory Settings
shared_buffers = 128MB
effective_cache_size = 512MB
work_mem = 4MB
maintenance_work_mem = 64MB

# WAL Settings
wal_level = replica
max_wal_senders = 10
wal_keep_size = 64MB
hot_standby = on

# Replication Settings
synchronous_commit = on
max_replication_slots = 10

# Logging
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql-%Y-%m-%d_%H%M%S.log'
log_rotation_age = 1d
log_rotation_size = 100MB
log_line_prefix = '%t [%p]: [%l-1] user=%u,db=%d,app=%a,client=%h '
log_min_duration_statement = 1000

# pgraft Extension Settings
shared_preload_libraries = 'pgraft'

# pgraft Configuration (etcd-style)
pgraft.cluster_id = 'pgraft-cluster'
pgraft.node_id = {node_id}
pgraft.name = '{node.name}'
pgraft.address = '{'0.0.0.0' if self.docker_mode else '127.0.0.1'}'
pgraft.port = {node.pgraft_port}
pgraft.data_dir = '/var/lib/postgresql/pgraft' if self.docker_mode else '{self.base_dir}/{node.name}/pgraft'

# etcd-style cluster configuration
pgraft.initial_cluster = '{initial_cluster}'
pgraft.initial_cluster_state = 'new'
pgraft.listen_peer_urls = 'http://0.0.0.0:2380'
pgraft.listen_client_urls = 'http://0.0.0.0:2379'
pgraft.advertise_client_urls = 'http://{'localhost' if not self.docker_mode else node.container_name}:2379'
pgraft.initial_advertise_peer_urls = 'http://{'localhost' if not self.docker_mode else node.container_name}:2380'

# Raft Tuning
pgraft.election_timeout = 1000
pgraft.heartbeat_interval = 100
pgraft.snapshot_interval = 3600
pgraft.log_level = 'info'

# Go Library Path
pgraft.go_library_path = '{node.go_library_path}'
"""
        return config
    
    # ========================================================================
    # Cluster Management (works for both Docker and local)
    # ========================================================================
    
    def init_cluster(self, num_nodes: int = 3) -> None:
        """Initialize cluster"""
        self.log(f"Initializing {num_nodes}-node pgraft cluster...")
        
        if self.docker_mode:
            # Docker initialization
            if not self.docker_check_installed():
                sys.exit(1)
        
            # Create network
            self.docker_create_network()
            
            # Create containers
            node_names = list(self.nodes.keys())[:num_nodes]
            for node_name in node_names:
                node = self.nodes[node_name]
                self.docker_create_container(node)
            
            # Wait for all containers to be ready
            self.log("Waiting for all containers to be ready...")
            time.sleep(15)
            
            # Install pgraft extension on all nodes
            for node_name in node_names:
            node = self.nodes[node_name]
                self.docker_install_pgraft(node)
            
            # Create pgraft extension
            for node_name in node_names:
                node = self.nodes[node_name]
                self.docker_create_pgraft_extension(node)
            
            self.log("✓ Docker cluster initialized successfully")
            self.print_docker_status(num_nodes)
        else:
            # Local initialization (existing code)
            self.log("Local mode initialization not implemented in this version", "ERROR")
            self.log("Please use --docker flag for Docker-based clusters", "ERROR")
            sys.exit(1)
        
    def docker_create_pgraft_extension(self, node: NodeConfig) -> None:
        """Create pgraft extension in container"""
        self.log(f"[{node.name}] Creating pgraft extension...")
        
        # Use docker exec with psql
        result = self.docker_exec(node.container_name, [
            'psql', '-U', 'postgres', '-d', 'postgres',
            '-c', 'CREATE EXTENSION IF NOT EXISTS pgraft;'
        ])
        
        if result.returncode == 0:
            self.log(f"[{node.name}] pgraft extension created")
        else:
            self.log(f"[{node.name}] Failed to create extension: {result.stderr}", "ERROR")
    
    def destroy_cluster(self, num_nodes: int = 3) -> None:
        """Destroy cluster"""
        self.log("Destroying cluster...")
        
        if self.docker_mode:
            node_names = list(self.nodes.keys())[:num_nodes]
            for node_name in node_names:
                node = self.nodes[node_name]
                self.log(f"[{node.name}] Stopping and removing container...")
                self.docker_remove_container(node)
            
            # Remove network
            subprocess.run(['docker', 'network', 'rm', self.docker_network], capture_output=True)
            
            # Clean up data directories
            docker_dir = self.base_dir / "docker"
            if docker_dir.exists():
                import shutil
                shutil.rmtree(docker_dir)
            
            self.log("✓ Docker cluster destroyed")
            else:
            self.log("Local mode destroy not implemented in this version", "ERROR")
    
    def print_docker_status(self, num_nodes: int = 3) -> None:
        """Print Docker cluster status"""
        print("\n" + "="*70)
        print("PostgreSQL pgraft Docker Cluster Status")
        print("="*70)
        print(f"Mode: Docker")
        print(f"Network: {self.docker_network}")
        print(f"Nodes: {num_nodes}")
        print()
        
        node_names = list(self.nodes.keys())[:num_nodes]
        for node_name in node_names:
            node = self.nodes[node_name]
            is_running = self.docker_container_is_running(node.container_name)
            status = "✓ Running" if is_running else "✗ Stopped"
            
            print(f"Node: {node.name}")
            print(f"  Container: {node.container_name}")
            print(f"  Status: {status}")
            print(f"  PostgreSQL Port: {node.port} (host) -> 5432 (container)")
            print(f"  pgraft Port: {node.pgraft_port}")
            print(f"  Metrics Port: {node.metrics_port}")
            
            if is_running:
                ip = self.docker_get_container_ip(node.container_name)
                print(f"  IP Address: {ip}")
                print(f"  Connect: psql postgresql://postgres:postgres@localhost:{node.port}/postgres")
            
            print()
        
        print("="*70)
        print("\nQuick Commands:")
        print(f"  View logs:    docker logs pgraft-primary1")
        print(f"  Connect:      psql postgresql://postgres:postgres@localhost:{self.nodes['primary1'].port}/postgres")
        print(f"  Shell access: docker exec -it pgraft-primary1 bash")
        print()


def signal_handler(signum, frame):
    """Handle interrupt signals"""
    print("\nReceived interrupt signal. Cleaning up...")
    sys.exit(1)


def main():
    """Main entry point"""
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    parser = argparse.ArgumentParser(
        description="PostgreSQL pgraft Cluster Management with Docker Support",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Docker clusters (recommended)
  python pgraft_cluster.py --docker --init --nodes 3      # Create 3-node Docker cluster
  python pgraft_cluster.py --docker --status --nodes 3    # Show Docker cluster status
  python pgraft_cluster.py --docker --destroy --nodes 3   # Destroy Docker cluster
  
  # Single node
  python pgraft_cluster.py --docker --init --nodes 1      # Single node cluster
  
  # Five nodes
  python pgraft_cluster.py --docker --init --nodes 5      # 5-node cluster
        """
    )
    
    parser.add_argument('--docker', action='store_true', help='Use Docker containers (recommended)')
    parser.add_argument('--init', action='store_true', help='Initialize cluster')
    parser.add_argument('--destroy', action='store_true', help='Destroy cluster')
    parser.add_argument('--status', action='store_true', help='Show cluster status')
    parser.add_argument('--nodes', '-n', type=int, default=3, choices=[1, 2, 3, 4, 5],
                       help='Number of nodes (default: 3)')
    parser.add_argument('--base-dir', default='/tmp/pgraft', help='Base directory for cluster data')
    parser.add_argument('-v', '--verbose', type=int, default=0, choices=[0, 1], 
                       help='Verbose level: 0=minimal, 1=detailed')
    
    args = parser.parse_args()
    
    if not any([args.init, args.destroy, args.status]):
        parser.print_help()
        sys.exit(1)
    
    manager = PgraftClusterManager(args.base_dir, args.verbose, args.docker)
    
    try:
        if args.init:
            manager.init_cluster(args.nodes)
        elif args.destroy:
            manager.destroy_cluster(args.nodes)
        elif args.status:
            if args.docker:
                manager.print_docker_status(args.nodes)
            else:
                print("Local mode status not implemented. Use --docker flag.")
                sys.exit(1)
            
    except KeyboardInterrupt:
        print("\nOperation interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
