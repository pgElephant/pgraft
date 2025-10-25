# Changelog

All notable changes to pgraft will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2024-01-XX

### Added
- Initial release of pgraft
- Raft consensus protocol integration using etcd-io/raft library
- Automatic leader election with quorum-based voting
- Crash-safe log replication across cluster nodes
- Background worker architecture for Raft state machine
- SQL functions for cluster management and status monitoring
- Support for dynamic node addition and removal
- Persistent storage for Raft state and log entries
- Split-brain prevention with mathematical guarantees
- Comprehensive documentation and examples
- GitHub Actions CI/CD workflows for multiple PostgreSQL versions (14, 15, 16, 17)
- RPM and DEB packaging support
- Prometheus metrics integration
- PostgreSQL extension registration and control files

### Technical Details
- Written in PostgreSQL C with Go integration for Raft consensus
- Compatible with PostgreSQL versions 16, 17, and 18
- Zero compilation warnings
- Follows PostgreSQL C coding conventions
- Comprehensive error handling and logging
- Production-ready quality and testing

[Unreleased]: https://github.com/pgElephant/pgraft/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/pgElephant/pgraft/releases/tag/v1.0.0
