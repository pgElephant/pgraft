# Getting Started

Welcome to pgraft! This section will help you get up and running quickly.

## What is pgraft?

**pgraft** is a PostgreSQL extension that implements the Raft consensus algorithm for distributed PostgreSQL clusters. It provides:

- **Leader election** - Automatic with quorum-based voting
- **Log replication** - Consistent state across all nodes
- **Split-brain protection** - 100% guaranteed via Raft quorum
- **Automatic configuration replication** - Changes on leader replicate to all nodes

## Quick Navigation

### Installation

Learn how to install pgraft on your system.

[Install pgraft](installation.md){ .md-button .md-button--primary }

### Quick Start

Set up your first cluster in minutes.

[Quick Start Guide](quick-start.md){ .md-button }

## Prerequisites

Before you begin, ensure you have:

- **PostgreSQL 17+** with development headers
- **Go 1.21+** for building
- **GCC** or compatible C compiler
- **Basic knowledge** of PostgreSQL and distributed systems

## Learning Path

We recommend following this path:

1. **Installation** - Get pgraft installed on your system
2. **Quick Start** - Create your first 3-node cluster
3. **Tutorial** - Learn all features in depth
4. **Configuration** - Understand all available settings
5. **Operations** - Learn monitoring and maintenance

## System Requirements

### Minimum Requirements

- **CPU**: 2 cores per node
- **RAM**: 4GB per node
- **Disk**: 10GB free space per node
- **Network**: Reliable connectivity between nodes

### Recommended for Production

- **CPU**: 4+ cores per node
- **RAM**: 16GB+ per node
- **Disk**: SSD or NVMe storage
- **Network**: 1 Gbps with <10ms latency between nodes

## Next Steps

Ready to begin?

- **New to pgraft?** Start with [Installation](installation.md)
- **Want to test quickly?** Jump to [Quick Start](quick-start.md)
- **Need detailed guidance?** Check the [Complete Tutorial](../user-guide/tutorial.md)

## Support

- **Documentation**: You're reading it!
- **Issues**: [GitHub Issues](https://github.com/pgelephant/pgraft/issues)
- **Source Code**: [GitHub Repository](https://github.com/pgelephant/pgraft)

