#!/bin/bash
# Test script for cler-viz tool

echo "Running cler-viz tests..."

# Test basic flowgraph
echo "Testing basic flowgraph..."
python -m cler_tools.viz.visualize test_basic.cpp -o test_basic.svg
if [ -f test_basic.svg ]; then
    echo "✓ Basic test passed"
else
    echo "✗ Basic test failed"
fi

# Test complex flowgraph
echo "Testing complex flowgraph..."
python -m cler_tools.viz.visualize test_complex.cpp -o test_complex.svg
if [ -f test_complex.svg ]; then
    echo "✓ Complex test passed"
else
    echo "✗ Complex test failed"
fi

# Test circular layout
echo "Testing circular layout..."
python -m cler_tools.viz.visualize test_basic.cpp -o test_circular.svg --layout circular
if [ -f test_circular.svg ]; then
    echo "✓ Circular layout test passed"
else
    echo "✗ Circular layout test failed"
fi

# Test compact mode
echo "Testing compact mode..."
python -m cler_tools.viz.visualize test_basic.cpp -o test_compact.svg --compact
if [ -f test_compact.svg ]; then
    echo "✓ Compact mode test passed"
else
    echo "✗ Compact mode test failed"
fi

# Clean up
rm -f test_*.svg

echo "All tests completed!"