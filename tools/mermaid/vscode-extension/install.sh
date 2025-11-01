#!/bin/bash

set -e  # Exit on error

echo "=================================================="
echo "  Cler Flowgraph Preview - Installation Script"
echo "=================================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Helper functions
error() {
    echo -e "${RED}✗ Error: $1${NC}" >&2
    exit 1
}

success() {
    echo -e "${GREEN}✓ $1${NC}"
}

warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

info() {
    echo "ℹ $1"
}

# Spinner for long-running commands
spinner() {
    local pid=$1
    local message=$2
    local spinstr='|/-\\'
    local delay=0.1

    while kill -0 $pid 2>/dev/null; do
        local temp=${spinstr#?}
        printf "\r  %s [%c]  " "$message" "$spinstr"
        spinstr=$temp${spinstr%"$temp"}
        sleep $delay
    done

    wait $pid
    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        printf "\r  %s " "$message"
        echo -e "${GREEN}✓${NC}\033[K"  # \033[K clears to end of line
    else
        printf "\r  %s " "$message"
        echo -e "${RED}✗${NC}\033[K"
        return $exit_code
    fi
}

# Run command with spinner
run_with_spinner() {
    local message=$1
    shift

    "$@" > /tmp/install_output.log 2>&1 &
    local pid=$!

    spinner $pid "$message" || {
        # Check if it's the VSCode install crash (extension actually installed)
        if grep -q "successfully installed" /tmp/install_output.log; then
            return 0  # Ignore the crash, extension is installed
        fi
        echo ""
        error "Command failed. Log:\n$(tail -20 /tmp/install_output.log)"
    }
}

# Check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Step 1: Check prerequisites
echo "Step 1/7: Checking prerequisites..."
echo ""

MISSING_DEPS=()

if ! command_exists node; then
    MISSING_DEPS+=("node")
fi

if ! command_exists npm; then
    MISSING_DEPS+=("npm")
fi

if ! command_exists cmake; then
    MISSING_DEPS+=("cmake")
fi

if ! command_exists g++; then
    MISSING_DEPS+=("g++")
fi

if ! command_exists code; then
    MISSING_DEPS+=("code (VS Code CLI)")
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    error "Missing required dependencies: ${MISSING_DEPS[*]}\n\nPlease install them first:\n  sudo apt-get install nodejs npm cmake g++\n  And install VS Code from https://code.visualstudio.com/"
fi

success "All prerequisites found"
echo ""

# Step 2: Build cler-mermaid tool
echo "Step 2/7: Building cler-mermaid tool..."
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MERMAID_DIR="$SCRIPT_DIR/.."
MERMAID_BUILD="$MERMAID_DIR/build"

if [ ! -d "$MERMAID_DIR" ]; then
    error "Cannot find mermaid directory at: $MERMAID_DIR"
fi

cd "$MERMAID_DIR"

# Clean and build
if [ -d "$MERMAID_BUILD" ]; then
    info "Cleaning previous build..."
    rm -rf "$MERMAID_BUILD"
fi

run_with_spinner "Configuring with CMake..." cmake -B build -S .
run_with_spinner "Building C++ tool..." cmake --build build

if [ ! -f "$MERMAID_BUILD/cler-mermaid" ]; then
    error "Build succeeded but executable not found"
fi

success "cler-mermaid built successfully"
echo ""

# Step 3: Install cler-mermaid to ~/.local/bin
echo "Step 3/7: Installing cler-mermaid..."
echo ""

INSTALL_DIR="$HOME/.local/bin"

run_with_spinner "Installing to ~/.local/bin..." cmake --install "$MERMAID_BUILD" --prefix "$HOME/.local"

success "cler-mermaid installed to $INSTALL_DIR"

# Check if ~/.local/bin is in PATH
if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    warning "~/.local/bin is not in your PATH"
    info "Add this to your ~/.bashrc or ~/.zshrc:"
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
fi

# Step 4: Install npm dependencies
echo "Step 4/7: Installing extension dependencies..."
echo ""

cd "$SCRIPT_DIR"

if [ ! -f "package.json" ]; then
    error "package.json not found in $SCRIPT_DIR"
fi

run_with_spinner "Installing npm dependencies..." npm install

success "Dependencies installed"
echo ""

# Step 5: Compile TypeScript
echo "Step 5/7: Compiling extension..."
echo ""

run_with_spinner "Compiling TypeScript..." npm run compile

success "Extension compiled"
echo ""

# Step 6: Install Markdown Preview Mermaid Support (before Cler extension in case it crashes)
echo "Step 6/7: Installing Mermaid preview extension..."
echo ""

run_with_spinner "Installing bierner.markdown-mermaid..." code --install-extension bierner.markdown-mermaid

success "Mermaid preview extension installed"
echo ""

# Step 7: Package and install extension
echo "Step 7/7: Packaging and installing VS Code extension..."
echo ""

# Check if vsce is available (should be in node_modules)
if [ ! -f "node_modules/.bin/vsce" ]; then
    run_with_spinner "Installing vsce..." npm install --save-dev vsce
fi

run_with_spinner "Packaging extension..." npx vsce package --allow-star-activation

# Find the generated .vsix file
VSIX_FILE=$(ls -t *.vsix 2>/dev/null | head -1)

if [ -z "$VSIX_FILE" ]; then
    error "No .vsix file generated"
fi

run_with_spinner "Installing to VS Code..." code --install-extension "$VSIX_FILE"

success "Extension installed: $VSIX_FILE"
echo ""

# Final summary
echo "=================================================="
echo "  ✅ Installation Complete!"
echo "=================================================="
echo ""
echo "Extension: cler-flowgraph-preview v1.0.0"
echo "Tool location: $INSTALL_DIR/cler-mermaid"
echo ""
echo "To use:"
echo "  1. Open a Cler C++ file in VS Code"
echo "  2. Click the graph icon in the editor toolbar"
echo "     OR run command: 'Open Cler Flowgraph Preview'"
echo ""
echo "Configuration (optional):"
echo "  - Open VS Code settings"
echo "  - Search for 'cler'"
echo "  - Configure tool path, auto-update, etc."
echo ""
echo "To uninstall:"
echo "  - Run: ./uninstall.sh"
echo "  - Or manually: code --uninstall-extension cler.cler-flowgraph-preview"
echo ""
