---
name: Bug report
about: Report a problem with pgraft
title: "[bug] "
labels: bug
assignees: ''
---

## Description

A clear and concise description of what the bug is.

## Environment

- pgraft version (`SELECT pgraft.get_version();`):
- PostgreSQL version (`SELECT version();`):
- Go version used to build (`go version`):
- Operating system / architecture:
- Number of nodes in the cluster:

## Configuration

Relevant `pgraft.*` settings from each node (redact hostnames/credentials):

```ini
pgraft.name =
pgraft.data_dir =
pgraft.initial_cluster =
pgraft.election_timeout =
pgraft.heartbeat_interval =
```

## Steps to reproduce

1.
2.
3.

## Expected behavior

What you expected to happen.

## Actual behavior

What actually happened. Include, from each node:

```sql
SELECT * FROM pgraft.get_cluster_status();
SELECT * FROM pgraft.get_nodes();
```

## Logs

Any `pgraft`-related lines from the PostgreSQL server log:

```
```

## Additional context

Anything else that might help.
