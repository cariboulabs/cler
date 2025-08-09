/******************************************************************************************
*                                                                                         *
*    BlockLibrary - Implementation of block management and UI browser                    *
*                                                                                         *
******************************************************************************************/

#include "block_library.hpp"
#include "flow_canvas.hpp"
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <filesystem>

namespace clerflow {

BlockLibrary::BlockLibrary()
{
    // Initialize with empty test library (will be populated with test blocks)
    LibraryInfo test_lib;
    test_lib.name = "Test Blocks";
    test_lib.path = "";
    test_lib.blocks_by_category["Sources"] = {};
    test_lib.blocks_by_category["Sinks"] = {};
    test_lib.blocks_by_category["Processing"] = {};
    test_lib.blocks_by_category["Math"] = {};
    test_lib.blocks_by_category["Utility"] = {};
    libraries["Test Blocks"] = test_lib;
    
#ifdef HAS_LIBCLANG
    cache = std::make_unique<BlockCache>();
#endif
}

BlockLibrary::~BlockLibrary()
{
#ifdef HAS_LIBCLANG
    // Stop the parsing thread if it's running
    if (parse_thread && parse_thread->joinable()) {
        cancel_requested = true;
        parse_thread->join();
    }
#endif
}

void BlockLibrary::AddBlock(std::shared_ptr<BlockSpec> spec)
{
    all_blocks.push_back(spec);
    
    // Determine library name (use default if not specified)
    std::string lib_name = spec->library_name.empty() ? "Test Blocks" : spec->library_name;
    
    // Ensure library exists
    if (libraries.find(lib_name) == libraries.end()) {
        LibraryInfo new_lib;
        new_lib.name = lib_name;
        new_lib.path = spec->library_path;
        libraries[lib_name] = new_lib;
    }
    
    // Add to library's blocks
    libraries[lib_name].blocks.push_back(spec);
    
    // Add to library's category
    auto& lib_categories = libraries[lib_name].blocks_by_category;
    if (lib_categories.find(spec->category) == lib_categories.end()) {
        lib_categories[spec->category] = {};
    }
    lib_categories[spec->category].push_back(spec);
}

void BlockLibrary::ClearBlocks()
{
    all_blocks.clear();
    libraries.clear();
    
    // Re-initialize empty test library
    LibraryInfo test_lib;
    test_lib.name = "Test Blocks";
    test_lib.path = "";
    test_lib.blocks_by_category["Sources"] = {};
    test_lib.blocks_by_category["Sinks"] = {};
    test_lib.blocks_by_category["Processing"] = {};
    test_lib.blocks_by_category["Math"] = {};
    test_lib.blocks_by_category["Utility"] = {};
    libraries["Test Blocks"] = test_lib;
}

void BlockLibrary::ClearLibrary(const std::string& library_name)
{
    if (libraries.find(library_name) != libraries.end()) {
        // Remove blocks from all_blocks
        auto& lib_blocks = libraries[library_name].blocks;
        for (const auto& block : lib_blocks) {
            all_blocks.erase(
                std::remove(all_blocks.begin(), all_blocks.end(), block),
                all_blocks.end()
            );
        }
        
        // Clear the library
        libraries[library_name].blocks.clear();
        libraries[library_name].blocks_by_category.clear();
    }
}

void BlockLibrary::ImportFromHeader(const std::string& header_path)
{
    // TODO: Implement header parsing
    (void)header_path;
}

void BlockLibrary::ImportFromDirectory(const std::string& dir_path)
{
    // TODO: Implement directory scanning
    (void)dir_path;
}

void BlockLibrary::SetSearchFilter(const std::string& filter)
{
    search_filter = filter;
}

#ifdef HAS_LIBCLANG
std::string BlockLibrary::GetLoadStatus() const 
{
    std::lock_guard<std::mutex> lock(const_cast<BlockLibrary*>(this)->status_mutex);
    return load_status;
}

std::string BlockLibrary::GetCurrentFile() const 
{
    std::lock_guard<std::mutex> lock(const_cast<BlockLibrary*>(this)->status_mutex);
    return current_file;
}

std::string BlockLibrary::GetCurrentBlock() const 
{
    std::lock_guard<std::mutex> lock(const_cast<BlockLibrary*>(this)->status_mutex);
    return current_block_name;
}

void BlockLibrary::StartLoadingDesktopBlocks()
{
    // Set the library context
    current_library_name = "desktop_blocks";
    current_library_path = "/home/alon/repos/cler/desktop_blocks";
    
    LoadLibrary(current_library_path, current_library_name);
}

void BlockLibrary::LoadLibrary(const std::string& path, const std::string& library_name)
{
    // Stop any existing parsing thread
    if (parse_thread && parse_thread->joinable()) {
        cancel_requested = true;
        parse_thread->join();
    }
    
    // Set library context
    current_library_name = library_name;
    current_library_path = path;
    
    // Reset state
    is_loading = true;
    cancel_requested = false;
    load_progress = 0.0f;
    files_scanned = 0;
    blocks_found = 0;
    current_file_index = 0;
    files_to_scan.clear();
    temp_parsed_blocks.clear();
    loaded_from_cache = false;
    
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        current_block_name.clear();
        load_status = "Initializing...";
    }
    
