#!/bin/bash
# Git pre-commit hook for Cler validation
# 
# Installation:
#   cp tools/integration/pre-commit-hook.sh .git/hooks/pre-commit
#   chmod +x .git/hooks/pre-commit
#
# Or create a symlink:
#   ln -s ../../tools/integration/pre-commit-hook.sh .git/hooks/pre-commit

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Find the validator script
VALIDATOR_PATH="tools/cler-validate.py"
if [ ! -f "$VALIDATOR_PATH" ]; then
    # Try relative to git root
    GIT_ROOT=$(git rev-parse --show-toplevel)
    VALIDATOR_PATH="$GIT_ROOT/tools/cler-validate.py"
fi

if [ ! -f "$VALIDATOR_PATH" ]; then
    echo -e "${YELLOW}Warning: Cler validator not found. Skipping validation.${NC}"
    exit 0
fi

# Get all staged C++ files
files=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|hpp|cc|h)$')

if [ -z "$files" ]; then
    # No C++ files to validate
    exit 0
fi

echo "Running Cler flowgraph validation..."

# Run the validator
python3 "$VALIDATOR_PATH" $files

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Cler validation failed!${NC}"
    echo "Please fix the errors before committing."
    echo "To bypass this check (not recommended), use: git commit --no-verify"
    exit 1
else
    echo -e "${GREEN}✓ Cler validation passed!${NC}"
fi

exit 0