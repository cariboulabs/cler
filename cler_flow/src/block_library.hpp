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
    bool ShouldShowImportPopup() const { return request_import_popup; }
    void ClearImportPopupRequest() { request_import_popup = false; }
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
    std::atomic<bool> is_loading{false};
    std::atomic<bool> cancel_requested{false};
    bool request_import_popup = false;
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
    
    // Background parsing thread
    std::unique_ptr<std::thread> parse_thread;
    std::atomic<bool> parsing_active{false};
    
    // Thread-safe queue for parsed results
    std::mutex result_queue_mutex;
    std::vector<BlockMetadata> result_queue;
#endif
    
    // Create test blocks for development
    std::shared_ptr<BlockSpec> CreateCWSourceBlock();
    std::shared_ptr<BlockSpec> CreateAddBlock();
    std::shared_ptr<BlockSpec> CreateThrottleBlock();
    std::shared_ptr<BlockSpec> CreateFileSinkBlock();
};

} // namespace clerflow