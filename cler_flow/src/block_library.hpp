/******************************************************************************************
*                                                                                         *
*    BlockLibrary - Library browser for available blocks                                 *
*                                                                                         *
*    Manages discovered blocks and provides UI for browsing                              *
*                                                                                         *
******************************************************************************************/

#pragma once

#include "block_spec.hpp"
#include <memory>
#include <vector>
#include <map>
#include <string>

namespace clerflow {

class FlowCanvas;

class BlockLibrary {
public:
    BlockLibrary();
    ~BlockLibrary() = default;
    
    // Block management
    void AddBlock(std::shared_ptr<BlockSpec> spec);
    void ClearBlocks();
    
    // Import blocks from headers
    void ImportFromHeader(const std::string& header_path);
    void ImportFromDirectory(const std::string& dir_path);
    
    // Load built-in test blocks
    void LoadTestBlocks();
    
    // UI
    void Draw(FlowCanvas* canvas);
    
    // Search/filter
    void SetSearchFilter(const std::string& filter);
    
private:
    // Organized by category
    std::map<std::string, std::vector<std::shared_ptr<BlockSpec>>> blocks_by_category;
    
    // All blocks for searching
    std::vector<std::shared_ptr<BlockSpec>> all_blocks;
    
    // UI state
    std::string search_filter;
    std::string selected_category;
    
    // Create test blocks for development
    std::shared_ptr<BlockSpec> CreateCWSourceBlock();
    std::shared_ptr<BlockSpec> CreateAddBlock();
    std::shared_ptr<BlockSpec> CreateThrottleBlock();
    std::shared_ptr<BlockSpec> CreateFileSinkBlock();
};

} // namespace clerflow