    {
        std::lock_guard<std::mutex> lock(result_queue_mutex);
        result_queue.clear();
    }
    
    // Check if we can load from cache (only for desktop_blocks for now)
    if (library_name == "desktop_blocks" && cache && cache->isCacheValid(path)) {
        // Cache is valid! Load from it instead of scanning
        std::cout << "Loading blocks from cache..." << std::endl;
        
        auto cached_blocks = cache->loadFromCache();
        if (!cached_blocks.empty()) {
            // Successfully loaded from cache
            temp_parsed_blocks = std::move(cached_blocks);
            loaded_from_cache = true;
            need_initial_scan = false;
            scan_complete = true;
            
            // Set progress to almost complete
            load_progress = 0.99f;
            blocks_found = temp_parsed_blocks.size();
            
            {
                std::lock_guard<std::mutex> lock(status_mutex);
                load_status = "Loaded " + std::to_string(blocks_found.load()) + " blocks from cache";
            }
            
            // We'll finalize in ProcessNextBlocks
            return;
        }
    }
    
    // No valid cache, do normal scan
    need_initial_scan = true;  // Flag to do the scan on first ProcessNextBlocks
    scan_complete = false;  // Reset scan flag
    parsing_active = false;
    
    total_files_to_scan = 0;
}

void BlockLibrary::ProcessNextBlocks(int blocks_per_frame)
{
    (void)blocks_per_frame; // Not used with threading approach
    
    if (!is_loading) {
        return;
    }
    
    // If we loaded from cache, skip straight to finalization
    if (loaded_from_cache) {
        // Jump straight to the finalization code
        // This will happen on the first call after loading from cache
    }
    // Do the initial file scan on first call (after popup is shown)
    else if (need_initial_scan) {
        need_initial_scan = false;
        // Just show initial status, don't scan yet
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            load_status = "Preparing to scan...";
        }
        return;  // Return to show 0% progress first
    }
    
    // Do the actual scanning on the second call
    if (!scan_complete && files_to_scan.empty()) {
        scan_complete = true;
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            load_status = "Scanning for block files...";
        }
        
        // Collect all .hpp files from the library path
        namespace fs = std::filesystem;
        
        try {
            // Check if it's a single file or directory
            if (fs::is_regular_file(current_library_path) && fs::path(current_library_path).extension() == ".hpp") {
                // Single header file
                files_to_scan.push_back(current_library_path);
            } else if (fs::is_directory(current_library_path)) {
                // Directory - scan recursively
                for (const auto& entry : fs::recursive_directory_iterator(current_library_path)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".hpp") {
                        files_to_scan.push_back(entry.path().string());
                    }
                }
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(status_mutex);
            load_status = std::string("Error: ") + e.what();
            is_loading = false;
            return;
        }
        
        total_files_to_scan = files_to_scan.size();
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            load_status = "Found " + std::to_string(total_files_to_scan.load()) + " header files";
        }
        
        // Start the background parsing thread
        if (!files_to_scan.empty() && !parsing_active) {
            parsing_active = true;
            parse_thread = std::make_unique<std::thread>([this]() {
                BlockParser parser;
                
                while (current_file_index < files_to_scan.size() && !cancel_requested) {
                    size_t idx = current_file_index++;
                    if (idx >= files_to_scan.size()) break;
                    
                    const std::string& file_path = files_to_scan[idx];
                    
                    // Update current file being processed
                    {
                        namespace fs = std::filesystem;
                        std::lock_guard<std::mutex> lock(status_mutex);
                        current_file = fs::path(file_path).filename().string();
                    }
                    
                    // Quick check if it's a block header
                    if (parser.isBlockHeader(file_path)) {
                        // Parse the header (this is the slow operation)
                        BlockMetadata metadata = parser.parseHeader(file_path);
                        if (metadata.is_valid) {
                            // Extract category from path
                            namespace fs = std::filesystem;
                            fs::path file(file_path);
                            fs::path root(current_library_path);
                            fs::path relative = fs::relative(file.parent_path(), root);
                            
                            if (relative == ".") {
                                metadata.category = "Uncategorized";
                            } else {
                                // Convert path to category string
                                std::string category;
                                for (const auto& part : relative) {
                                    if (!category.empty()) {
                                        category += "/";
                                    }
                                    std::string part_str = part.string();
                                    if (!part_str.empty()) {
                                        // Capitalize first letter
                                        part_str[0] = std::toupper(part_str[0]);
                                    }
                                    category += part_str;
                                }
                                metadata.category = category;
                            }
                            
                            metadata.library_name = current_library_name;
                            metadata.library_path = current_library_path;
                            
                            // Add to result queue
                            {
                                std::lock_guard<std::mutex> lock(result_queue_mutex);
                                result_queue.push_back(metadata);
                            }
                            
                            // Update current block name
                            {
                                std::lock_guard<std::mutex> lock(status_mutex);
                                current_block_name = metadata.class_name;
                            }
                            
                            blocks_found++;
                        }
                    }
                    
                    files_scanned++;
                    load_progress = static_cast<float>(files_scanned.load()) / static_cast<float>(total_files_to_scan.load());
                }
                
                parsing_active = false;
            });
        }
        
        return;
    }
    
    // Process results from the background thread
    {
        std::lock_guard<std::mutex> lock(result_queue_mutex);
        if (!result_queue.empty()) {
            // Move results to temp storage
            temp_parsed_blocks.insert(temp_parsed_blocks.end(), 
                                     result_queue.begin(), 
                                     result_queue.end());
            result_queue.clear();
        }
    }
    
    // Update status
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        if (!current_file.empty()) {
            load_status = "Processing: " + current_file;
        }
    }
    
    // Check if we're done
    if (!parsing_active && current_file_index >= files_to_scan.size()) {
        // Wait for the thread to finish
        if (parse_thread && parse_thread->joinable()) {
            parse_thread->join();
        }
        
        // Process any remaining results
        {
            std::lock_guard<std::mutex> lock(result_queue_mutex);
            if (!result_queue.empty()) {
                temp_parsed_blocks.insert(temp_parsed_blocks.end(), 
                                         result_queue.begin(), 
                                         result_queue.end());
                result_queue.clear();
            }
        }
        
        // Convert all parsed blocks to BlockSpec
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            load_status = "Finalizing...";
        }
        parsed_blocks = std::move(temp_parsed_blocks);
        
        for (const auto& metadata : parsed_blocks) {
            auto spec = std::make_shared<BlockSpec>();
            spec->class_name = metadata.class_name;
            spec->display_name = metadata.class_name;
            spec->category = metadata.category.empty() ? "Uncategorized" : metadata.category;
            spec->header_file = metadata.header_path;
            spec->library_name = metadata.library_name.empty() ? current_library_name : metadata.library_name;
            spec->library_path = metadata.library_path.empty() ? current_library_path : metadata.library_path;
            
            // Convert template params
            for (const auto& tparam : metadata.template_params) {
                ParamSpec param;
                param.name = tparam.name;
                param.display_name = tparam.name;
                param.type = ParamType::String;
                param.default_value = tparam.default_value;
                spec->template_params.push_back(param);
            }
            
            // Convert constructor params
            for (const auto& cparam : metadata.constructor_params) {
                ParamSpec param;
                param.name = cparam.name;
                param.display_name = cparam.name;
                // Detect type from string
                if (cparam.type.find("float") != std::string::npos) {
                    param.type = ParamType::Float;
                } else if (cparam.type.find("int") != std::string::npos) {
                    param.type = ParamType::Int;
                } else if (cparam.type.find("bool") != std::string::npos) {
                    param.type = ParamType::Bool;
                } else if (cparam.type.find("string") != std::string::npos || 
                          cparam.type.find("char") != std::string::npos) {
                    param.type = ParamType::String;
                } else {
                    param.type = ParamType::String;
                }
                param.default_value = cparam.default_value;
                spec->constructor_params.push_back(param);
            }
            
            // Convert input ports
            for (const auto& channel : metadata.input_channels) {
                PortSpec port;
                port.name = channel.name;
                port.display_name = channel.name;
                port.cpp_type = channel.type;
                // Detect data type from string
                if (channel.type.find("float") != std::string::npos) {
                    port.data_type = DataType::Float;
                } else if (channel.type.find("double") != std::string::npos) {
                    port.data_type = DataType::Double;
                } else if (channel.type.find("complex") != std::string::npos) {
                    if (channel.type.find("float") != std::string::npos) {
                        port.data_type = DataType::ComplexFloat;
                    } else {
                        port.data_type = DataType::ComplexDouble;
                    }
                } else if (channel.type.find("int") != std::string::npos) {
                    port.data_type = DataType::Int;
                } else {
                    port.data_type = DataType::Custom;
                }
                spec->input_ports.push_back(port);
            }
            
            // Convert output ports
            for (const auto& channel : metadata.output_channels) {
                PortSpec port;
                port.name = channel.name;
                port.display_name = channel.name;
                port.cpp_type = channel.type;
                // Detect data type from string
                if (channel.type.find("float") != std::string::npos) {
                    port.data_type = DataType::Float;
                } else if (channel.type.find("double") != std::string::npos) {
                    port.data_type = DataType::Double;
                } else if (channel.type.find("complex") != std::string::npos) {
                    if (channel.type.find("float") != std::string::npos) {
                        port.data_type = DataType::ComplexFloat;
                    } else {
                        port.data_type = DataType::ComplexDouble;
                    }
                } else if (channel.type.find("int") != std::string::npos) {
                    port.data_type = DataType::Int;
                } else {
                    port.data_type = DataType::Custom;
                }
                spec->output_ports.push_back(port);
            }
            
            // Detect if source or sink
            spec->is_source = spec->input_ports.empty() && !spec->output_ports.empty();
            spec->is_sink = !spec->input_ports.empty() && spec->output_ports.empty();
            
            AddBlock(spec);
        }
        
        is_loading = false;
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            load_status = "Import complete! Found " + std::to_string(parsed_blocks.size()) + " blocks";
            current_block_name.clear();
        }
        
        // Save to cache if we did a fresh scan (not loaded from cache)
        if (!loaded_from_cache && cache && !parsed_blocks.empty()) {
            std::string desktop_blocks_path = "/home/alon/repos/cler/desktop_blocks";
            std::cout << "Saving cache with " << parsed_blocks.size() << " blocks..." << std::endl;
            cache->saveToCache(parsed_blocks, desktop_blocks_path);
            std::cout << "Cache saved to: " << cache->getCachePath() << std::endl;
        }
    }
}

