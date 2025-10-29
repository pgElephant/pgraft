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
        return result
    
    def init_cluster(self, num_nodes=3):
        self.log(f"Initializing 1 primary + {num_nodes-1} replicas with pgraft Raft cluster...")
        
        # Cleanup existing containers
        self.run_cmd("docker rm -f $(docker ps -a --filter name=pgraft- -q) 2>/dev/null || true", check=False)
        self.run_cmd(f"docker network rm {self.network} 2>/dev/null || true", check=False)
        
        # Create network
        self.log("Creating Docker network...")
        self.run_cmd(f"docker network create {self.network}")
        
        # Node configurations: 1 primary + (num_nodes-1) replicas
        nodes = []
        # Always create primary first
        nodes.append({
            "name": "primary", 
            "id": 1, 
            "pg_port": 7001, 
            "raft_port": 8001, 
            "metrics_port": 9191, 
            "role": "primary"
        })
        
        # Create replicas dynamically based on num_nodes
        for i in range(1, num_nodes):
            nodes.append({
                "name": f"replica{i}",
                "id": i + 1,
                "pg_port": 7000 + i + 1,
                "raft_port": 8000 + i + 1,
                "metrics_port": 9190 + i + 1,
                "role": "replica"
            })
        
        self.log(f"Creating cluster with {num_nodes} nodes: 1 primary + {num_nodes-1} replicas")
        
        # Build initial_cluster string
        initial_cluster = ",".join([f"{node['name']}=http://pgraft-{node['name']}:{node['raft_port']}" for node in nodes])
        
        # Generate pgraft configuration files for each node
        self.log("Generating pgraft configuration files...")
        # Calculate replication settings based on cluster size
        num_replicas = num_nodes - 1
        max_replicas = max(3, num_replicas + 1)  # At least 3, or num_replicas + 1 buffer
        
        for node in nodes:
            config_content = f"""
# PostgreSQL Replication Settings
wal_level = replica
max_wal_senders = {max_replicas}
max_replication_slots = {max_replicas}
hot_standby = on

# pgraft Extension Configuration
shared_preload_libraries = 'pgraft'
pgraft.name = '{node['name']}'
pgraft.data_dir = '/var/lib/postgresql/pgraft-data'
pgraft.listen_peer_urls = 'http://0.0.0.0:{node['raft_port']}'
pgraft.listen_client_urls = 'http://0.0.0.0:{node['metrics_port']}'
pgraft.initial_advertise_peer_urls = 'http://pgraft-{node['name']}:{node['raft_port']}'
pgraft.advertise_client_urls = 'http://pgraft-{node['name']}:{node['metrics_port']}'
pgraft.initial_cluster = '{initial_cluster}'
pgraft.initial_cluster_state = 'new'
pgraft.initial_cluster_token = 'pgraft-cluster'
"""
            config_file = f"postgresql.conf.{node['name']}"
            with open(config_file, 'w') as f:
                f.write(config_content.strip() + '\n')
            self.log(f"  ✓ Created {config_file}")
        
        # Start PRIMARY first WITH pgraft config
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
        
        # Copy pgraft configuration to primary
        self.log("Configuring PRIMARY with pgraft...")
        config_file = f"postgresql.conf.{primary['name']}"
        self.run_cmd(f"docker cp {config_file} pgraft-{primary['name']}:/tmp/pgraft.conf", check=False)
        self.run_cmd(f"docker exec pgraft-{primary['name']} sh -c \"cat /tmp/pgraft.conf >> /var/lib/postgresql/data/postgresql.conf\"", check=False)
        
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
            
            # Update pgraft config with replica-specific settings
            # (pg_basebackup copied primary's config, we need to replace pgraft section)
            self.log(f"Updating pgraft configuration for {node['name']}...")
            
            # Copy the pre-generated config file for this replica
            config_file = f"postgresql.conf.{node['name']}"
            self.run_cmd(f"docker cp {config_file} pgraft-{node['name']}:/tmp/pgraft_replica.conf", check=False)
            
            # Remove old pgraft settings and append new ones
            # This ensures no duplicate/conflicting pgraft.* settings
            self.run_cmd(f"""docker exec pgraft-{node['name']} sh -c "
# Remove all existing pgraft settings
sed -i '/^pgraft\\./d' /var/lib/postgresql/data/postgresql.conf
sed -i '/^shared_preload_libraries.*pgraft/d' /var/lib/postgresql/data/postgresql.conf
# Append replica-specific pgraft config
cat /tmp/pgraft_replica.conf >> /var/lib/postgresql/data/postgresql.conf
"
            """, check=False)
            
            # Start PostgreSQL on replica as standby
            self.log(f"Starting {node['name']} as standby...")
            self.run_cmd(f"docker exec -d pgraft-{node['name']} su -c 'postgres -D /var/lib/postgresql/data' postgres", check=False)
            time.sleep(5)
        
        self.log("Waiting for all nodes to start with pgraft...")
        time.sleep(15)
        
        # Install pgraft extension on PRIMARY only (replicas get it from pg_basebackup)
        self.log(f"Installing pgraft extension on primary...")
        result = self.run_cmd(f"PGPASSWORD=postgres psql -h localhost -p {primary['pg_port']} -U postgres -c \"CREATE EXTENSION IF NOT EXISTS pgraft;\"", check=False)
        if result and result.returncode == 0:
            self.log(f"✅ pgraft extension installed on primary")
        
        # Note: Don't clear pgraft data directory here - workers have already initialized!
        # The data directory is fresh because we destroyed the containers before creating them
        
        # Note: pgraft initialization is now automatic!
        # The background worker on each node will automatically:
        # 1. Load the Go library
        # 2. Call pgraft_init_from_gucs() to initialize Raft
        # 3. Start the Raft consensus protocol
        # 4. Update shared memory and persistence files
        # No manual pgraft_init() calls needed!
        
        self.log("Background workers will automatically initialize pgraft Raft cluster...")
        
        self.log("Waiting for Raft cluster to synchronize and elect leader...")
        time.sleep(10)
        
        # Wait for leader election (check every 2 seconds for up to 60 seconds)
        self.log("Waiting for leader election...")
        leader_elected = False
        for attempt in range(30):  # 30 attempts * 2 seconds = 60 seconds max
            try:
                result = self.run_cmd(f"PGPASSWORD=postgres psql -h localhost -p {primary['pg_port']} -U postgres -t -c \"SELECT pgraft_get_leader();\"", check=False)
                if result and result.stdout:
                    leader_id = result.stdout.strip()
                    if leader_id and leader_id.isdigit() and leader_id != "0":
                        self.log(f"✅ Leader elected: node {leader_id}")
                        leader_elected = True
                        break
            except:
                pass
            time.sleep(2)
            if attempt % 5 == 4:  # Log every 10 seconds
                self.log(f"Still waiting for leader... ({attempt * 2 + 2}s)")
        
        if leader_elected:
            self.log("✓ Cluster initialization complete!")
        else:
            self.log("⚠️  Leader election timed out, but cluster may still be forming...", "WARN")
        
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

