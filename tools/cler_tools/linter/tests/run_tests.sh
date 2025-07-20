#!/bin/bash
# Test script for Cler validator

echo "Testing Cler Flowgraph Validator..."
echo "=================================="

# Test files that should pass
echo "Testing PASS cases:"
echo "-------------------"

for file in pass_*.cpp; do
    echo -n "Testing $(basename $file)... "
    if python -m cler_tools.linter.validate "$file" >/dev/null 2>&1; then
        echo "✅ PASS"
    else
        echo "❌ FAIL (should have passed)"
        exit 1
    fi
done

# Test files that should fail
echo
echo "Testing FAIL cases:"
echo "-------------------"

for file in fail_*.cpp; do
    echo -n "Testing $(basename $file)... "
    if python -m cler_tools.linter.validate "$file" >/dev/null 2>&1; then
        echo "❌ FAIL (should have failed)"
        exit 1
    else
        echo "✅ PASS (correctly failed)"
    fi
done

echo
echo "🎉 All tests passed!"
echo
echo "Summary:"
echo "- Pass cases: All correctly passed"
echo "- Fail cases: All correctly failed"
echo
echo "The validator is working correctly!"