# Cler Flowgraph Validator

A lightweight Python tool that validates Cler C++ flowgraph code to catch common mistakes before compilation, preventing confusing template errors.

## Features

- ✅ Validates that all blocks have BlockRunners
- ✅ Checks that all runners are added to the flowgraph
- ✅ Detects invalid connections to non-existent blocks
- ✅ Warns about unconnected inputs
- ✅ Catches duplicate block names
- ✅ Provides helpful fix suggestions
- ✅ Works standalone (no IDE required)
- ✅ JSON output for CI/CD integration

## Quick Start

```bash
# Validate a single file
python3 tools/cler-validate.py main.cpp

# Validate multiple files
python3 tools/cler-validate.py desktop_examples/*.cpp

# Output as JSON (for tooling)
python3 tools/cler-validate.py --json src/*.cpp > results.json

# Use custom rules
python3 tools/cler-validate.py --config my-rules.yaml *.cpp
```

## Example Output

```
examples/hello_world.cpp:49:4: ERROR: Block 'throttle' has no BlockRunner
    Suggestion: Add: BlockRunner(&throttle, &<output_channel>)
examples/hello_world.cpp:62:14: ERROR: BlockRunner for 'source2' not added to flowgraph
    Suggestion: Add the BlockRunner for 'source2' to make_*_flowgraph()
examples/hello_world.cpp:73:4: WARNING: Block 'fft' appears to have no input connections
    Suggestion: Connect this block's input or verify it's a source block

✓ Validated 1 file: 2 errors, 1 warning
```

## Command Line Options

- `--json` - Output errors as JSON format
- `--config FILE` - Use custom validation rules (YAML)
- `--quiet` - Only show errors, no summary
- `--werror` - Treat warnings as errors (exit code 1)
- `--no-suggestions` - Don't show fix suggestions

## Integration Options

### 1. Git Pre-commit Hook

```bash
# Install the hook
cp tools/integration/pre-commit-hook.sh .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

Now validation runs automatically before each commit.

### 2. CMake Integration

In your `CMakeLists.txt`:
```cmake
include(tools/integration/cmake-integration.cmake)

# Optional: Enable validation on every build
set(CLER_VALIDATE_ON_BUILD ON)
```

Then use:
```bash
make validate-cler
```

### 3. Makefile Integration

```makefile
include tools/integration/Makefile.example

# Or add this simple target:
validate:
	python3 tools/cler-validate.py src/*.cpp
```

### 4. GitHub Actions

Copy `tools/integration/github-action.yml` to `.github/workflows/cler-validate.yml`

The validator will run on all PRs and commits, posting results as comments.

## Validation Rules

The validator checks for:

1. **Missing BlockRunner** - Every block needs a runner
2. **Runner not in flowgraph** - All runners must be added to the flowgraph
3. **Invalid connections** - Connections must reference existing blocks
4. **Unconnected inputs** - Warns about blocks with no input (unless they're sources)
5. **Duplicate block names** - Variable names must be unique

## Customizing Rules

Create a custom `rules.yaml`:

```yaml
rules:
  missing_runner:
    severity: error  # or warning
  unconnected_input:
    severity: warning
    
patterns:
  # Add custom regex patterns
  block_declaration: 'MyCustomBlock<.*?>\s+(\w+)'
```

## Requirements

- Python 3.6+
- PyYAML (only if using custom config files)

## Limitations

This is a lightweight regex-based validator. It doesn't:
- Perform full C++ parsing
- Validate template types
- Check channel type compatibility
- Understand preprocessor directives

For most common Cler mistakes, this simple approach works well and provides immediate feedback during development.

## Testing

Run the validator on the test file to see it catch various errors:

```bash
python3 tools/cler-validate.py tools/test_validation.cpp
```

## Contributing

Feel free to enhance the validator by:
- Adding new validation rules
- Improving error messages
- Adding more integration options
- Creating VS Code extension

The validator is designed to be simple and extensible!