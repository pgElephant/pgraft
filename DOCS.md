# Documentation Guide

This guide explains how to work with pgraft's documentation.

## Overview

pgraft uses [MkDocs](https://www.mkdocs.org/) with the [Material theme](https://squidfunk.github.io/mkdocs-material/) to generate beautiful, searchable documentation hosted on GitHub Pages.

## Local Development

### Prerequisites

```bash
# Install Python 3.8 or higher
python3 --version

# Install documentation dependencies
pip install -r docs-requirements.txt
```

### Serve Locally

Run a local documentation server:

```bash
mkdocs serve
```

Then open http://127.0.0.1:8000 in your browser.

The server will automatically reload when you edit documentation files.

### Build Documentation

Build static HTML files:

```bash
mkdocs build
```

Output will be in the `site/` directory.

### Build with Strict Mode

Check for errors:

```bash
mkdocs build --strict
```

This will fail on warnings, useful for CI/CD.

## Documentation Structure

```
docs/
├── index.md                          # Home page
├── getting-started/
│   ├── installation.md               # Installation guide
│   └── quick-start.md                # Quick start guide
├── user-guide/
│   ├── tutorial.md                   # Complete tutorial
│   ├── configuration.md              # Configuration reference
│   ├── sql-functions.md              # SQL function reference
│   └── cluster-operations.md         # Cluster operations
├── concepts/
│   ├── architecture.md               # Architecture overview
│   ├── automatic-replication.md      # Replication explained
│   └── split-brain.md                # Split-brain protection
├── operations/
│   ├── monitoring.md                 # Monitoring guide
│   ├── troubleshooting.md            # Troubleshooting guide
│   └── best-practices.md             # Best practices
└── development/
    ├── building.md                   # Building from source
    ├── testing.md                    # Testing guide
    └── contributing.md               # Contributing guide
```

## Writing Documentation

### Markdown Basics

Use standard Markdown syntax:

```markdown
# Heading 1
## Heading 2
### Heading 3

**Bold text**
*Italic text*
`code`

- Bullet list
1. Numbered list

[Link text](url)
```

### Code Blocks

Use fenced code blocks with syntax highlighting:

````markdown
```sql
SELECT * FROM pgraft_get_cluster_status();
```

```bash
pg_ctl restart
```

```c
void my_function(void)
{
    /* C code */
}
```
````

### Admonitions

Use admonitions for important information:

```markdown
!!! note
    This is a note

!!! warning
    This is a warning

!!! success
    This is a success message

!!! danger
    This is a danger/error message

!!! info
    This is an info message
```

### Tabbed Content

Create tabbed content:

```markdown
=== "Tab 1"

    Content for tab 1

=== "Tab 2"

    Content for tab 2
```

### Tables

```markdown
| Column 1 | Column 2 |
|----------|----------|
| Value 1  | Value 2  |
| Value 3  | Value 4  |
```

## Deployment

### GitHub Pages

Documentation deployment is **manual only**. The deployment is handled by GitHub Actions (`.github/workflows/docs.yml`).

To trigger deployment:

1. Go to your GitHub repository
2. Click **Actions** tab
3. Select **Deploy Documentation** workflow
4. Click **Run workflow** → **Run workflow**

### Manual Deployment via CLI

To manually deploy:

```bash
mkdocs gh-deploy
```

This will:
1. Build the documentation
2. Push to the `gh-pages` branch
3. Update GitHub Pages

### Viewing Published Docs

After deployment, docs are available at:

```
https://pgelephant.github.io/pgraft/
```

## Configuration

Documentation configuration is in `mkdocs.yml`:

- **site_name**: Site title
- **theme**: Theme configuration (Material)
- **nav**: Navigation structure
- **markdown_extensions**: Enabled Markdown features
- **plugins**: Enabled plugins

## Best Practices

### Style Guidelines

1. **Use clear headings**: Organize content hierarchically
2. **Keep paragraphs short**: Easier to read
3. **Use code examples**: Show, don't just tell
4. **Add admonitions**: Highlight important information
5. **Link related pages**: Help users navigate
6. **No emojis**: Keep it professional

### Code Examples

- **Always test code examples**: Ensure they work
- **Use syntax highlighting**: Specify language
- **Show expected output**: Help users verify
- **Include full context**: Don't assume knowledge

### Writing Style

- **Use active voice**: "Run this command" not "This command should be run"
- **Be concise**: Remove unnecessary words
- **Use present tense**: "Creates" not "will create"
- **Define acronyms**: First use should be spelled out
- **Use consistent terminology**: Don't switch terms

## Contributing to Docs

1. **Edit markdown files** in `docs/` directory
2. **Test locally** with `mkdocs serve`
3. **Build with strict mode** to check for errors
4. **Commit changes** and push
5. **GitHub Actions** will deploy automatically

## Troubleshooting

### Build Fails

Check for:
- Broken links
- Missing files referenced in `mkdocs.yml`
- Invalid Markdown syntax

Run with strict mode to see errors:
```bash
mkdocs build --strict
```

### Links Not Working

Use relative links:
```markdown
[Link to installation](../getting-started/installation.md)
```

### Images Not Showing

Place images in `docs/assets/` and reference:
```markdown
![Image](../assets/image.png)
```

## Resources

- [MkDocs Documentation](https://www.mkdocs.org/)
- [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/)
- [Python Markdown Extensions](https://facelessuser.github.io/pymdown-extensions/)

