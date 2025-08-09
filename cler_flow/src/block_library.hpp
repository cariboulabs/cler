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
#include "block_cache.hpp"
#endif
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

namespace clerflow {

class FlowCanvas;

class BlockLibrary {
public:
    BlockLibrary();
    ~BlockLibrary();
    
    // Block management
    void AddBlock(std::shared_ptr<BlockSpec> spec);
    void ClearBlocks();
    void ClearLibrary(const std::string& library_name);
    
    // Import blocks from headers
    void ImportFromHeader(const std::string& header_path);
    void ImportFromDirectory(const std::string& dir_path);
    
    // Load built-in test blocks
    void LoadTestBlocks();
    
#ifdef HAS_LIBCLANG
    // Start loading blocks from desktop_blocks directory
    void StartLoadingDesktopBlocks();
    
    // Load a custom library
    void LoadLibrary(const std::string& path, const std::string& library_name);
    
    // Update a single block
    void UpdateBlock(std::shared_ptr<BlockSpec> block);
    
    // Update an entire library
    void UpdateLibrary(const std::string& library_name);
    
    // Process next batch of blocks (call each frame while loading)
    void ProcessNextBlocks(int blocks_per_frame = 1);
    
    // Progress tracking for loading
    bool IsLoading() const { return is_loading; }
    float GetLoadProgress() const { return load_progress; }
    std::string GetLoadStatus() const;
    std::string GetCurrentFile() const;
    std::string GetCurrentBlock() const;
    int GetTotalFiles() const { return total_files_to_scan; }
    int GetFilesScanned() const { return files_scanned; }
    int GetBlocksFound() const { return blocks_found; }
    
    // Cancel loading
    void CancelLoading();
#endif
    
    // UI
    void Draw(FlowCanvas* canvas);
    void DrawUpdateProgress();  // Draw progress popup for library updates
    
    // Check if update popup should be shown
    bool ShouldShowUpdatePopup() const { return show_update_popup; }
    
    // Search/filter
    void SetSearchFilter(const std::string& filter);
    
private:
    // Structure to hold library information
    struct LibraryInfo {
        std::string name;
        std::string path;
        std::vector<std::shared_ptr<BlockSpec>> blocks;
        std::map<std::string, std::vector<std::shared_ptr<BlockSpec>>> blocks_by_category;
        bool expanded = true;  // For UI tree view
    };
    
    // Organized by library, then by category
    std::map<std::string, LibraryInfo> libraries;
    
    // All blocks for searching
    std::vector<std::shared_ptr<BlockSpec>> all_blocks;
    
#ifdef HAS_LIBCLANG
    // Parsed metadata from headers
    std::vector<BlockMetadata> parsed_blocks;
    BlockLibraryScanner scanner;
    
    // Cache management
    std::unique_ptr<BlockCache> cache;
#endif
    
    // UI state
    std::string search_filter;
    std::string selected_category;
    bool show_parsed_blocks = false;
    
#ifdef HAS_LIBCLANG
    // Loading progress state
    std::atomic<bool> is_loading{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<float> load_progress{0.0f};
    
    // Thread-safe status strings
    std::mutex status_mutex;
    std::string load_status;
    std::string current_file;
    std::string current_block_name;
    
    // Files to scan
    std::vector<std::string> files_to_scan;
    std::atomic<size_t> current_file_index{0};
    std::atomic<int> total_files_to_scan{0};
    std::atomic<int> files_scanned{0};
    std::atomic<int> blocks_found{0};
    
    // Temporary storage during loading
    std::mutex parsed_blocks_mutex;
    std::vector<BlockMetadata> temp_parsed_blocks;
    
    // Flags to defer initial scan
    bool need_initial_scan = false;
    bool scan_complete = false;
    bool loaded_from_cache = false;
    
    // Current loading context
    std::string current_library_name;
    std::string current_library_path;
    
    // Background parsing thread
    std::unique_ptr<std::thread> parse_thread;
    std::atomic<bool> parsing_active{false};
    
    // Thread-safe queue for parsed results
    std::mutex result_queue_mutex;
    std::vector<BlockMetadata> result_queue;
    
    // Flag to show update progress popup
    bool show_update_popup = false;
    std::string updating_library_name;
#endif
    
    // Create test blocks for development
    std::shared_ptr<BlockSpec> CreateCWSourceBlock();
    std::shared_ptr<BlockSpec> CreateAddBlock();
    std::shared_ptr<BlockSpec> CreateThrottleBlock();
    std::shared_ptr<BlockSpec> CreateFileSinkBlock();
};

} // namespace clerflow