#include "../../src/block_parser.hpp"
#include <iostream>

int main() {
    std::cout << "Testing BlockLibraryScanner" << std::endl;
    
    clerflow::BlockLibraryScanner scanner;
    
    // Scan desktop blocks
    std::cout << "\nScanning desktop_blocks directory..." << std::endl;
    auto library = scanner.scanDesktopBlocks();
    
    std::cout << "Library name: " << library.name << std::endl;
    std::cout << "Is built-in: " << (library.is_builtin ? "Yes" : "No") << std::endl;
    std::cout << "Total blocks found: " << library.blocks.size() << std::endl;
    
    // Show categories
    std::cout << "\nCategories:" << std::endl;
    for (const auto& [category, blocks] : library.blocks_by_category) {
        std::cout << "  " << category << ": " << blocks.size() << " blocks" << std::endl;
    }
    
    // Show a few sample blocks
    std::cout << "\nFirst 5 blocks:" << std::endl;
    int count = 0;
    for (const auto& block : library.blocks) {
        std::cout << "  - " << block.class_name;
        if (!block.template_params.empty()) {
            std::cout << "<";
            bool first = true;
            for (const auto& param : block.template_params) {
                if (!first) std::cout << ", ";
                std::cout << param.name;
                first = false;
            }
            std::cout << ">";
        }
        std::cout << " (" << block.category << ")" << std::endl;
        
        if (++count >= 5) break;
    }
    
    std::cout << "\nScanner test completed successfully!" << std::endl;
    
    return 0;
}