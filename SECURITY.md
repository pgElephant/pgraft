# Security Policy

## Supported Versions

pgraft follows the same support policy as PostgreSQL:

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

We take the security of pgraft seriously. If you believe you have found a security vulnerability, please report it to us as described below.

### Please DO NOT:
- Open a public GitHub issue
- Discuss the vulnerability in public forums
- Create pull requests that might reveal the issue

### Please DO:
- **Email**: Send a detailed report to security@pgelephant.com
- **Include**:
  - Description of the vulnerability
  - Steps to reproduce
  - Potential impact
  - Suggested fix (if you have one)
  - Your contact information

### What to expect:
- **Acknowledgment**: We will acknowledge receipt within 24 hours
- **Initial Assessment**: We will provide an initial assessment within 5 business days
- **Updates**: We will provide updates on the status every 5 business days
- **Resolution**: We will work with you to resolve the issue and coordinate disclosure

### Security Best Practices

When using pgraft in production:
- Keep PostgreSQL and pgraft updated to the latest versions
- Use strong passwords and SSL/TLS encryption
- Restrict network access to cluster nodes
- Follow PostgreSQL security hardening guidelines
- Monitor logs for suspicious activity
- Use firewall rules to limit access
- Regularly audit cluster configuration

## Security Considerations

### Cluster Communication
- pgraft nodes communicate over TCP/IP for Raft consensus
- Consider using network isolation (VPN, private networks)
- Implement network-level encryption (TLS/SSL) if required

### Database Access Control
- Use PostgreSQL's role-based access control (RBAC)
- Limit who can use pgraft SQL functions
- Follow the principle of least privilege

### Data Persistence
- pgraft stores Raft state on disk
- Ensure proper file permissions on the data directory
- Regular backups of both PostgreSQL and pgraft state

## Known Security Limitations

Currently, pgraft:
- Does not implement TLS encryption for inter-node communication (use network-level encryption)
- Relies on PostgreSQL's authentication and authorization
- Does not implement its own authentication mechanism

## Additional Resources

- [PostgreSQL Security Best Practices](https://www.postgresql.org/docs/current/security.html)
- [PostgreSQL pg_hba.conf Configuration](https://www.postgresql.org/docs/current/auth-pg-hba-conf.html)
