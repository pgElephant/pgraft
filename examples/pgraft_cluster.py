#!/usr/bin/env python3
"""
pgraft_cluster.py - 1 Primary + 2 Replicas with pgraft Raft Cluster

Creates 1 PostgreSQL primary + 2 replicas with streaming replication.
ALL 3 nodes run pgraft for Raft consensus (replicas can participate in pgraft).

Usage:
    ./pgraft_cluster.py --docker --init --nodes 3
    ./pgraft_cluster.py --docker --status
    ./pgraft_cluster.py --docker --destroy
"""

import subprocess
import time
import sys
import argparse

class PgraftCluster:
    def __init__(self):
        self.network = "pgraft-network"
        self.image = "postgres-pgraft:17"
        self.password = "postgres"
        
    def log(self, msg, level="INFO"):
        colors = {"INFO": "\033[92m✓\033[0m", "ERROR": "\033[91m✗\033[0m", "WARN": "\033[93m⚠\033[0m"}
        symbol = colors.get(level, "•")
        print(f"{symbol} {msg}")
    
    def run_cmd(self, cmd, check=True):
        result = subprocess.run(cmd, capture_output=True, text=True, shell=isinstance(cmd, str))
        if check and result.returncode != 0:
            self.log(f"Command failed: {result.stderr}", "ERROR")
            return None
        return result
    
    def init_cluster(self, num_nodes=3):
        self.log(f"Initializing 1 primary + {num_nodes-1} replicas with pgraft Raft cluster...")
        
        # Cleanup
        self.run_cmd("docker rm -f pgraft-primary pgraft-replica1 pgraft-replica2 2>/dev/null || true", check=False)
        self.run_cmd(f"docker network rm {self.network} 2>/dev/null || true", check=False)
        
        # Create network
        self.log("Creating Docker network...")
        self.run_cmd(f"docker network create {self.network}")
        
        # Node configurations: 1 primary + 2 replicas
        nodes = [
            {"name": "primary", "id": 1, "pg_port": 7001, "raft_port": 8001, "metrics_port": 9191, "role": "primary"},
            {"name": "replica1", "id": 2, "pg_port": 7002, "raft_port": 8002, "metrics_port": 9192, "role": "replica"},
            {"name": "replica2", "id": 3, "pg_port": 7003, "raft_port": 8003, "metrics_port": 9193, "role": "replica"},
        ][:num_nodes]
        
        # Build initial_cluster string
        initial_cluster = ",".join([f"{node['name']}=http://pgraft-{node['name']}:{node['raft_port']}" for node in nodes])
        
        # Start PRIMARY first
        primary = nodes[0]
        self.log(f"Starting PRIMARY ({primary['name']} on port {primary['pg_port']})...")
        self.run_cmd(f"""
docker run -d \
  --name pgraft-{primary['name']} \
  --network {self.network} \
  --hostname pgraft-{primary['name']} \
  -e POSTGRES_PASSWORD={self.password} \
  -e POSTGRES_HOST_AUTH_METHOD=trust \
  -p {primary['pg_port']}:5432 \
  -p {primary['raft_port']}:{primary['raft_port']} \
  -p {primary['metrics_port']}:{primary['metrics_port']} \
  --shm-size=256mb \
  {self.image}
        """)
        time.sleep(5)
        
        # Configure PRIMARY for replication (WITHOUT pgraft yet)
        self.log("Configuring PRIMARY for streaming replication...")
        
        # Add replication settings only (no pgraft yet)
        self.run_cmd(f"docker exec pgraft-{primary['name']} sh -c \"echo 'wal_level = replica' >> /var/lib/postgresql/data/postgresql.conf\"", check=False)
        self.run_cmd(f"docker exec pgraft-{primary['name']} sh -c \"echo 'max_wal_senders = 3' >> /var/lib/postgresql/data/postgresql.conf\"", check=False)
        self.run_cmd(f"docker exec pgraft-{primary['name']} sh -c \"echo 'max_replication_slots = 3' >> /var/lib/postgresql/data/postgresql.conf\"", check=False)
        self.run_cmd(f"docker exec pgraft-{primary['name']} sh -c \"echo 'hot_standby = on' >> /var/lib/postgresql/data/postgresql.conf\"", check=False)
        
        # Restart primary to apply settings
        self.run_cmd(f"docker restart pgraft-{primary['name']}")
        time.sleep(10)
        
        # Create replication user on primary
        self.log("Creating replication user on primary...")
        self.run_cmd(f"PGPASSWORD=postgres psql -h localhost -p {primary['pg_port']} -U postgres -c \"CREATE USER replicator REPLICATION LOGIN PASSWORD 'replicator';\"", check=False)
        
        # Configure pg_hba.conf on primary to allow replication
        self.run_cmd(f"docker exec pgraft-{primary['name']} sh -c \"echo 'host replication replicator all trust' >> /var/lib/postgresql/data/pg_hba.conf\"", check=False)
        self.run_cmd(f"PGPASSWORD=postgres psql -h localhost -p {primary['pg_port']} -U postgres -c \"SELECT pg_reload_conf();\"", check=False)
        
        # Start REPLICAS using pg_basebackup
        for node in nodes[1:]:
            self.log(f"Creating {node['role'].upper()} ({node['name']} on port {node['pg_port']})...")
            
            # Create replica container that won't auto-initialize (using sleep to keep it running)
            self.run_cmd(f"""
docker run -d \
  --name pgraft-{node['name']} \
  --network {self.network} \
  --hostname pgraft-{node['name']} \
  -p {node['pg_port']}:5432 \
  -p {node['raft_port']}:{node['raft_port']} \
  -p {node['metrics_port']}:{node['metrics_port']} \
  --shm-size=256mb \
  --entrypoint /bin/bash \
  {self.image} \
  -c "sleep infinity"
            """)
            time.sleep(2)
            
            # Run pg_basebackup from replica targeting primary
            self.log(f"Running pg_basebackup for {node['name']}...")
            self.run_cmd(f"docker exec pgraft-{node['name']} su -c 'PGPASSWORD=replicator pg_basebackup -h pgraft-{primary['name']} -U replicator -D /var/lib/postgresql/data -P -v -R -X stream -C -S {node['name']}_slot' postgres", check=False)
            
            # Fix data directory permissions (required by PostgreSQL)
            self.run_cmd(f"docker exec pgraft-{node['name']} chmod 700 /var/lib/postgresql/data", check=False)
            
            # Start PostgreSQL on replica as standby (without pgraft config yet)
            self.log(f"Starting {node['name']} as standby...")
            self.run_cmd(f"docker exec -d pgraft-{node['name']} su -c 'postgres -D /var/lib/postgresql/data' postgres", check=False)
            time.sleep(5)
        
        self.log("Waiting for all nodes to start...")
        time.sleep(10)
        
        # Now add pgraft configuration to ALL nodes
        self.log("Configuring pgraft on all nodes...")
        for node in nodes:
            # Copy pgraft config file for this specific node
            config_file = f"postgresql.conf.{node['name']}"
            self.run_cmd(f"docker cp {config_file} pgraft-{node['name']}:/tmp/pgraft.conf", check=False)
            self.run_cmd(f"docker exec pgraft-{node['name']} sh -c \"cat /tmp/pgraft.conf >> /var/lib/postgresql/data/postgresql.conf\"", check=False)
        
        # Restart all nodes to load pgraft
        self.log("Restarting all nodes to load pgraft...")
        self.run_cmd(f"docker restart pgraft-{primary['name']}")
        time.sleep(10)
        
        for node in nodes[1:]:
            self.run_cmd(f"docker restart pgraft-{node['name']}")
            time.sleep(3)
            # Restart PostgreSQL on replica (custom entrypoint)
            self.run_cmd(f"docker exec pgraft-{node['name']} chmod 700 /var/lib/postgresql/data", check=False)
            self.run_cmd(f"docker exec -d pgraft-{node['name']} su -c 'postgres -D /var/lib/postgresql/data' postgres", check=False)
            time.sleep(5)
        
        self.log("Waiting for all nodes to restart with pgraft...")
        time.sleep(10)
        
        # Install pgraft extension on PRIMARY only (replicas get it from pg_basebackup)
        self.log(f"Installing pgraft extension on primary...")
        result = self.run_cmd(f"PGPASSWORD=postgres psql -h localhost -p {primary['pg_port']} -U postgres -c \"CREATE EXTENSION IF NOT EXISTS pgraft;\"", check=False)
        if result and result.returncode == 0:
            self.log(f"✅ pgraft extension installed on primary")
        
        # Clear pgraft data directory on ALL nodes (ensure fresh Raft state)
        self.log(f"Clearing pgraft data directories on all nodes...")
        for node in nodes:
            self.run_cmd(f"docker exec pgraft-{node['name']} sh -c \"rm -rf /var/lib/postgresql/pgraft-data/*\"", check=False)
        
        # Initialize pgraft on ALL nodes IN PARALLEL to form a unified cluster
        # All nodes must call pgraft_init() simultaneously with the same initial_cluster config
        # pgraft_init() can run on replicas because it doesn't modify the database
        self.log("Initializing pgraft Raft cluster on all nodes (in parallel)...")
        
        # Start pgraft_init() on all nodes in background
        import threading
        results = {}
        
        def init_node(node):
            result = self.run_cmd(f"PGPASSWORD=postgres psql -h localhost -p {node['pg_port']} -U postgres -c \"SELECT pgraft_init();\"", check=False)
            results[node['name']] = result
        
        threads = []
        for node in nodes:
            t = threading.Thread(target=init_node, args=(node,))
            t.start()
            threads.append(t)
        
        # Wait for all to complete
        for t in threads:
            t.join(timeout=30)
        
        # Check results
        for node in nodes:
            if node['name'] in results and results[node['name']].returncode == 0:
                self.log(f"  ✅ pgraft initialized on {node['name']} ({node['role']})")
            else:
                self.log(f"  ⚠️  pgraft init failed on {node['name']} ({node['role']})", "WARN")
        
        self.log("Waiting for Raft cluster to synchronize...")
        time.sleep(10)
        
        self.log("✓ Cluster initialization complete!")
        self.show_status()
    
    def show_status(self):
        print("\n" + "="*70)
        print("📊 CLUSTER STATUS")
        print("="*70)
        result = self.run_cmd("docker ps --filter name=pgraft --format '{{.Names}}\t{{.Status}}\t{{.Ports}}'", check=False)
        if result and result.stdout:
            print(result.stdout)
        
        print("\n" + "="*70)
        print("🔍 PGRAFT MEMBER LIST (from primary)")
        print("="*70)
        self.run_cmd("PGPASSWORD=postgres psql -h localhost -p 7001 -U postgres -c 'SELECT * FROM pgraft.member_list;'", check=False)
        
        print("\n" + "="*70)
        print("🔄 REPLICATION STATUS")
        print("="*70)
        self.run_cmd("PGPASSWORD=postgres psql -h localhost -p 7001 -U postgres -c 'SELECT client_addr, application_name, state, sync_state FROM pg_stat_replication;'", check=False)
        
        print("\n" + "="*70)
        print("🔑 CONNECTION INFO")
        print("="*70)
        print("Primary (read/write + pgraft):  psql postgresql://postgres:postgres@localhost:7001/postgres")
        print("Replica1 (read-only + pgraft):  psql postgresql://postgres:postgres@localhost:7002/postgres")
        print("Replica2 (read-only + pgraft):  psql postgresql://postgres:postgres@localhost:7003/postgres")
        print("="*70)
    
    def destroy_cluster(self):
        self.log("Destroying cluster...")
        self.run_cmd("docker rm -f pgraft-primary pgraft-replica1 pgraft-replica2 2>/dev/null || true", check=False)
        self.run_cmd(f"docker network rm {self.network} 2>/dev/null || true", check=False)
        self.log("✓ Cluster destroyed")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="pgraft Cluster Management")
    parser.add_argument("--docker", action="store_true", help="Use Docker mode")
    parser.add_argument("--init", action="store_true", help="Initialize cluster")
    parser.add_argument("--destroy", action="store_true", help="Destroy cluster")
    parser.add_argument("--status", action="store_true", help="Show status")
    parser.add_argument("--nodes", type=int, default=3, help="Number of nodes (default: 3)")
    args = parser.parse_args()
    
    if not args.docker:
        print("❌ Only Docker mode is supported. Use --docker flag")
        sys.exit(1)
    
    cluster = PgraftCluster()
    
    if args.init:
        cluster.init_cluster(args.nodes)
    elif args.destroy:
        cluster.destroy_cluster()
    elif args.status:
        cluster.show_status()
    else:
        parser.print_help()

