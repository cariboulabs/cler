/******************************************************************************************
*                                                                                         *
*    BlockCache - Implementation of cache management for block library                   *
*                                                                                         *
******************************************************************************************/

#include "block_cache.hpp"
#include "../include/external/toml.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace clerflow {

BlockCache::BlockCache()
{
    // Determine cache directory
    // First try user's home directory
    const char* home = std::getenv("HOME");
    if (home) {
        cache_path = std::filesystem::path(home) / ".cache" / "cler-flow";
    } else {
        // Fallback to temp directory
        cache_path = std::filesystem::temp_directory_path() / "cler-flow-cache";
    }
    
    // Create cache directory if it doesn't exist
    std::filesystem::create_directories(cache_path);
    
    // Set the cache file name
    cache_path /= "block_library_cache.toml";
}

bool BlockCache::hasCacheFile() const
{
    return std::filesystem::exists(cache_path);
}

bool BlockCache::isCacheValid(const std::string& source_path) const
{
    if (!hasCacheFile()) {
        return false;
    }
    
    try {
        auto config = toml::parse_file(cache_path.string());
        
        // Check if source path matches
        auto cached_source = config["source_path"].value<std::string>();
        if (!cached_source || *cached_source != source_path) {
            return false;
        }
        
        // Get cache timestamp
        auto cache_timestamp = config["timestamp"].value<std::string>();
        if (!cache_timestamp) {
            return false;
        }
        
        auto cache_time = stringToTime(*cache_timestamp);
        
        // Check if any header files are newer than cache
        namespace fs = std::filesystem;
        for (const auto& entry : fs::recursive_directory_iterator(source_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".hpp") {
                if (fs::last_write_time(entry.path()) > cache_time) {
                    return false;  // Found a newer file
                }
            }
        }
        
        return true;  // Cache is still valid
        
    } catch (const std::exception& e) {
        std::cerr << "Cache validation error: " << e.what() << std::endl;
        return false;
    }
}

