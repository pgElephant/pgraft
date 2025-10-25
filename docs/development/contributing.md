# Contributing

Thank you for your interest in contributing to pgraft! This guide will help you get started.

## Getting Started

1. **Fork the repository** on GitHub
2. **Clone your fork:**
   ```bash
   git clone https://github.com/YOUR_USERNAME/pgraft.git
   cd pgraft
   ```
3. **Create a feature branch:**
   ```bash
   git checkout -b feature/my-new-feature
   ```

## Development Environment

### Setup

Follow the [Building from Source](building.md) guide to set up your development environment.

### Prerequisites

- PostgreSQL 17+
- Go 1.21+
- GCC/Clang
- Make

## Coding Standards

### C Code Standards

pgraft follows **strict PostgreSQL C coding standards**:

#### C89/C90 Compliance

```c
/* All variables at function start */
void my_function(void)
{
    int result;
    char *message;
    bool success;
    
    /* Initialize variables */
    result = 0;
    message = NULL;
    success = false;
    
    /* Function code */
}
```

#### Comments

```c
/*
 * Use C-style comments only
 * Multi-line comments like this
 */

/* Single line comments */

/* NO C++ style comments */
// Don't use these
```

#### Indentation

- **Tabs only** (no spaces)
- Tab width: 4 spaces equivalent
- Opening brace on same line (usually):

```c
if (condition)
{
    /* code */
}
```

#### Naming Conventions

```c
/* Functions: snake_case with pgraft_ prefix */
void pgraft_do_something(void);

/* Variables: snake_case */
int node_count;
char *cluster_id;

/* Macros: UPPER_CASE */
#define PGRAFT_MAX_NODES 7
```

### Go Code Standards

Follow standard Go conventions:

```go
// Use gofmt
gofmt -w src/pgraft_go.go

// Use golint
golint src/pgraft_go.go

// Keep functions focused
// Add comments for exported functions
// Handle errors explicitly
```

## Code Quality

### Before Submitting

**1. No compilation errors or warnings:**
```bash
make clean && make 2>&1 | grep -E "(error|warning)"
# Should be empty
```

**2. Format code:**
```bash
# C code: Follow PostgreSQL style
# Go code:
gofmt -w src/pgraft_go.go
```

**3. Test thoroughly:**
```bash
cd examples
./run.sh --destroy
./run.sh --init
./run.sh --status
```

**4. Check for memory leaks (optional but recommended):**
```bash
valgrind --leak-check=full postgres -D /path/to/data
```

## Testing Requirements

### Minimum Testing

Before submitting a pull request:

1. **Build successfully** with no errors or warnings
2. **Initialize test cluster** successfully
3. **All existing tests pass**
4. **New feature works** as documented

### Testing Your Changes

```bash
# 1. Clean build
make clean && make

# 2. Install
make install

# 3. Test cluster
cd examples
./run.sh --destroy
./run.sh --init
./run.sh --status

# 4. Run manual tests
psql -p 5432 -c "SELECT pgraft_test();"
# ... test your feature ...

# 5. Check logs for errors
tail -100 examples/logs/primary1/postgresql.log | grep -i error
```

## Pull Request Process

### 1. Prepare Your Changes

```bash
# Make your changes
vim src/pgraft_core.c

# Test thoroughly
make clean && make
make install
# Run tests

# Commit with good message
git add src/pgraft_core.c
git commit -m "Add feature: description of change

Detailed explanation of:
- What changed
- Why it changed
- How to use it"
```

### 2. Push to Your Fork

```bash
git push origin feature/my-new-feature
```

### 3. Create Pull Request

On GitHub:

1. Navigate to your fork
2. Click "New Pull Request"
3. Select your feature branch
4. Fill out the template:

```markdown
## Description
Brief description of the change

## Motivation
Why is this change needed?

## Changes
- List of specific changes
- Another change

## Testing
How did you test this?

## Checklist
- [ ] Code compiles without errors
- [ ] Code compiles without warnings
- [ ] Tests pass
- [ ] Documentation updated (if needed)
- [ ] Follows coding standards
```

### 4. Code Review

Maintainers will review your PR:

- Respond to feedback
- Make requested changes
- Push updates to same branch (PR updates automatically)

### 5. Merge

Once approved, maintainers will merge your PR!

## Commit Message Guidelines

### Format

```
Short summary (50 chars or less)

Longer explanation if needed. Wrap at 72 characters.
Explain what and why, not how.

- Bullet points are fine
- Use present tense: "Add feature" not "Added feature"
```

### Good Examples

```
Add support for dynamic cluster membership

Allows nodes to be added and removed from cluster at runtime
without restarting. Uses Raft's joint consensus protocol.

Fix memory leak in log compaction

Free allocated memory after snapshot creation.

Improve error handling in network layer

Add retries for transient network failures and better
error messages for debugging.
```

## Documentation

### Update Documentation

If your change affects:

- **User-facing features**: Update relevant `.md` files
- **Configuration**: Update GUC documentation
- **SQL functions**: Update function reference
- **Architecture**: Update architecture docs

### Documentation Style

```markdown
# Use sentence case for headings

Write clear, concise sentences. Use active voice.

Provide examples:
\`\`\`sql
SELECT pgraft_example();
\`\`\`

Use admonitions for important information:
!!! warning
    Important warning here
```

## Areas for Contribution

We welcome contributions in these areas:

### High Priority

- **Testing**: More test cases and scenarios
- **Documentation**: Examples, tutorials, guides
- **Bug fixes**: Check GitHub issues
- **Performance**: Optimization improvements

### Medium Priority

- **Monitoring**: Better metrics and observability
- **Security**: TLS, authentication
- **Platform support**: Windows, BSD

### Nice to Have

- **Tools**: Cluster management utilities
- **Examples**: Real-world use cases
- **Integrations**: Connection poolers, HA tools

## Bug Reports

### Before Reporting

1. **Search existing issues**: Check if already reported
2. **Test latest version**: Bug may be fixed
3. **Reproduce**: Verify you can reproduce it

### Creating Good Bug Reports

Use this template:

```markdown
**Describe the bug**
Clear description of the bug

**To Reproduce**
Steps to reproduce:
1. Do X
2. Do Y
3. See error

**Expected behavior**
What should happen

**Actual behavior**
What actually happens

**Environment**
- OS: [e.g. Ubuntu 22.04]
- PostgreSQL version: [e.g. 17.2]
- pgraft version: [e.g. 1.0.0]

**Logs**
\`\`\`
Relevant log output
\`\`\`

**Configuration**
\`\`\`ini
pgraft.cluster_id = 'test'
...
\`\`\`
```

## Feature Requests

### Creating Feature Requests

```markdown
**Feature Description**
What feature do you want?

**Use Case**
Why is this needed? What problem does it solve?

**Proposed Solution**
How might this work?

**Alternatives**
Other solutions you've considered?

**Additional Context**
Any other information
```

## Communication

- **GitHub Issues**: Bug reports, feature requests
- **Pull Requests**: Code contributions
- **Discussions**: Questions, ideas, help

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

## Code of Conduct

### Our Standards

- Be respectful and inclusive
- Welcome newcomers
- Accept constructive criticism gracefully
- Focus on what's best for the project

### Unacceptable Behavior

- Harassment or discrimination
- Trolling or insulting comments
- Publishing others' private information
- Other unprofessional conduct

## Recognition

Contributors will be recognized in:

- Release notes
- CONTRIBUTORS file
- Git history

Thank you for contributing to pgraft!

