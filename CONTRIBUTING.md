# Contributing to pgraft

Thank you for your interest in contributing to pgraft! This document provides guidelines and instructions for contributing to the project.

## Code of Conduct

By participating in this project, you agree to maintain a respectful and inclusive environment for all contributors.

## How to Contribute

### Reporting Issues

Before creating a new issue, please:
1. Check if the issue already exists in the [issue tracker](https://github.com/pgElephant/pgraft/issues)
2. Use a clear and descriptive title
3. Provide as much context as possible, including:
   - PostgreSQL version
   - Operating system
   - Steps to reproduce
   - Expected vs actual behavior
   - Relevant logs or error messages

### Submitting Pull Requests

1. **Fork the repository** and create your branch from `main`
2. **Follow the coding standards** (see below)
3. **Write tests** for any new functionality
4. **Update documentation** as needed
5. **Ensure all tests pass**
6. **Submit the pull request** with a clear description

### Coding Standards

#### PostgreSQL C Code

- Follow PostgreSQL C coding conventions
- Use `/* */` style comments, not `//`
- Declare variables at the start of functions
- Use the Allman brace style (opening brace on its own line)
- Keep functions under 100 lines when possible
- No compilation warnings allowed

#### Go Code

- Follow standard Go conventions
- Use `gofmt` for formatting
- Write comprehensive comments for exported functions
- Keep functions focused and single-purpose

#### Commit Messages

Use clear, descriptive commit messages:
- Start with a verb in present tense (e.g., "Add", "Fix", "Update")
- Keep the first line under 50 characters
- Provide additional context in the body if needed

Example:
```
Add leader election timeout configuration

Added pgraft.election_timeout GUC parameter to allow
configuring the Raft election timeout. Defaults to 1000ms.
```

## Development Setup

```bash
# Clone the repository
git clone https://github.com/pgElephant/pgraft.git
cd pgraft

# Build the extension
make

# Run tests
make test

# Install (requires sudo)
sudo make install
```

## Testing

All contributions must include appropriate tests:
- Unit tests for new functionality
- Integration tests for complex features
- Test coverage should not decrease

Run tests with:
```bash
make test
```

## Documentation

When adding new features:
- Update the main README.md if needed
- Add SQL function documentation in the extension SQL file
- Update the architecture documentation for significant changes
- Include examples in the docs/ directory

## License

By contributing to pgraft, you agree that your contributions will be licensed under the MIT License.

## Questions?

If you have questions about contributing, please open an issue on GitHub or contact the maintainers.

Thank you for contributing to pgraft! 🎉
