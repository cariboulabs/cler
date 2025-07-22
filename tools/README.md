# Cler Tools

Development tools and utilities for the Cler DSP framework.

## Installation

Install using `uv`:

```bash
cd tools
uv pip install -e .
```

Or using regular pip:

```bash
cd tools
pip install -e .
```

## Available Tools

### 🔍 cler-validate

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
cler-validate desktop_examples/*.cpp

# JSON output for CI/CD
cler-validate --json src/*.cpp
```

See [`cler_tools/linter/README.md`](cler_tools/linter/README.md) for complete documentation.

### 📊 cler-viz

Generates visual representations of Cler flowgraphs as SVG images.

**Key Features:**
- Automatic flowgraph extraction from C++ code
- Multiple layout algorithms (hierarchical, circular, force-directed)
- Customizable styling and channel labels
- Batch processing for multiple files

**Quick Usage:**
```bash
# Generate SVG for a single file
cler-viz file.cpp -o output.svg

# Process multiple files
cler-viz *.cpp --output-dir ./diagrams/

# Use circular layout with channel labels
cler-viz main.cpp --layout circular --show-channels
```

See [`cler_tools/viz/README.md`](cler_tools/viz/README.md) for complete documentation.

### 🔧 Integration (`/integration/`)

Ready-to-use integration examples for build systems and workflows:

- **`pre-commit-hook.sh`** - Git pre-commit validation
- **`cmake-integration.cmake`** - CMake build integration
- **`github-action.yml`** - GitHub Actions workflow
- **`Makefile.example`** - Make integration examples

## Future Tools

The tools directory is organized to accommodate additional development utilities:

- **cler-flow**: Generate Cler C++ code from visual flowgraph definitions
- **cler-bench**: Performance benchmarking utilities
- **cler-doc**: Documentation generator for Cler blocks

## Package Structure

```
tools/
├── README.md              # This file
├── pyproject.toml         # Python package configuration
├── cler_tools/            # Main package
│   ├── common/            # Shared utilities
│   │   ├── cpp_parser.py  # C++ parsing logic
│   │   └── patterns.py    # Regex patterns
│   ├── linter/            # Validation tool
│   │   ├── validate.py    # Main validator
│   │   └── tests/         # Test suite
│   └── viz/               # Visualization tool
│       ├── visualize.py   # Main script
│       ├── graph_builder.py
│       ├── svg_renderer.py
│       ├── layout.py      # Layout algorithms
│       └── tests/         # Test suite
└── integration/           # Build system integrations
    ├── pre-commit-hook.sh
    ├── cmake-integration.cmake
    ├── github-action.yml
    └── Makefile.example
```

## Requirements

- **Python 3.8+**
- **PyYAML** (for configuration files)

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