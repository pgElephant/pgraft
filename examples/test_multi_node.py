#!/usr/bin/env python3
"""
test_multi_node.py - Test pgraft with different cluster sizes (3, 5, 7 nodes)

This script tests pgraft robustness with various cluster topologies to ensure
it works reliably across different Raft quorum sizes.
"""

import os
import sys
import time
import subprocess
import psycopg2
from pathlib import Path
from dataclasses import dataclass
from typing import Dict, List, Optional

@dataclass
class NodeConfig:
    """Configuration for a PostgreSQL node with pgraft"""
    name: str
    port: int
    pgraft_port: int
    data_dir: str
    config_file: str
    metrics_port: int
    go_library_path: str = '/usr/local/pgsql.17/lib/pgraft_go.dylib'

class MultiNodeTester:
    """Test pgraft with different cluster sizes"""
    
    def __init__(self, base_dir: str = "/tmp/pgraft_test"):
        self.base_dir = Path(base_dir)
        self.log_dir = Path.cwd() / "logs"
        
    def log(self, message: str, level: str = "INFO"):
        """Simple logging with timestamp"""
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        if level == "INFO":
            print(f"✓ {timestamp}: {message}")
        elif level == "ERROR":
            print(f"✗ {timestamp}: {message}")
        elif level == "WARN":
            print(f"⚠ {timestamp}: {message}")
        else:
            print(f"• {timestamp}: {message}")
    
    def generate_node_configs(self, node_count: int) -> Dict[str, NodeConfig]:
        """Generate node configurations for different cluster sizes"""
        nodes = {}
        
        for i in range(node_count):
            if i == 0:
                name = "primary1"
            else:
                name = f"replica{i}"
                
            nodes[name] = NodeConfig(
                name=name,
                port=5432 + i,
                pgraft_port=7001 + i,
                data_dir=str(self.base_dir / name),
                config_file=f"{name}.conf",
                metrics_port=9091 + i
            )
        
        return nodes
    
    def check_port_availability(self, nodes: Dict[str, NodeConfig]) -> bool:
        """Check if all required ports are available"""
        self.log("Checking port availability...")
        
        for node_name, config in nodes.items():
            # Check PostgreSQL port
            result = subprocess.run(['lsof', '-i', f':{config.port}'], 
                                  capture_output=True, text=True)
            if result.returncode == 0:
                self.log(f"Port {config.port} is in use", "ERROR")
                return False
                
            # Check pgraft port  
            result = subprocess.run(['lsof', '-i', f':{config.pgraft_port}'], 
                                  capture_output=True, text=True)
            if result.returncode == 0:
                self.log(f"Port {config.pgraft_port} is in use", "ERROR")
                return False
                
        return True
    
    def cleanup_cluster(self, nodes: Dict[str, NodeConfig]):
        """Clean up any existing cluster"""
        self.log("Cleaning up existing cluster...")
        
        # Stop all PostgreSQL instances
        for node_name, config in nodes.items():
            try:
                subprocess.run(['pg_ctl', 'stop', '-D', config.data_dir, '-m', 'fast'], 
                             capture_output=True, timeout=30)
            except:
                pass
                
        # Remove data directories
        try:
            subprocess.run(['rm', '-rf', str(self.base_dir)], check=False)
        except:
            pass
            
        # Remove log directories
        try:
            subprocess.run(['rm', '-rf', str(self.log_dir)], check=False)
        except:
            pass
    
    def test_cluster_size(self, node_count: int) -> bool:
        """Test a specific cluster size"""
        self.log(f"=== Testing {node_count}-node cluster ===")
        
        nodes = self.generate_node_configs(node_count)
        
        # Clean up first
        self.cleanup_cluster(nodes)
        
        # Check port availability
        if not self.check_port_availability(nodes):
            self.log(f"Ports not available for {node_count}-node cluster", "ERROR")
            return False
        
        # Create base directory
        self.base_dir.mkdir(parents=True, exist_ok=True)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        
        try:
            # Initialize nodes
            self.log(f"Initializing {node_count} nodes...")
            for node_name, config in nodes.items():
                # Create data directory
                os.makedirs(config.data_dir, exist_ok=True)
                
                # Initialize database
                result = subprocess.run([
                    'initdb', '-D', config.data_dir, 
                    '--auth-local=trust', '--auth-host=trust'
                ], capture_output=True, text=True, timeout=60)
                
                if result.returncode != 0:
                    self.log(f"Failed to initialize {node_name}: {result.stderr}", "ERROR")
                    return False
            
            # Start nodes
            self.log(f"Starting {node_count} nodes...")
            for node_name, config in nodes.items():
                log_file = self.log_dir / node_name / "postgresql.log"
                log_file.parent.mkdir(parents=True, exist_ok=True)
                
                result = subprocess.run([
                    'pg_ctl', 'start', '-D', config.data_dir,
                    '-o', f'-p {config.port} -k /tmp',
                    '-l', str(log_file)
                ], capture_output=True, text=True, timeout=30)
                
                if result.returncode != 0:
                    self.log(f"Failed to start {node_name}: {result.stderr}", "ERROR")
                    return False
                    
                # Wait for node to be ready
                time.sleep(2)
            
            # Install pgraft extension on all nodes
            self.log("Installing pgraft extension...")
            for node_name, config in nodes.items():
                try:
                    conn = psycopg2.connect(
                        host='localhost', port=config.port, 
                        dbname='postgres', user=os.getenv('USER')
                    )
                    with conn.cursor() as cur:
                        cur.execute("CREATE EXTENSION IF NOT EXISTS pgraft;")
                        cur.execute("SELECT pgraft_init();")
                    conn.commit()
                    conn.close()
                    self.log(f"pgraft initialized on {node_name}")
                except Exception as e:
                    self.log(f"Failed to initialize pgraft on {node_name}: {e}", "ERROR")
                    return False
            
            # Add peers to each node
            self.log("Configuring cluster peers...")
            for node_name, config in nodes.items():
                try:
                    conn = psycopg2.connect(
                        host='localhost', port=config.port, 
                        dbname='postgres', user=os.getenv('USER')
                    )
                    with conn.cursor() as cur:
                        # Add all other nodes as peers
                        for peer_name, peer_config in nodes.items():
                            if peer_name != node_name:
                                node_id = peer_config.pgraft_port - 7000  # Simple ID mapping
                                cur.execute(
                                    "SELECT pgraft_add_node(%s, %s, %s);",
                                    (node_id, '127.0.0.1', peer_config.pgraft_port)
                                )
                    conn.commit()
                    conn.close()
                except Exception as e:
                    self.log(f"Failed to configure peers on {node_name}: {e}", "ERROR")
                    return False
            
            # Wait for election
            self.log("Waiting for leader election...")
            time.sleep(5)
            
            # Check cluster status
            self.log("Checking cluster status...")
            leader_count = 0
            follower_count = 0
            
            for node_name, config in nodes.items():
                try:
                    conn = psycopg2.connect(
                        host='localhost', port=config.port, 
                        dbname='postgres', user=os.getenv('USER')
                    )
                    with conn.cursor() as cur:
                        cur.execute("SELECT pgraft_get_cluster_status();")
                        result = cur.fetchone()[0]
                        # Parse result tuple: (node_id, term, leader, state, nodes, ...)
                        status = eval(result) if isinstance(result, str) else result
                        node_id, term, leader, state, node_count_reported = status[:5]
                        
                        if state == 'leader':
                            leader_count += 1
                            self.log(f"{node_name}: LEADER (term={term}, nodes={node_count_reported})")
                        elif state == 'follower':
                            follower_count += 1
                            self.log(f"{node_name}: follower (term={term}, leader={leader})")
                        else:
                            self.log(f"{node_name}: {state} (term={term})")
                    
                    conn.close()
                except Exception as e:
                    self.log(f"Failed to check status on {node_name}: {e}", "ERROR")
            
            # Verify election results
            if leader_count == 1 and follower_count == node_count - 1:
                self.log(f"✓ {node_count}-node cluster: Election successful! 1 leader, {follower_count} followers")
                return True
            else:
                self.log(f"✗ {node_count}-node cluster: Election failed! {leader_count} leaders, {follower_count} followers", "ERROR")
                return False
                
        except Exception as e:
            self.log(f"Test failed with exception: {e}", "ERROR")
            return False
        finally:
            # Cleanup
            self.cleanup_cluster(nodes)
            time.sleep(2)
    
    def run_all_tests(self):
        """Run tests for all cluster sizes"""
        cluster_sizes = [3, 5, 7]
        results = {}
        
        self.log("Starting multi-node pgraft testing...")
        
        for size in cluster_sizes:
            try:
                success = self.test_cluster_size(size)
                results[size] = success
                
                if success:
                    self.log(f"✓ {size}-node test: PASSED")
                else:
                    self.log(f"✗ {size}-node test: FAILED", "ERROR")
                    
                # Wait between tests
                time.sleep(3)
                
            except KeyboardInterrupt:
                self.log("Test interrupted by user", "WARN")
                break
            except Exception as e:
                self.log(f"✗ {size}-node test failed with exception: {e}", "ERROR")
                results[size] = False
        
        # Summary
        self.log("=== TEST SUMMARY ===")
        passed = sum(1 for success in results.values() if success)
        total = len(results)
        
        for size, success in results.items():
            status = "PASSED" if success else "FAILED"
            self.log(f"{size}-node cluster: {status}")
        
        self.log(f"Overall: {passed}/{total} tests passed")
        
        if passed == total:
            self.log("🎉 All tests passed! pgraft is robust across all cluster sizes!")
            return True
        else:
            self.log(f"⚠ {total - passed} tests failed. pgraft needs improvement.", "WARN")
            return False

if __name__ == "__main__":
    tester = MultiNodeTester()
    success = tester.run_all_tests()
    sys.exit(0 if success else 1)