void BlockLibrary::CancelLoading()
{
    cancel_requested = true;
    
    // Wait for the parsing thread to stop
    if (parse_thread && parse_thread->joinable()) {
        parse_thread->join();
    }
    
    // Clear loading state immediately for responsive UI
    is_loading = false;
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        load_status = "Import cancelled";
        current_block_name.clear();
        current_file.clear();
    }
}

void BlockLibrary::UpdateBlock(std::shared_ptr<BlockSpec> block)
{
    if (!block || block->header_file.empty()) return;
    
    // Parse the header file again
    BlockParser parser;
    BlockMetadata metadata = parser.parseHeader(block->header_file);
    
    if (metadata.is_valid) {
        // Update the block spec with new metadata
        block->class_name = metadata.class_name;
        block->display_name = metadata.class_name;
        
        // Clear and rebuild parameters
        block->template_params.clear();
        for (const auto& tparam : metadata.template_params) {
            ParamSpec param;
            param.name = tparam.name;
            param.display_name = tparam.name;
            param.type = ParamType::String;
            param.default_value = tparam.default_value;
            block->template_params.push_back(param);
        }
        
        block->constructor_params.clear();
        for (const auto& cparam : metadata.constructor_params) {
            ParamSpec param;
            param.name = cparam.name;
            param.display_name = cparam.name;
            // Detect type from string
            if (cparam.type.find("float") != std::string::npos) {
                param.type = ParamType::Float;
            } else if (cparam.type.find("int") != std::string::npos) {
                param.type = ParamType::Int;
            } else if (cparam.type.find("bool") != std::string::npos) {
                param.type = ParamType::Bool;
            } else if (cparam.type.find("string") != std::string::npos || 
                      cparam.type.find("char") != std::string::npos) {
                param.type = ParamType::String;
            } else {
                param.type = ParamType::String;
            }
            param.default_value = cparam.default_value;
            block->constructor_params.push_back(param);
        }
        
        // Update ports...
        block->input_ports.clear();
        for (const auto& channel : metadata.input_channels) {
            PortSpec port;
            port.name = channel.name;
            port.display_name = channel.name;
            port.cpp_type = channel.type;
            // Detect data type from string
            if (channel.type.find("float") != std::string::npos) {
                port.data_type = DataType::Float;
            } else if (channel.type.find("double") != std::string::npos) {
                port.data_type = DataType::Double;
            } else if (channel.type.find("complex") != std::string::npos) {
                if (channel.type.find("float") != std::string::npos) {
                    port.data_type = DataType::ComplexFloat;
                } else {
                    port.data_type = DataType::ComplexDouble;
                }
            } else if (channel.type.find("int") != std::string::npos) {
                port.data_type = DataType::Int;
            } else {
                port.data_type = DataType::Custom;
            }
            block->input_ports.push_back(port);
        }
        
        block->output_ports.clear();
        for (const auto& channel : metadata.output_channels) {
            PortSpec port;
            port.name = channel.name;
            port.display_name = channel.name;
            port.cpp_type = channel.type;
            // Detect data type from string
            if (channel.type.find("float") != std::string::npos) {
                port.data_type = DataType::Float;
            } else if (channel.type.find("double") != std::string::npos) {
                port.data_type = DataType::Double;
            } else if (channel.type.find("complex") != std::string::npos) {
                if (channel.type.find("float") != std::string::npos) {
                    port.data_type = DataType::ComplexFloat;
                } else {
                    port.data_type = DataType::ComplexDouble;
                }
            } else if (channel.type.find("int") != std::string::npos) {
                port.data_type = DataType::Int;
            } else {
                port.data_type = DataType::Custom;
            }
            block->output_ports.push_back(port);
        }
        
        // Update source/sink flags
        block->is_source = block->input_ports.empty() && !block->output_ports.empty();
        block->is_sink = !block->input_ports.empty() && block->output_ports.empty();
    }
}

