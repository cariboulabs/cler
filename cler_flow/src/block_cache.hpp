/******************************************************************************************
*                                                                                         *
*    BlockCache - Cache management for block library metadata                            *
*                                                                                         *
******************************************************************************************/

#pragma once

#include "block_parser.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>

namespace clerflow {

class BlockCache {
public:
    BlockCache();
    ~BlockCache() = default;
    
    // Check if cache exists and is valid
    bool hasCacheFile() const;
    bool isCacheValid(const std::string& source_path) const;
    
    // Load blocks from cache
    std::vector<BlockMetadata> loadFromCache();
    
    // Save blocks to cache
    void saveToCache(const std::vector<BlockMetadata>& blocks, const std::string& source_path);
    
    // Get list of files that need scanning (new or modified)
    std::vector<std::string> getModifiedFiles(const std::string& source_path) const;
    
    // Update specific blocks in cache
    void updateCache(const std::vector<BlockMetadata>& updated_blocks);
    
    // Get cache file path
    std::filesystem::path getCachePath() const;
    
private:
    std::filesystem::path cache_path;
    
    // Helper to get file modification time
    std::filesystem::file_time_type getFileModTime(const std::filesystem::path& path) const;
    
    // Convert filesystem time to string for TOML
    std::string timeToString(const std::filesystem::file_time_type& time) const;
    std::filesystem::file_time_type stringToTime(const std::string& str) const;
};

} // namespace clerflow