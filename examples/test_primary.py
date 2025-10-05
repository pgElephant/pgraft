#!/usr/bin/env python3
"""
test_primary.py - Test the primary node startup and configuration

This script tests the primary node startup process step by step.
"""

import os
import sys
import time
import psycopg2
from start_primary import PrimaryConfig, PrimaryNodeManager


def test_config_validation():
    """Test configuration validation"""
    print("🧪 Testing configuration validation...")
    
    config = PrimaryConfig()
    manager = PrimaryNodeManager(config)
    
    if manager.validate_config():
        print("✅ Configuration validation passed")
        return True
    else:
        print("❌ Configuration validation failed")
        return False


def test_postgresql_connection():
    """Test PostgreSQL connection"""
    print("🧪 Testing PostgreSQL connection...")
    
    config = PrimaryConfig()
    
    try:
        conn = psycopg2.connect(
            host="127.0.0.1",
            port=config.port,
            database="postgres",
            user="postgres"
        )
        
        with conn.cursor() as cur:
            cur.execute("SELECT version();")
            version = cur.fetchone()
            print(f"✅ PostgreSQL connection successful: {version[0]}")
            
        conn.close()
        return True
        
    except Exception as e:
        print(f"❌ PostgreSQL connection failed: {e}")
        return False


def test_pgraft_extension():
    """Test pgraft extension"""
    print("🧪 Testing pgraft extension...")
    
    config = PrimaryConfig()
    
    try:
        conn = psycopg2.connect(
            host="127.0.0.1",
            port=config.port,
            database="postgres",
            user="postgres"
        )
        
        with conn.cursor() as cur:
            # Test pgraft extension is loaded
            cur.execute("SELECT * FROM pg_extension WHERE extname = 'pgraft';")
            extension = cur.fetchone()
            if extension:
                print("✅ pgraft extension loaded")
            else:
                print("❌ pgraft extension not loaded")
                return False
            
            # Test basic pgraft functions
            cur.execute("SELECT pgraft_get_nodes();")
            nodes = cur.fetchone()
            print(f"✅ pgraft nodes: {nodes}")
            
            # Test etcd-compatible views
            cur.execute("SELECT * FROM pgraft.member_list;")
            members = cur.fetchall()
            print(f"✅ etcd-compatible member list: {members}")
            
            # Test cluster status
            cur.execute("SELECT * FROM pgraft.endpoint_status;")
            status = cur.fetchall()
            print(f"✅ endpoint status: {status}")
            
        conn.close()
        print("✅ pgraft extension working correctly")
        return True
        
    except Exception as e:
        print(f"❌ pgraft extension test failed: {e}")
        return False


def main():
    """Run all tests"""
    print("🧪 pgraft Primary Node Tests")
    print("=" * 30)
    
    tests = [
        ("Configuration Validation", test_config_validation),
        ("PostgreSQL Connection", test_postgresql_connection),
        ("pgraft Extension", test_pgraft_extension),
    ]
    
    results = []
    
    for test_name, test_func in tests:
        print(f"\n📋 Running: {test_name}")
        try:
            result = test_func()
            results.append((test_name, result))
        except Exception as e:
            print(f"❌ {test_name} failed with exception: {e}")
            results.append((test_name, False))
    
    print("\n📊 Test Results:")
    print("=" * 20)
    
    passed = 0
    for test_name, result in results:
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"{status} {test_name}")
        if result:
            passed += 1
    
    print(f"\n📈 Summary: {passed}/{len(results)} tests passed")
    
    if passed == len(results):
        print("🎉 All tests passed!")
        return True
    else:
        print("💥 Some tests failed!")
        return False


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
