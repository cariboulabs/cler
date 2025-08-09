#include "src/block_parser.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <header_file>" << std::endl;
        return 1;
    }
    
    std::string header_path = argv[1];
    
    clerflow::BlockParser parser;
    
    // First check if it's a block header
    std::cout << "Testing: " << header_path << std::endl;
    bool is_block = parser.isBlockHeader(header_path);
    std::cout << "Is block header (quick check): " << (is_block ? "Yes" : "No") << std::endl;
    
    // Try to parse it
    std::cout << "\nParsing with libclang..." << std::endl;
    clerflow::BlockMetadata metadata = parser.parseHeader(header_path);
    
    if (metadata.is_valid) {
        std::cout << "✓ Successfully parsed!" << std::endl;
        std::cout << "  Class name: " << metadata.class_name << std::endl;
        std::cout << "  Base class: " << metadata.base_class << std::endl;
        
        if (!metadata.template_params.empty()) {
            std::cout << "  Template parameters:" << std::endl;
            for (const auto& param : metadata.template_params) {
                std::cout << "    - " << param.name << " (" << param.type << ")" << std::endl;
            }
        }
        
        if (!metadata.constructor_params.empty()) {
            std::cout << "  Constructor parameters:" << std::endl;
            for (const auto& param : metadata.constructor_params) {
                std::cout << "    - " << param.name << " : " << param.type << std::endl;
            }
        }
        
        if (!metadata.input_channels.empty()) {
            std::cout << "  Input channels:" << std::endl;
            for (const auto& channel : metadata.input_channels) {
                std::cout << "    - " << channel.name << " : " << channel.type << std::endl;
            }
        }
        
        if (!metadata.output_channels.empty()) {
            std::cout << "  Output channels:" << std::endl;
            for (const auto& channel : metadata.output_channels) {
                std::cout << "    - " << channel.name << " : " << channel.type << std::endl;
            }
        }
    } else {
        std::cout << "✗ Failed to parse: " << metadata.error_message << std::endl;
    }
    
    return 0;
}