void BlockLibrary::UpdateLibrary(const std::string& library_name)
{
    if (libraries.find(library_name) == libraries.end()) return;
    
    // Get all blocks in this library
    auto& lib_blocks = libraries[library_name].blocks;
    
    // Update each block
    for (auto& block : lib_blocks) {
        UpdateBlock(block);
    }
    
    // If it's desktop_blocks, update the cache
    if (library_name == "desktop_blocks" && cache) {
        // Convert blocks to metadata for cache
        std::vector<BlockMetadata> metadata_list;
        for (const auto& block : lib_blocks) {
            BlockMetadata metadata;
            metadata.class_name = block->class_name;
            metadata.header_path = block->header_file;
            metadata.category = block->category;
            metadata.library_name = block->library_name;
            metadata.library_path = block->library_path;
            metadata.is_valid = true;
            
            // Convert params back to metadata format...
            // (simplified for now)
            
            metadata_list.push_back(metadata);
        }
        
        cache->saveToCache(metadata_list, libraries[library_name].path);
    }
}

#endif

void BlockLibrary::LoadTestBlocks()
{
    // Create some test blocks for Phase 1
    
    // Sine Source
    auto sine_source = std::make_shared<BlockSpec>();
    sine_source->class_name = "SineSource";
    sine_source->display_name = "Sine Source";
    sine_source->category = "Sources";
    sine_source->header_file = "cler/blocks/sources.hpp";
    sine_source->is_source = true;
    
    ParamSpec freq_param;
    freq_param.name = "frequency";
    freq_param.display_name = "Frequency";
    freq_param.type = ParamType::Float;
    freq_param.default_value = "1000.0";
    freq_param.tooltip = "Frequency in Hz";
    freq_param.min = 0.1;
    freq_param.max = 20000.0;
    sine_source->constructor_params.push_back(freq_param);
    
    ParamSpec sr_param;
    sr_param.name = "sample_rate";
    sr_param.display_name = "Sample Rate";
    sr_param.type = ParamType::Float;
    sr_param.default_value = "48000.0";
    sr_param.tooltip = "Sample rate";
    sine_source->constructor_params.push_back(sr_param);
    
    PortSpec sine_out;
    sine_out.name = "out";
    sine_out.display_name = "Output";
    sine_out.data_type = DataType::Float;
    sine_out.cpp_type = "float";
    sine_source->output_ports.push_back(sine_out);
    
    AddBlock(sine_source);
    
    // Noise Source
    auto noise_source = std::make_shared<BlockSpec>();
    noise_source->class_name = "NoiseSource";
    noise_source->display_name = "Noise Source";
    noise_source->category = "Sources";
    noise_source->header_file = "cler/blocks/sources.hpp";
    noise_source->is_source = true;
    
    ParamSpec amp_param;
    amp_param.name = "amplitude";
    amp_param.display_name = "Amplitude";
    amp_param.type = ParamType::Float;
    amp_param.default_value = "0.5";
    amp_param.tooltip = "Signal amplitude";
    amp_param.min = 0.0;
    amp_param.max = 1.0;
    noise_source->constructor_params.push_back(amp_param);
    
    PortSpec noise_out;
    noise_out.name = "out";
    noise_out.display_name = "Output";
    noise_out.data_type = DataType::Float;
    noise_out.cpp_type = "float";
    noise_source->output_ports.push_back(noise_out);
    
    AddBlock(noise_source);
    
    // File Sink
    auto file_sink = std::make_shared<BlockSpec>();
    file_sink->class_name = "FileSink";
    file_sink->display_name = "File Sink";
    file_sink->category = "Sinks";
    file_sink->header_file = "cler/blocks/sinks.hpp";
    file_sink->is_sink = true;
    
    ParamSpec file_param;
    file_param.name = "filename";
    file_param.display_name = "Filename";
    file_param.type = ParamType::String;
    file_param.default_value = "output.dat";
    file_param.tooltip = "Output filename";
    file_sink->constructor_params.push_back(file_param);
    
    PortSpec file_in;
    file_in.name = "in";
    file_in.display_name = "Input";
    file_in.data_type = DataType::Float;
    file_in.cpp_type = "float";
    file_sink->input_ports.push_back(file_in);
    
    AddBlock(file_sink);
    
    // Multiply Block
    auto multiply = std::make_shared<BlockSpec>();
    multiply->class_name = "Multiply";
    multiply->display_name = "Multiply";
    multiply->category = "Math";
    multiply->header_file = "cler/blocks/math.hpp";
    
    PortSpec mul_in1;
    mul_in1.name = "in1";
    mul_in1.display_name = "Input 1";
    mul_in1.data_type = DataType::Float;
    mul_in1.cpp_type = "float";
    multiply->input_ports.push_back(mul_in1);
    
    PortSpec mul_in2;
    mul_in2.name = "in2";
    mul_in2.display_name = "Input 2";
    mul_in2.data_type = DataType::Float;
    mul_in2.cpp_type = "float";
    multiply->input_ports.push_back(mul_in2);
    
    PortSpec mul_out;
    mul_out.name = "out";
    mul_out.display_name = "Output";
    mul_out.data_type = DataType::Float;
    mul_out.cpp_type = "float";
    multiply->output_ports.push_back(mul_out);
    
    AddBlock(multiply);
}

