# Cler Flowgraph Linter

A lightweight validation tool for Cler C++ flowgraph code that catches common mistakes before compilation, preventing confusing template errors.

## Overview

The Cler linter analyzes C++ flowgraph code and validates:
- Block declarations and BlockRunner usage
- Channel connections between blocks
- Missing blocks in flowgraph definitions
- BlockRunner argument order
- Multiple connections to same input channel
- Unconnected blocks (sources without outputs, sinks without inputs)
- Duplicate display names (warnings)
- Streamlined vs flowgraph mode detection

## Quick Start

```bash
# Install in development mode
cd tools && uv venv && source .venv/bin/activate
uv pip install -e .

# Validate files
python cler_tools/linter/validate.py ../desktop_examples/hello_world.cpp

# JSON output for tooling integration
python cler_tools/linter/validate.py --json *.cpp

# Custom rules configuration
python cler_tools/linter/validate.py --config custom_rules.yaml main.cpp
```

## Architecture

### Core Components

**`validate.py`** - Main CLI entry point
- Argument parsing and file processing
- Configuration loading (YAML)
- Output formatting (text/JSON)

**`validator.py`** - Validation engine and rules
- `ValidationRule` abstract base class
- `RuleEngine` for coordinating multiple rules
- Individual validation rules (missing runners, invalid connections, etc.)

**`../common/cpp_parser.py`** - C++ parsing logic
- Block and connection extraction
- Template parameter parsing
- Channel direction inference
- Shared with visualization tools

### Data Flow

```
C++ File → ClerParser → FlowGraph → RuleEngine → ValidationErrors → Output
```

## Parser Capabilities

The parser extracts:

**Blocks**: Type, name, template parameters, constructor args
```cpp
SourceCWBlock<float> source("Source", 1.0f, 440.0f, 1000);
// → Block(name='source', type='SourceCWBlock', template_params='float', ...)
```

**Connections**: Source/target blocks and channels
```cpp
cler::BlockRunner(&source, &adder.in[0])
// → Connection(source_block='source', target_block='adder', target_channel='in[0]')
```

**Execution Mode**: Flowgraph vs streamlined detection
```cpp
auto flowgraph = make_desktop_flowgraph(...)  // → Flowgraph mode
source.procedure(&sink.in);                   // → Streamlined mode
```

## Validation Rules

### MissingRunnerRule
Detects blocks declared but not added to flowgraph:
```cpp
SourceCWBlock<float> source("Source", ...);
AddBlock<float> adder("Adder", 2);           // ← Missing from flowgraph!

auto flowgraph = make_desktop_flowgraph(
    cler::BlockRunner(&source, &adder.in[0])
    // Missing: cler::BlockRunner(&adder, ...)
);
```

### InvalidConnectionRule  
Validates block and channel references:
```cpp
cler::BlockRunner(&source, &undefined_block.in)  // ← Block doesn't exist
cler::BlockRunner(&source, &sink.wrong_channel)  // ← Channel validation
```

### BlockRunnerOrderRule
Detects incorrect BlockRunner argument order:
```cpp
cler::BlockRunner(&sink.in, &adder)          // ← ERROR: channels before block
// Should be:
cler::BlockRunner(&adder, &sink.in)          // ← Block first, then channels
```

### MultipleConnectionsRule
Prevents multiple connections to same input channel:
```cpp
cler::BlockRunner(&source1, &sink.in),
cler::BlockRunner(&source2, &sink.in),  // ← ERROR: sink.in already connected  
```

### UnconnectedBlocksRule
Detects blocks added to flowgraph but not properly connected:
```cpp
cler::BlockRunner(&adder),        // ← ERROR: adder has no outputs connected
cler::BlockRunner(&sink)          // ← ERROR: sink has no inputs connected
```

### DuplicateDisplayNamesRule
Warns about blocks with duplicate display names:
```cpp
SourceCWBlock<float> source1("Source", ...);  
SourceCWBlock<float> source2("Source", ...);  // ← WARNING: duplicate "Source"
```

## Configuration

Create `rules.yaml` for custom rule configuration:

```yaml
rules:
  missing_runner:
    severity: error
    enabled: true
  blockrunner_order:
    severity: error
    enabled: true
  multiple_connections:
    severity: error
    enabled: true
  unconnected_blocks:
    severity: error
    enabled: true
  duplicate_display_names:
    severity: warning
    enabled: true
  invalid_connection:
    severity: error
    enabled: false
```

## Testing Framework

**Test Structure**: `tests/` directory with pass/fail cases
- `pass_*.cpp` - Should validate without errors
- `fail_*.cpp` - Should produce specific validation errors

**Running Tests**:
```bash
bash cler_tools/linter/tests/run_tests.sh
```

**Test Categories**:
- Basic flowgraph validation
- Streamlined mode detection
- Template type parsing
- Variadic output handling
- Error cases (missing runners, invalid connections, wrong argument order)
- Connection validation (multiple connections, unconnected blocks)
- Warning cases (duplicate display names)

## Extending the Linter

### Adding New Rules

1. **Create Rule Class**:
```python
class MyCustomRule(ValidationRule):
    def get_rule_name(self) -> str:
        return "my_custom_rule"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        # Your validation logic here
        return errors
```

2. **Register in RuleEngine**:
```python
# In validator.py RuleEngine.__init__()
self.rule_registry['my_custom_rule'] = MyCustomRule
```

3. **Add Test Cases**:
- Create `pass_my_feature.cpp` and `fail_my_feature.cpp`
- Update test suite

### Parser Extensions

The parser is shared with visualization tools - changes benefit multiple tools:

**Block Detection**: Modify `PATTERNS['block_instance']` in `patterns.py`
**Connection Parsing**: Extend `_extract_runners()` method
**Channel Inference**: Enhance `_infer_channel_directions()`

## Integration Examples

### Git Pre-commit Hook
```bash
#!/bin/bash
python cler_tools/linter/validate.py $(git diff --cached --name-only --diff-filter=ACM | grep '\.cpp$')
```

### CMake Integration
```cmake
find_program(CLER_VALIDATE cler-validate)
if(CLER_VALIDATE)
    add_custom_target(validate
        COMMAND ${CLER_VALIDATE} ${CMAKE_SOURCE_DIR}/src/*.cpp
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
```

### VS Code Integration
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Validate Cler Code",
            "type": "shell",
            "command": "python",
            "args": ["cler_tools/linter/validate.py", "${file}"],
            "group": "build"
        }
    ]
}
```

## Development Workflow

1. **Setup Environment**:
```bash
cd tools && uv venv && source .venv/bin/activate && uv pip install -e .
```

2. **Test Changes**:
```bash
python cler_tools/linter/validate.py test_file.cpp
bash cler_tools/linter/tests/run_tests.sh
```

3. **Debug Parser**:
```python
from cler_tools.common import ClerParser
parser = ClerParser()
flowgraph = parser.parse_file(content, "debug.cpp")
print("Blocks:", flowgraph.blocks)
print("Connections:", flowgraph.connections)
```

## Future Extensions

The shared validation framework enables:
- **cler-flow**: Code generation tool using same parser
- **IDE Integration**: Language server protocol support  
- **CI/CD**: Automated validation in build pipelines
- **Custom Rules**: Project-specific validation logic

## Common Patterns

**Error Messages**: Include line numbers, suggestions, context
**Rule Configuration**: Support enable/disable and severity levels
**Performance**: Batch file processing, parallel validation
**Extensibility**: Plugin architecture for custom rules