std::vector<BlockMetadata> BlockCache::loadFromCache()
{
    std::vector<BlockMetadata> blocks;
    
    try {
        auto config = toml::parse_file(cache_path.string());
        
        auto blocks_array = config["blocks"].as_array();
        if (!blocks_array) {
            return blocks;
        }
        
        for (const auto& block_node : *blocks_array) {
            BlockMetadata metadata;
            
            // Basic fields - need to cast to table first
            auto block_tbl = block_node.as_table();
            if (!block_tbl) continue;
            
            auto class_name = (*block_tbl)["class_name"].value<std::string>();
            auto header_path = (*block_tbl)["header_path"].value<std::string>();
            auto category = (*block_tbl)["category"].value<std::string>();
            auto library_name = (*block_tbl)["library_name"].value<std::string>();
            
            if (class_name && header_path) {
                metadata.class_name = *class_name;
                metadata.header_path = *header_path;
                metadata.category = category.value_or("");
                metadata.library_name = library_name.value_or("Desktop Blocks");
                metadata.is_valid = true;
                
                // Template parameters
                if (auto tparams = (*block_tbl)["template_params"].as_array()) {
                    for (const auto& tparam_node : *tparams) {
                        auto tparam = tparam_node.as_table();
                        if (tparam) {
                            BlockMetadata::TemplateParam param;
                            param.name = (*tparam)["name"].value_or("");
                            param.default_value = (*tparam)["default_value"].value_or("");
                            metadata.template_params.push_back(param);
                        }
                    }
                }
                
                // Constructor parameters
                if (auto cparams = (*block_tbl)["constructor_params"].as_array()) {
                    for (const auto& cparam_node : *cparams) {
                        auto cparam = cparam_node.as_table();
                        if (cparam) {
                            BlockMetadata::ConstructorParam param;
                            param.name = (*cparam)["name"].value_or("");
                            param.type = (*cparam)["type"].value_or("");
                            param.default_value = (*cparam)["default_value"].value_or("");
                            metadata.constructor_params.push_back(param);
                        }
                    }
                }
                
                // Input channels
                if (auto inputs = (*block_tbl)["input_channels"].as_array()) {
                    for (const auto& input_node : *inputs) {
                        auto input = input_node.as_table();
                        if (input) {
                            BlockMetadata::ChannelInfo channel;
                            channel.name = (*input)["name"].value_or("");
                            channel.type = (*input)["type"].value_or("");
                            metadata.input_channels.push_back(channel);
                        }
                    }
                }
                
                // Output channels
                if (auto outputs = (*block_tbl)["output_channels"].as_array()) {
                    for (const auto& output_node : *outputs) {
                        auto output = output_node.as_table();
                        if (output) {
                            BlockMetadata::ChannelInfo channel;
                            channel.name = (*output)["name"].value_or("");
                            channel.type = (*output)["type"].value_or("");
                            metadata.output_channels.push_back(channel);
                        }
                    }
                }
                
                blocks.push_back(metadata);
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading cache: " << e.what() << std::endl;
    }
    
    return blocks;
}

void BlockCache::saveToCache(const std::vector<BlockMetadata>& blocks, const std::string& source_path)
{
    try {
        toml::table tbl;
        
        // Metadata
        tbl.insert("version", "1.0");
        tbl.insert("timestamp", timeToString(std::filesystem::file_time_type::clock::now()));
        tbl.insert("source_path", source_path);
        
        // Blocks array
        toml::array blocks_array;
        
        for (const auto& block : blocks) {
            if (!block.is_valid) continue;
            
            toml::table block_tbl;
            block_tbl.insert("class_name", block.class_name);
            block_tbl.insert("header_path", block.header_path);
            block_tbl.insert("category", block.category);
            block_tbl.insert("library_name", block.library_name);
            
            // Get file modification time
            if (std::filesystem::exists(block.header_path)) {
                auto mod_time = std::filesystem::last_write_time(block.header_path);
                block_tbl.insert("last_modified", timeToString(mod_time));
            }
            
            // Template parameters
            if (!block.template_params.empty()) {
                toml::array tparams;
                for (const auto& param : block.template_params) {
                    toml::table param_tbl;
                    param_tbl.insert("name", param.name);
                    param_tbl.insert("default_value", param.default_value);
                    tparams.push_back(param_tbl);
                }
                block_tbl.insert("template_params", tparams);
            }
            
            // Constructor parameters
            if (!block.constructor_params.empty()) {
                toml::array cparams;
                for (const auto& param : block.constructor_params) {
                    toml::table param_tbl;
                    param_tbl.insert("name", param.name);
                    param_tbl.insert("type", param.type);
                    param_tbl.insert("default_value", param.default_value);
                    cparams.push_back(param_tbl);
                }
                block_tbl.insert("constructor_params", cparams);
            }
            
            // Input channels
            if (!block.input_channels.empty()) {
                toml::array inputs;
                for (const auto& channel : block.input_channels) {
                    toml::table channel_tbl;
                    channel_tbl.insert("name", channel.name);
                    channel_tbl.insert("type", channel.type);
                    inputs.push_back(channel_tbl);
                }
                block_tbl.insert("input_channels", inputs);
            }
            
            // Output channels
            if (!block.output_channels.empty()) {
                toml::array outputs;
                for (const auto& channel : block.output_channels) {
                    toml::table channel_tbl;
                    channel_tbl.insert("name", channel.name);
                    channel_tbl.insert("type", channel.type);
                    outputs.push_back(channel_tbl);
                }
                block_tbl.insert("output_channels", outputs);
            }
            
            blocks_array.push_back(block_tbl);
        }
        
        tbl.insert("blocks", blocks_array);
        
        // Write to file
        std::ofstream file(cache_path);
        file << tbl;
        file.close();
        
        std::cout << "Cache saved to: " << cache_path << " (" << blocks.size() << " blocks)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error saving cache: " << e.what() << std::endl;
    }
}

std::vector<std::string> BlockCache::getModifiedFiles(const std::string& source_path) const
{
    std::vector<std::string> modified_files;
    
    if (!hasCacheFile()) {
        // No cache, all files need scanning
        namespace fs = std::filesystem;
        for (const auto& entry : fs::recursive_directory_iterator(source_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".hpp") {
                modified_files.push_back(entry.path().string());
            }
        }
        return modified_files;
    }
    
    try {
        auto config = toml::parse_file(cache_path.string());
        
        // Build a map of cached files and their modification times
        std::map<std::string, std::filesystem::file_time_type> cached_times;
        
        auto blocks_array = config["blocks"].as_array();
        if (blocks_array) {
            for (const auto& block_node : *blocks_array) {
                auto block = block_node.as_table();
                if (block) {
                    auto header_path = (*block)["header_path"].value<std::string>();
                    auto last_modified = (*block)["last_modified"].value<std::string>();
                    if (header_path && last_modified) {
                        cached_times[*header_path] = stringToTime(*last_modified);
                    }
                }
            }
        }
        
        // Check all header files
        namespace fs = std::filesystem;
        for (const auto& entry : fs::recursive_directory_iterator(source_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".hpp") {
                std::string file_path = entry.path().string();
                auto current_time = fs::last_write_time(entry.path());
                
                // Check if file is new or modified
                auto it = cached_times.find(file_path);
                if (it == cached_times.end() || it->second < current_time) {
                    modified_files.push_back(file_path);
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error checking modified files: " << e.what() << std::endl;
        // On error, scan all files
        namespace fs = std::filesystem;
        for (const auto& entry : fs::recursive_directory_iterator(source_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".hpp") {
                modified_files.push_back(entry.path().string());
            }
        }
    }
    
    return modified_files;
}

std::filesystem::path BlockCache::getCachePath() const
{
    return cache_path;
}

std::string BlockCache::timeToString(const std::filesystem::file_time_type& time) const
{
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    
    auto tt = std::chrono::system_clock::to_time_t(sctp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&tt), "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

std::filesystem::file_time_type BlockCache::stringToTime(const std::string& str) const
{
    std::tm tm = {};
    std::stringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    
    return std::filesystem::file_time_type::clock::now() + 
           (tp - std::chrono::system_clock::now());
}

} // namespace clerflow