void BlockLibrary::Draw(FlowCanvas* canvas)
{
    if (!canvas) return;
    
    ImGui::BeginChild("BlockList", ImVec2(0, 0), true);
    
#ifdef HAS_LIBCLANG
    // Load Library button
    if (ImGui::Button("Load Library")) {
        // For now, show a simple input dialog
        // In the future, this could open a file dialog
        ImGui::OpenPopup("Load Library Dialog");
    }
    ImGui::Separator();
#endif
    
    // Search filter
    static char searchBuffer[256] = {0};
    ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
    ImGui::Separator();
    
    std::string search(searchBuffer);
    std::transform(search.begin(), search.end(), search.begin(), ::tolower);
    
    // Draw libraries with their categories
    for (auto& [lib_name, lib_info] : libraries) {
        // Skip empty libraries
        if (lib_info.blocks.empty()) continue;
        
        // Library header with right-click menu
        ImGui::PushID(lib_name.c_str());
        bool lib_open = ImGui::TreeNodeEx(lib_name.c_str(), 
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s (%zu blocks)", lib_name.c_str(), lib_info.blocks.size());
        
        // Right-click context menu for library
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("LibraryHeaderContextMenu");
        }
        
        if (ImGui::BeginPopup("LibraryHeaderContextMenu")) {
#ifdef HAS_LIBCLANG
            if (ImGui::MenuItem("Update Library")) {
                UpdateLibrary(lib_name);
            }
            ImGui::Separator();
#endif
            if (ImGui::MenuItem("Remove Library")) {
                ClearLibrary(lib_name);
            }
            ImGui::EndPopup();
        }
        
        if (lib_open) {
            // Draw categories within this library
            for (const auto& [category, blocks] : lib_info.blocks_by_category) {
                if (blocks.empty()) continue;
                
                ImGui::PushID(category.c_str());
                if (ImGui::TreeNode(category.c_str())) {
                    for (const auto& block : blocks) {
                        // Filter by search
                        if (!search.empty()) {
                            std::string name = block->display_name;
                            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                            if (name.find(search) == std::string::npos) {
                                continue;
                            }
                        }
                        
                        // Draw block entry
                        ImGui::PushID(block.get());
                        
                        bool selected = false;
                        if (ImGui::Selectable(block->display_name.c_str(), &selected, ImGuiSelectableFlags_AllowItemOverlap)) {
                            // Double-click to add
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                // Add at center of canvas view
                                ImVec2 canvas_center = ImGui::GetWindowPos();
                                canvas_center.x += ImGui::GetWindowWidth() / 2;
                                canvas_center.y += ImGui::GetWindowHeight() / 2;
                                canvas->AddNode(block, canvas_center);
                            }
                        }
                        
                        // Handle right-click context menu for block
                        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                            ImGui::OpenPopup("LibraryBlockContextMenu");
                        }
                        
                        if (ImGui::BeginPopup("LibraryBlockContextMenu")) {
#ifdef HAS_LIBCLANG
                            if (ImGui::MenuItem("Update Block")) {
                                UpdateBlock(block);
                            }
#endif
                            ImGui::EndPopup();
                        }
                        
                        // Drag to add
                        if (ImGui::BeginDragDropSource()) {
                            ImGui::SetDragDropPayload("BLOCK_SPEC", &block, sizeof(block));
                            ImGui::Text("Add %s", block->display_name.c_str());
                            ImGui::EndDragDropSource();
                        }
                        
                        // Tooltip with details
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", block->class_name.c_str());
                            if (!block->header_file.empty()) {
                                ImGui::Text("Header: %s", block->header_file.c_str());
                            }
                            if (!block->template_params.empty()) {
                                ImGui::Text("Template: ");
                                for (const auto& param : block->template_params) {
                                    ImGui::Text("  %s = %s", 
                                               param.name.c_str(), param.default_value.c_str());
                                }
                            }
                            if (!block->constructor_params.empty()) {
                                ImGui::Text("Parameters:");
                                for (const auto& param : block->constructor_params) {
                                    ImGui::Text("  %s: %s", param.name.c_str(), param.display_name.c_str());
                                }
                            }
                            ImGui::EndTooltip();
                        }
                        
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    
    // Load Library Dialog
#ifdef HAS_LIBCLANG
    static char lib_path[512] = "";
    static char lib_name[128] = "";
    
    if (ImGui::BeginPopupModal("Load Library Dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Load a library from a directory or .hpp file");
        ImGui::Separator();
        
        ImGui::InputText("Path", lib_path, sizeof(lib_path));
        ImGui::InputText("Library Name", lib_name, sizeof(lib_name));
        
        ImGui::Separator();
        
        if (ImGui::Button("Load", ImVec2(120, 0))) {
            if (strlen(lib_path) > 0 && strlen(lib_name) > 0) {
                LoadLibrary(lib_path, lib_name);
                // Clear inputs
                lib_path[0] = '\0';
                lib_name[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
#endif
    
    ImGui::EndChild();
}

} // namespace clerflow