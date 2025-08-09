#include "../../src/block_parser.hpp"
#include <iostream>
#include <filesystem>
#include <vector>

void test_single_file(const std::string& header_path) {
    clerflow::BlockParser parser;
    
    std::cout << "\n=== Testing: " << header_path << " ===" << std::endl;
    
    // Quick check
    bool is_block = parser.isBlockHeader(header_path);
    std::cout << "Quick check: " << (is_block ? "Contains BlockBase" : "No BlockBase found") << std::endl;
    
    if (!is_block) {
        return;
    }
    
    // Full parse
    clerflow::BlockMetadata metadata = parser.parseHeader(header_path);
    
    if (metadata.is_valid) {
        std::cout << "✓ Parse successful!" << std::endl;
        std::cout << "  Class: " << metadata.class_name << std::endl;
        
        if (!metadata.template_params.empty()) {
            std::cout << "  Templates: ";
            for (const auto& p : metadata.template_params) {
                std::cout << p.name << " ";
            }
            std::cout << std::endl;
        }
        
        std::cout << "  Constructor params: " << metadata.constructor_params.size() << std::endl;
        std::cout << "  Input channels: " << metadata.input_channels.size() << std::endl;
        std::cout << "  Output channels: " << metadata.output_channels.size() << std::endl;
    } else {
        std::cout << "✗ Parse failed: " << metadata.error_message << std::endl;
    }
}

void test_desktop_blocks() {
    namespace fs = std::filesystem;
    std::string desktop_blocks = "/home/alon/repos/cler/desktop_blocks";
    
    std::vector<std::string> categories = {
        "math", "sources", "sinks", "utils", "noise", 
        "channelizers", "resamplers", "plots", "udp", "ezgmsk"
    };
    
    int total_files = 0;
    int detected_blocks = 0;
    int parsed_blocks = 0;
    
    std::cout << "\n=== Testing all desktop_blocks ===" << std::endl;
    
    clerflow::BlockParser parser;
    
    for (const auto& category : categories) {
        std::string cat_path = desktop_blocks + "/" + category;
        if (!fs::exists(cat_path)) continue;
        
        std::cout << "\nCategory: " << category << std::endl;
        
        for (const auto& entry : fs::directory_iterator(cat_path)) {
            if (entry.path().extension() == ".hpp") {
                total_files++;
                std::string file_path = entry.path().string();
                
                // Quick check
                bool is_block = parser.isBlockHeader(file_path);
                if (is_block) {
                    detected_blocks++;
                    
                    // Full parse
                    clerflow::BlockMetadata metadata = parser.parseHeader(file_path);
                    if (metadata.is_valid) {
                        parsed_blocks++;
                        std::cout << "  ✓ " << entry.path().filename().string() 
                                  << " -> " << metadata.class_name << std::endl;
                    } else {
                        std::cout << "  ✗ " << entry.path().filename().string() 
                                  << " (detected but parse failed)" << std::endl;
                    }
                }
            }
        }
    }
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Total .hpp files: " << total_files << std::endl;
    std::cout << "Detected as blocks: " << detected_blocks << std::endl;
    std::cout << "Successfully parsed: " << parsed_blocks << std::endl;
    std::cout << "Success rate: " << (detected_blocks > 0 ? 
        (100.0 * parsed_blocks / detected_blocks) : 0) << "%" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc == 2) {
        // Test single file
        test_single_file(argv[1]);
    } else {
        // Test all desktop_blocks
        test_desktop_blocks();
    }
    
    return 0;
}