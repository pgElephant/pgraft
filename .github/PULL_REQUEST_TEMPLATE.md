## Summary

Briefly describe what this PR changes and why.

Fixes #(issue)

## Type of change

- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that changes existing behavior)
- [ ] Documentation only

## How has this been tested?

- [ ] `make` and `make install` succeed
- [ ] `CREATE EXTENSION pgraft` succeeds and the smoke queries pass
- [ ] A multi-node cluster was brought up (`scripts/test_cluster.sh` or
      `scripts/pgraft_cluster.py`) and elected a leader
- [ ] Built cleanly against PostgreSQL versions: (list, e.g. 16/17/18)

Describe any additional manual testing.

## Checklist

- [ ] Code follows the PostgreSQL source formatting and error-message style
- [ ] Builds with no new warnings (the build uses `-Wall -Wextra -Werror`)
- [ ] No PostgreSQL API is called from the Go Raft thread
- [ ] Shared-memory access is guarded by the appropriate lock
- [ ] New SQL objects live in the `pgraft` schema and are granted deliberately
- [ ] Updated `docs/` where relevant
- [ ] Added an entry to `CHANGELOG.md` under `[Unreleased]`
