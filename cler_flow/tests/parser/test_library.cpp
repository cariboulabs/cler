#include "../../src/block_library.hpp"
#include <iostream>

int main() {
    clerflow::BlockLibrary library;
    
    std::cout << "Testing BlockLibrary::LoadDesktopBlocks()" << std::endl;
    
    library.LoadDesktopBlocks();
    
    auto blocks = library.GetBlocks();
    std::cout << "\nTotal blocks loaded: " << blocks.size() << std::endl;
    
    // Show categories
    std::cout << "\nCategories found:" << std::endl;
    auto categories = library.GetCategories();
    for (const auto& cat : categories) {
        auto cat_blocks = library.GetBlocksByCategory(cat);
        std::cout << "  " << cat << ": " << cat_blocks.size() << " blocks" << std::endl;
    }
    
    // Show a few sample blocks
    std::cout << "\nSample blocks:" << std::endl;
    int count = 0;
    for (const auto& block : blocks) {
        std::cout << "  - " << block.GetName();
        if (!block.GetTemplateParams().empty()) {
            std::cout << "<";
            bool first = true;
            for (const auto& param : block.GetTemplateParams()) {
                if (!first) std::cout << ", ";
                std::cout << param;
                first = false;
            }
            std::cout << ">";
        }
        std::cout << " (" << block.GetCategory() << ")" << std::endl;
        
        if (++count >= 5) break;
    }
    
    return 0;
}