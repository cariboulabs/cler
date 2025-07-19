# Cler Flowgraph Linter

A static analysis tool for Cler DSP framework C++ code that catches common flowgraph configuration mistakes before compilation.

## Overview

The Cler linter prevents confusing template compilation errors by validating:
- ✅ **Missing BlockRunners**: Ensures all blocks have runners
- ✅ **Invalid Connections**: Catches connections to undeclared blocks  
- ✅ **Streamlined Mode Support**: Handles manual procedure calls correctly
- ✅ **Template Compatibility**: Works with complex template declarations

## Quick Start

```bash
# Validate single file
python3 tools/linter/cler-validate.py main.cpp

# Validate multiple files
python3 tools/linter/cler-validate.py src/*.cpp

# JSON output for CI/CD
python3 tools/linter/cler-validate.py --json *.cpp

# Custom rules
python3 tools/linter/cler-validate.py --config tools/linter/rules.yaml *.cpp
```

## Features

### Validation Rules
- **Missing Runner Detection**: Catches blocks without BlockRunners
- **Connection Validation**: Verifies all connections reference existing blocks
- **Mode Detection**: Distinguishes flowgraph vs streamlined modes
- **Template Support**: Handles nested templates like `FanoutBlock<std::complex<float>>`

### Output Formats
- **Human-readable**: Clear error messages with suggestions
- **JSON**: Structured output for automation
- **Configurable**: Custom severity levels and rules

### Integration Options
- **Pre-commit hooks**: Catch errors before they reach the repository
- **CI/CD**: GitHub Actions, GitLab CI integration
- **Build systems**: CMake, Make integration
- **IDEs**: VS Code extension support (planned)

## Configuration

The linter uses `rules.yaml` for configuration:

```yaml
patterns:
  block_instance: '(\w+Block)(?:<.*?>)?\s+(\w+)\s*\('
  # ... other patterns

rules:
  missing_runner: 
    severity: 'error'
  invalid_connection: 
    severity: 'error'
  # ... other rules
```

## Directory Structure

```
tools/linter/
├── cler-validate.py     # Main linter script
├── rules.yaml          # Default configuration
├── README.md           # This file
└── tests/              # Test suite
    ├── run_tests.sh    # Test runner
    ├── pass_*.cpp      # Files that should pass
    └── fail_*.cpp      # Files that should fail
```

## Testing

```bash
# Run full test suite
bash tools/linter/tests/run_tests.sh

# Test specific file
python3 tools/linter/cler-validate.py tools/linter/tests/fail_missing_runner.cpp
```

## Integration Examples

See `../integration/` for:
- `pre-commit-hook.sh` - Git pre-commit integration
- `cmake-integration.cmake` - CMake build integration  
- `github-action.yml` - GitHub Actions workflow
- `Makefile.example` - Make integration examples

## Command Line Options

```
usage: cler-validate.py [-h] [--json] [--config CONFIG] [--quiet] [--werror] [--no-suggestions] files [files ...]

Options:
  --json              Output errors as JSON
  --config CONFIG     Path to custom rules configuration file
  --quiet             Only show errors, no summary
  --werror            Treat warnings as errors (exit code 1)
  --no-suggestions    Suppress fix suggestions
```

## Exit Codes

- `0`: No errors found
- `1`: Validation errors found or file processing failed
- `1`: Warnings found when `--werror` is used