#include "src/block_library.hpp"
#include "src/flow_canvas.hpp"
#include <iostream>
#include <filesystem>

using namespace clerflow;

int main() {
    std::cout << "Testing CLER Flow Library Management System\n";
    std::cout << "==========================================\n\n";
    
    // Create block library
    BlockLibrary library;
    
    // Test 1: Load desktop blocks (should use cache on second run)
    std::cout << "1. Loading desktop blocks...\n";
    library.StartLoadingDesktopBlocks();
    
    // Wait for loading to complete
    while (library.IsLoading()) {
        library.ProcessNextBlocks(10);
        if (library.GetFilesScanned() % 10 == 0) {
            std::cout << "   Progress: " << library.GetFilesScanned() 
                      << "/" << library.GetTotalFiles() 
                      << " files, " << library.GetBlocksFound() << " blocks found\n";
        }
    }
    
    std::cout << "   Complete: " << library.GetBlocksFound() << " blocks loaded\n\n";
    
    // Test 2: Load a custom library (if test directory exists)
    std::string test_lib_path = "/home/alon/repos/cler/desktop_blocks/sources";
    if (std::filesystem::exists(test_lib_path)) {
        std::cout << "2. Loading custom library from: " << test_lib_path << "\n";
        library.LoadLibrary(test_lib_path, "Test Sources");
        
        while (library.IsLoading()) {
            library.ProcessNextBlocks(10);
        }
        
        std::cout << "   Complete: " << library.GetBlocksFound() << " total blocks\n\n";
    }
    
    // Test 3: Update a specific library
    std::cout << "3. Testing library update...\n";
    library.UpdateLibrary("Desktop Blocks");
    
    while (library.IsLoading()) {
        library.ProcessNextBlocks(10);
    }
    
    std::cout << "   Update complete\n\n";
    
    std::cout << "All tests completed successfully!\n";
    std::cout << "Cache location: ~/.cache/cler-flow/block_library_cache.toml\n";
    
    return 0;
}