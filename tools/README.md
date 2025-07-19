# Cler Tools

Development tools and utilities for the Cler DSP framework.

## Available Tools

### 🔍 Linter (`/linter/`)

Static analysis tool for Cler C++ flowgraph code that catches common mistakes before compilation.

**Key Features:**
- Validates BlockRunner configuration
- Detects invalid connections
- Supports streamlined and flowgraph modes
- Template-aware parsing
- Configurable rules and output formats

**Quick Usage:**
```bash
# Validate files
python3 tools/linter/cler-validate.py desktop_examples/*.cpp

# JSON output for CI/CD
python3 tools/linter/cler-validate.py --json src/*.cpp
```

See [`linter/README.md`](linter/README.md) for complete documentation.

### 🔧 Integration (`/integration/`)

Ready-to-use integration examples for build systems and workflows:

- **`pre-commit-hook.sh`** - Git pre-commit validation
- **`cmake-integration.cmake`** - CMake build integration
- **`github-action.yml`** - GitHub Actions workflow
- **`Makefile.example`** - Make integration examples

## Future Tools

The tools directory is organized to accommodate additional development utilities:

- **Benchmarking**: Performance measurement tools
- **Code Generation**: Template and boilerplate generators  
- **Documentation**: API documentation generators
- **Testing**: Specialized testing utilities
- **Analysis**: Performance profiling and optimization tools

## Directory Structure

```
tools/
├── README.md              # This file
├── linter/                # Static analysis linter
│   ├── cler-validate.py   # Main linter script
│   ├── rules.yaml         # Configuration
│   ├── README.md          # Linter documentation
│   └── tests/             # Test suite
└── integration/           # Build system integrations
    ├── pre-commit-hook.sh
    ├── cmake-integration.cmake
    ├── github-action.yml
    └── Makefile.example
```

## Requirements

- **Python 3.6+** (for linter)
- **PyYAML** (for custom linter configurations)

## Contributing

When adding new tools:

1. Create a dedicated subdirectory under `tools/`
2. Include a README.md with usage instructions
3. Add integration examples where applicable
4. Update this main README with tool description
5. Include test cases for validation tools

The tools are designed to be:
- **Standalone**: Work without complex dependencies
- **Integrable**: Easy to add to existing workflows  
- **Configurable**: Support customization for different projects
- **Documented**: Clear usage instructions and examples