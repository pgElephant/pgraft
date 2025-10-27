#!/bin/bash
# Test script for pgraft cluster with 1 primary + 2 replicas

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🧪 TESTING PGRAFT CLUSTER (1 Primary + 2 Replicas)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "1️⃣  CHECKING CONTAINER STATUS..."
docker ps --filter name=pgraft --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
echo ""

echo "2️⃣  CHECKING PGRAFT RAFT CLUSTER..."
PGPASSWORD=postgres psql -h localhost -p 7001 -U postgres -c "SELECT * FROM pgraft.member_list;"
echo ""

echo "3️⃣  CHECKING POSTGRESQL REPLICATION STATUS..."
PGPASSWORD=postgres psql -h localhost -p 7001 -U postgres -c "SELECT client_addr, application_name, state, sync_state FROM pg_stat_replication;"
echo ""

echo "4️⃣  CHECKING REPLICA1 IS IN STANDBY MODE..."
PGPASSWORD=postgres psql -h localhost -p 7002 -U postgres -c "SELECT pg_is_in_recovery() as is_standby;"
echo ""

echo "5️⃣  CHECKING REPLICA2 IS IN STANDBY MODE..."
PGPASSWORD=postgres psql -h localhost -p 7003 -U postgres -c "SELECT pg_is_in_recovery() as is_standby;"
echo ""

echo "6️⃣  TESTING DATA REPLICATION..."
echo "   📝 Writing to PRIMARY..."
PGPASSWORD=postgres psql -h localhost -p 7001 -U postgres -c "DROP TABLE IF EXISTS test_replication; CREATE TABLE test_replication (id SERIAL PRIMARY KEY, timestamp TIMESTAMP DEFAULT NOW(), message TEXT); INSERT INTO test_replication (message) VALUES ('Test from primary at $(date)');"
sleep 2
echo ""

echo "   📖 Reading from REPLICA1..."
PGPASSWORD=postgres psql -h localhost -p 7002 -U postgres -c "SELECT * FROM test_replication;"
echo ""

echo "   📖 Reading from REPLICA2..."
PGPASSWORD=postgres psql -h localhost -p 7003 -U postgres -c "SELECT * FROM test_replication;"
echo ""

echo "7️⃣  TESTING WRITE PROTECTION ON REPLICAS..."
echo "   Attempting to write to REPLICA1 (should fail)..."
PGPASSWORD=postgres psql -h localhost -p 7002 -U postgres -c "INSERT INTO test_replication (message) VALUES ('This should fail');" 2>&1 | grep -E "(ERROR|cannot execute)"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ ALL TESTS COMPLETE!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

