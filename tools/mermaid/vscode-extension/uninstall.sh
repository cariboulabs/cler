#!/bin/bash

set -e

echo "=================================================="
echo "  Cler Flowgraph Preview - Uninstall Script"
echo "=================================================="
echo ""

GREEN='\033[0;32m'
NC='\033[0m'

success() {
    echo -e "${GREEN}✓ $1${NC}"
}

# Uninstall VS Code extension
echo "Uninstalling VS Code extension..."
if command -v code >/dev/null 2>&1; then
    code --uninstall-extension cler.cler-flowgraph-preview 2>/dev/null || true
    success "Extension uninstalled from VS Code"
else
    echo "⚠ VS Code CLI not found, skipping extension uninstall"
fi
echo ""

# Remove cler-mermaid from ~/.local/bin
INSTALL_DIR="$HOME/.local/bin"
if [ -f "$INSTALL_DIR/cler-mermaid" ]; then
    echo "Removing cler-mermaid from $INSTALL_DIR..."
    rm -f "$INSTALL_DIR/cler-mermaid"
    success "cler-mermaid removed"
else
    echo "ℹ cler-mermaid not found in $INSTALL_DIR"
fi
echo ""

# Clean build artifacts
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -d "$SCRIPT_DIR/out" ]; then
    echo "Cleaning build artifacts..."
    rm -rf "$SCRIPT_DIR/out"
    rm -f "$SCRIPT_DIR"/*.vsix
    success "Build artifacts cleaned"
fi
echo ""

echo "✅ Uninstall complete"
echo ""
