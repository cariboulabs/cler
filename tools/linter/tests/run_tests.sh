#!/bin/bash
# Test script for Cler validator

echo "Testing Cler Flowgraph Validator..."
echo "=================================="

# Test files that should pass
echo "Testing PASS cases:"
echo "-------------------"

for file in tools/linter/tests/pass_*.cpp; do
    echo -n "Testing $(basename $file)... "
    if python3 tools/linter/cler-validate.py "$file" >/dev/null 2>&1; then
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

for file in tools/linter/tests/fail_*.cpp; do
    echo -n "Testing $(basename $file)... "
    if python3 tools/linter/cler-validate.py "$file" >/dev/null 2>&1; then
        echo "❌ FAIL (should have failed)"
        exit 1
    else
        echo "✅ PASS (correctly failed)"
    fi
done

# Test desktop examples (should all pass)
echo
echo "Testing desktop examples:"
echo "-------------------------"

if python3 tools/linter/cler-validate.py desktop_examples/*.cpp >/dev/null 2>&1; then
    echo "✅ All desktop examples pass"
else
    echo "❌ Some desktop examples failed"
    exit 1
fi

echo
echo "🎉 All tests passed!"
echo
echo "Summary:"
echo "- Pass cases: All correctly passed"
echo "- Fail cases: All correctly failed"
echo "- Desktop examples: All pass (no errors)"
echo
echo "The validator is working correctly!"