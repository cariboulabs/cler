#pragma once

#include <clang-c/Index.h>
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace clerflow {

// Metadata extracted from a CLER block header
struct BlockMetadata {
    std::string class_name;           // e.g., "SourceCWBlock"
    std::string header_path;           // Full path to .hpp file
    std::string category;              // Derived from directory structure
    std::string base_class;            // Should be "cler::BlockBase"
    
    // Template parameters
    struct TemplateParam {
        std::string type;              // "typename" or "class"
        std::string name;              // "T"
        std::string default_value;     // Optional default
    };
    std::vector<TemplateParam> template_params;
    
    // Constructor parameters
    struct ConstructorParam {
        std::string type;
        std::string name;
        std::string default_value;
    };
    std::vector<ConstructorParam> constructor_params;
    
    // Channel information
    struct ChannelInfo {
        std::string name;
        std::string type;              // e.g., "float", "std::complex<float>"
        bool is_array;                 // For things like in[2]
        size_t array_size;
    };
    std::vector<ChannelInfo> input_channels;   // Member channels
    std::vector<ChannelInfo> output_channels;  // From procedure() params
    
    // Library info
    std::string library_name;
    bool is_builtin = false;
    
    // Validation
    bool is_valid = false;
    std::string error_message;
};

class BlockParser {
public:
    BlockParser();
    ~BlockParser();
    
    // Parse a single header file
    BlockMetadata parseHeader(const std::string& header_path);
    
    // Check if a file contains a CLER block
    bool isBlockHeader(const std::string& header_path);
    
private:
    CXIndex clang_index;
    
    // Helper to extract metadata from AST
    void extractFromAST(CXTranslationUnit unit, BlockMetadata& metadata);
    
    // Visitor callbacks for AST traversal
    static CXChildVisitResult visitNode(CXCursor cursor, CXCursor parent, CXClientData client_data);
};

// Higher-level library scanner
class BlockLibraryScanner {
public:
    struct Library {
        std::string name;
        std::string root_path;
        std::vector<BlockMetadata> blocks;
        std::map<std::string, std::vector<BlockMetadata*>> blocks_by_category;
        bool is_builtin = false;
    };
    
    // Scan a directory for CLER blocks
    Library scanDirectory(const std::string& path, const std::string& library_name);
    
    // Scan the built-in desktop_blocks
    Library scanDesktopBlocks();
    
private:
    BlockParser parser;
    
    // Extract category from file path
    std::string extractCategory(const std::string& file_path, const std::string& root_path);
};

} // namespace clerflow