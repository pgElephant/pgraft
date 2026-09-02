# Getting help

## Documentation first

Most questions are answered in the docs:

| Question | Document |
|---|---|
| How do I build and install it? | [docs/getting-started/installation.md](docs/getting-started/installation.md) |
| How do I get a cluster running? | [docs/getting-started/quick-start.md](docs/getting-started/quick-start.md) |
| What does each setting do? | [docs/user-guide/configuration.md](docs/user-guide/configuration.md) |
| What SQL can I call? | [docs/user-guide/sql-functions.md](docs/user-guide/sql-functions.md) |
| How does it behave under a partition? | [docs/concepts/split-brain.md](docs/concepts/split-brain.md) |
| It is not electing a leader | [docs/operations/troubleshooting.md](docs/operations/troubleshooting.md) |
| How should I run it in production? | [docs/operations/best-practices.md](docs/operations/best-practices.md) |

## Asking a question

Open a [discussion](https://github.com/pgElephant/pgraft/discussions) for usage
questions, design questions, and "is this the right tool for my case".

## Reporting a bug

Open an [issue](https://github.com/pgElephant/pgraft/issues/new/choose). The
report is much easier to act on with:

- your PostgreSQL version, platform, and Go version;
- the `pgraft.*` settings from every node;
- `SELECT * FROM pgraft.get_cluster_status();` and
  `SELECT * FROM pgraft.get_nodes();` from each node;
- the server log lines matching `pgraft` from around the time of the problem.

## Reporting a vulnerability

Please do **not** open a public issue. See [SECURITY.md](SECURITY.md).

## Commercial support

pgraft is maintained by pgEdge, Inc. For commercial support, contact
<support@pgedge.com>.
