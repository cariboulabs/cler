#!/bin/bash
# Print all CLER CMake options and exit

# Check for --details flag
SHOW_DETAILS=false
if [[ "$1" == "--details" ]] || [[ "$1" == "-d" ]]; then
    SHOW_DETAILS=true
fi

if $SHOW_DETAILS; then
    # Show detailed explanations
    echo "==============================================="
    echo "      CLER CMake Options - Detailed Info"
    echo "==============================================="
    echo ""
    
    echo "Build Type Options:"
    echo "  CMAKE_BUILD_TYPE:"
    echo "    - Debug: Includes debugging symbols, no optimization"
    echo "    - Release: Optimized build without debug info"
    echo ""
    
    echo "CLER Build Options:"
    echo "  CLER_BUILD_BLOCKS:"
    echo "    - Enables compilation of DSP processing blocks"
    echo "    - Required for most CLER functionality"
    echo "    - Disabling reduces binary size for minimal builds"
    echo ""
    
    echo "  CLER_BUILD_BLOCKS_GUI:"
    echo "    - Includes graphical user interface blocks"
    echo "    - Requires GUI libraries (Qt/GTK)"
    echo "    - Only effective when CLER_BUILD_BLOCKS=ON"
    echo ""
    
    echo "  CLER_BUILD_BLOCKS_LIQUID:"
    echo "    - Integrates Liquid-DSP library blocks"
    echo "    - Provides advanced signal processing capabilities"
    echo "    - Only effective when CLER_BUILD_BLOCKS=ON"
    echo ""
    
    echo "  CLER_BUILD_EXAMPLES:"
    echo "    - Compiles example applications"
    echo "    - Useful for learning and testing"
    echo "    - Includes both desktop and embedded examples"
    echo "    - Only effective when CLER_BUILD_BLOCKS=ON"
    echo ""
    
    echo "  CLER_BUILD_PERFORMANCE:"
    echo "    - Builds performance benchmarking tools"
    echo "    - Helps optimize system configuration"
    echo "    - Increases build time"
    echo ""
    
    echo "  CLER_BUILD_TESTS:"
    echo "    - Compiles unit test suite"
    echo "    - Essential for development/debugging"
    echo "    - Not needed for production builds"
    echo ""
    
    echo "Runtime Configuration:"
    echo "  CLER_DEFAULT_MAX_WORKERS:"
    echo "    - Sets maximum thread pool size"
    echo "    - Memory usage: ~1KB per worker thread"
    echo "    - Guidelines by system type:"
    echo "      * Embedded systems (8): Minimal memory footprint"
    echo "      * Desktop systems (16): Good balance for most uses"
    echo "      * Server systems (32+): Maximum parallelism"
    echo ""
    
    echo "Important Notes:"
    echo "  - CLER_BUILD_BLOCKS_GUI and CLER_BUILD_BLOCKS_LIQUID have no effect unless CLER_BUILD_BLOCKS is ON"
    echo "  - CLER_BUILD_EXAMPLES will build both desktop and embedded examples, but only if CLER_BUILD_BLOCKS is ON"
    echo ""
    
    echo "Usage:"
    echo "  cmake -D<OPTION>=<VALUE> .."
    echo ""
    
    echo "Examples:"
    echo "  # Embedded build with conservative worker limit"
    echo "  cmake -DCMAKE_BUILD_TYPE=Release -DCLER_DEFAULT_MAX_WORKERS=8 .."
    echo ""
    echo "  # Desktop build with more thread pool capacity"
    echo "  cmake -DCMAKE_BUILD_TYPE=Release -DCLER_DEFAULT_MAX_WORKERS=16 .."
    echo ""
    echo "  # High-performance server build"
    echo "  cmake -DCMAKE_BUILD_TYPE=Release -DCLER_DEFAULT_MAX_WORKERS=32 .."
    echo ""
else
    # Show normal output - clean table only
    echo "==============================================="
    echo "         Available CLER CMake Options"
    echo "==============================================="
    echo "Build Type:"
    printf "  %-40s %-40s %s\n" "-DCMAKE_BUILD_TYPE=Debug|Release" "Build type configuration" "Default: Release"
    echo ""
    echo "Build Options:"
    printf "  %-40s %-40s %s\n" "-DCLER_BUILD_BLOCKS=ON|OFF"            "Enable DSP blocks" "Default: ON"
    printf "  %-40s %-40s %s\n" "-DCLER_BUILD_BLOCKS_GUI=ON|OFF"        "Include GUI-related blocks" "Default: ON"
    printf "  %-40s %-40s %s\n" "-DCLER_BUILD_BLOCKS_LIQUID=ON|OFF"     "Include Liquid-DSP blocks" "Default: ON"
    printf "  %-40s %-40s %s\n" "-DCLER_BUILD_EXAMPLES=ON|OFF"          "Build example binaries" "Default: ON"
    printf "  %-40s %-40s %s\n" "-DCLER_BUILD_PERFORMANCE=ON|OFF"       "Build performance tests" "Default: OFF"
    printf "  %-40s %-40s %s\n" "-DCLER_BUILD_TESTS=ON|OFF"             "Build unit tests" "Default: OFF"
    echo ""
    echo "Runtime Configuration:"
    printf "  %-40s %-40s %s\n" "-DCLER_DEFAULT_MAX_WORKERS=N"          "Max workers for thread pools" "Default: 8"
    echo ""
    echo "Debug Options:"
    printf "  %-40s %-40s %s\n" "-DCLER_VMEM_DEBUG=ON|OFF"              "Enable debug information for double buffering" "Default: OFF"
    echo ""
    echo "--details or -d for detailed explanations"
    echo "==============================================="
fi