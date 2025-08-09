/******************************************************************************************
*                                                                                         *
*    BlockLibrary - Library browser for available blocks                                 *
*                                                                                         *
*    Manages discovered blocks and provides UI for browsing                              *
*                                                                                         *
******************************************************************************************/

#pragma once

#include "block_spec.hpp"
#ifdef HAS_LIBCLANG
#include "block_parser.hpp"
#endif
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
    
#ifdef HAS_LIBCLANG
    // Start loading blocks from desktop_blocks directory
    void StartLoadingDesktopBlocks();
    
    // Process next batch of blocks (call each frame while loading)
    void ProcessNextBlocks(int blocks_per_frame = 1);
    
    // Refresh library from sources
    void RefreshLibrary();
    
    // Progress tracking for loading
    bool IsLoading() const { return is_loading; }
    float GetLoadProgress() const { return load_progress; }
    std::string GetLoadStatus() const { return load_status; }
    std::string GetCurrentFile() const { return current_file; }
    int GetTotalFiles() const { return total_files_to_scan; }
    int GetFilesScanned() const { return files_scanned; }
#endif
    
    // UI
    void Draw(FlowCanvas* canvas);
    
    // Search/filter
    void SetSearchFilter(const std::string& filter);
    
private:
    // Organized by category
    std::map<std::string, std::vector<std::shared_ptr<BlockSpec>>> blocks_by_category;
    
    // All blocks for searching
    std::vector<std::shared_ptr<BlockSpec>> all_blocks;
    
#ifdef HAS_LIBCLANG
    // Parsed metadata from headers
    std::vector<BlockMetadata> parsed_blocks;
    BlockLibraryScanner scanner;
#endif
    
    // UI state
    std::string search_filter;
    std::string selected_category;
    bool show_parsed_blocks = false;
    
#ifdef HAS_LIBCLANG
    // Loading progress state
    bool is_loading = false;
    float load_progress = 0.0f;
    std::string load_status;
    std::string current_file;
    
    // Files to scan
    std::vector<std::string> files_to_scan;
    size_t current_file_index = 0;
    int total_files_to_scan = 0;
    int files_scanned = 0;
    
    // Temporary storage during loading
    std::vector<BlockMetadata> temp_parsed_blocks;
#endif
    
    // Create test blocks for development
    std::shared_ptr<BlockSpec> CreateCWSourceBlock();
    std::shared_ptr<BlockSpec> CreateAddBlock();
    std::shared_ptr<BlockSpec> CreateThrottleBlock();
    std::shared_ptr<BlockSpec> CreateFileSinkBlock();
};

} // namespace clerflow