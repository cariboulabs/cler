#include "block_parser.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace clerflow {

// Helper structure for AST visitor
struct VisitorData {
    BlockMetadata* metadata;
    bool found_block_class = false;
    bool inside_target_class = false;
    std::string current_class_name;
};

BlockParser::BlockParser() {
    // Initialize clang index with default options
    clang_index = clang_createIndex(0, 0);
}

BlockParser::~BlockParser() {
    if (clang_index) {
        clang_disposeIndex(clang_index);
    }
}

bool BlockParser::isBlockHeader(const std::string& header_path) {
    // Quick check if file contains "BlockBase" inheritance
    std::ifstream file(header_path);
    if (!file) return false;
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.find(": public cler::BlockBase") != std::string::npos ||
            line.find(": public BlockBase") != std::string::npos ||
            line.find(":public cler::BlockBase") != std::string::npos ||
            line.find(":public BlockBase") != std::string::npos) {
            return true;
        }
    }
    return false;
}

CXChildVisitResult BlockParser::visitNode(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
    VisitorData* data = static_cast<VisitorData*>(client_data);
    CXCursorKind kind = clang_getCursorKind(cursor);
    
    // Get cursor name
    CXString cursor_name = clang_getCursorSpelling(cursor);
    std::string name(clang_getCString(cursor_name));
    clang_disposeString(cursor_name);
    
    // Handle class/struct declarations (including templates)
    if (kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl || 
        kind == CXCursor_ClassTemplate) {
        // Only debug output for non-empty names
        if (!name.empty() && name != "type-parameter-0-0") {
            // std::cout << "DEBUG: Found class/struct/template: " << name << std::endl;
        }
        // Check if this inherits from BlockBase
        if (!data->found_block_class) {
            // Check base classes
            clang_visitChildren(cursor, [](CXCursor c, CXCursor p, CXClientData cd) {
                VisitorData* d = static_cast<VisitorData*>(cd);
                if (clang_getCursorKind(c) == CXCursor_CXXBaseSpecifier) {
                    CXType base_type = clang_getCursorType(c);
                    CXString base_name = clang_getTypeSpelling(base_type);
                    std::string base_str(clang_getCString(base_name));
                    clang_disposeString(base_name);
                    
                    if (base_str.find("BlockBase") != std::string::npos) {
                        d->found_block_class = true;
                        d->inside_target_class = true;
                        d->metadata->base_class = base_str;
                        // Get the parent class name from the outer cursor (p parameter)
                        CXString class_name = clang_getCursorSpelling(p);
                        d->metadata->class_name = clang_getCString(class_name);
                        d->current_class_name = d->metadata->class_name;
                        clang_disposeString(class_name);
                        
                        // Found the BlockBase-derived class
                                  
                        // Now look for template parameters of this class
                        clang_visitChildren(p, [](CXCursor tc, CXCursor /*tp*/, CXClientData tcd) {
                            VisitorData* td = static_cast<VisitorData*>(tcd);
                            if (clang_getCursorKind(tc) == CXCursor_TemplateTypeParameter) {
                                CXString tname = clang_getCursorSpelling(tc);
                                std::string tname_str(clang_getCString(tname));
                                clang_disposeString(tname);
                                
                                if (!tname_str.empty() && tname_str != "type-parameter-0-0") {
                                    BlockMetadata::TemplateParam param;
                                    param.type = "typename";
                                    param.name = tname_str;
                                    td->metadata->template_params.push_back(param);
                                }
                            }
                            return CXChildVisit_Continue;
                        }, d);
                    }
                }
                return CXChildVisit_Continue;
            }, data);
        }
    }
    
    // If we're inside the block class, extract information
    if (data->inside_target_class) {
        // Extract constructor
        if (kind == CXCursor_Constructor) {
            int num_args = clang_Cursor_getNumArguments(cursor);
            for (int i = 0; i < num_args; ++i) {
                CXCursor arg = clang_Cursor_getArgument(cursor, i);
                CXType arg_type = clang_getCursorType(arg);
                CXString type_name = clang_getTypeSpelling(arg_type);
                CXString arg_name = clang_getCursorSpelling(arg);
                
                BlockMetadata::ConstructorParam param;
                param.type = clang_getCString(type_name);
                param.name = clang_getCString(arg_name);
                
                // TODO: Extract default values if present
                
                data->metadata->constructor_params.push_back(param);
                
                clang_disposeString(type_name);
                clang_disposeString(arg_name);
            }
        }
        
        // Extract member variables (input channels)
        if (kind == CXCursor_FieldDecl) {
            CXType field_type = clang_getCursorType(cursor);
            CXString type_name = clang_getTypeSpelling(field_type);
            std::string type_str(clang_getCString(type_name));
            clang_disposeString(type_name);
            
            // Check if it's a Channel
            if (type_str.find("Channel<") != std::string::npos ||
                type_str.find("cler::Channel<") != std::string::npos) {
                BlockMetadata::ChannelInfo channel;
                channel.name = name;
                
                // Extract template type from Channel<T>
                size_t start = type_str.find('<');
                size_t end = type_str.rfind('>');
                if (start != std::string::npos && end != std::string::npos) {
                    channel.type = type_str.substr(start + 1, end - start - 1);
                }
                
                data->metadata->input_channels.push_back(channel);
            }
        }
        
        // Extract procedure method to find output channels
        if (kind == CXCursor_CXXMethod && name == "procedure") {
            int num_params = clang_Cursor_getNumArguments(cursor);
            for (int i = 0; i < num_params; ++i) {
                CXCursor param = clang_Cursor_getArgument(cursor, i);
                CXType param_type = clang_getCursorType(param);
                CXString type_name = clang_getTypeSpelling(param_type);
                CXString param_name = clang_getCursorSpelling(param);
                
                std::string type_str(clang_getCString(type_name));
                std::string name_str(clang_getCString(param_name));
                
                // Check if it's a ChannelBase pointer (output)
                if (type_str.find("ChannelBase") != std::string::npos) {
                    BlockMetadata::ChannelInfo channel;
                    channel.name = name_str;
                    
                    // Extract template type
                    size_t start = type_str.find('<');
                    size_t end = type_str.rfind('>');
                    if (start != std::string::npos && end != std::string::npos) {
                        channel.type = type_str.substr(start + 1, end - start - 1);
                        // Clean up the type (remove "class " prefix if present)
                        if (channel.type.substr(0, 6) == "class ") {
                            channel.type = channel.type.substr(6);
                        }
                    }
                    
                    data->metadata->output_channels.push_back(channel);
                }
                
                clang_disposeString(type_name);
                clang_disposeString(param_name);
            }
        }
    }
    
    
    return CXChildVisit_Recurse;
}

BlockMetadata BlockParser::parseHeader(const std::string& header_path) {
    BlockMetadata metadata;
    metadata.header_path = header_path;
    
    // Create translation unit
    const char* args[] = {
        "-xc++",
        "-std=c++17",
        "-I/home/alon/repos/cler/include",  // Add CLER include path
        "-I/home/alon/repos/cler/desktop_blocks"  // Add desktop_blocks path
    };
    
    CXTranslationUnit unit = clang_parseTranslationUnit(
        clang_index,
        header_path.c_str(),
        args, 4,  // command line args
        nullptr, 0,  // unsaved files
        CXTranslationUnit_None
    );
    
    if (!unit) {
        metadata.is_valid = false;
        metadata.error_message = "Failed to parse translation unit";
        return metadata;
    }
    
    // Visit AST
    VisitorData visitor_data;
    visitor_data.metadata = &metadata;
    
    CXCursor root_cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(root_cursor, visitNode, &visitor_data);
    
    // Clean up
    clang_disposeTranslationUnit(unit);
    
    // Validate metadata
    metadata.is_valid = !metadata.class_name.empty();
    if (!metadata.is_valid) {
        metadata.error_message = "No BlockBase-derived class found";
    }
    
    return metadata;
}

// BlockLibraryScanner implementation

BlockLibraryScanner::Library BlockLibraryScanner::scanDirectory(
    const std::string& path, 
    const std::string& library_name) 
{
    Library library;
    library.name = library_name;
    library.root_path = path;
    
    namespace fs = std::filesystem;
    
    try {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".hpp") {
                std::string file_path = entry.path().string();
                
                // Quick check if it's a block header
                if (parser.isBlockHeader(file_path)) {
                    BlockMetadata metadata = parser.parseHeader(file_path);
                    if (metadata.is_valid) {
                        metadata.library_name = library_name;
                        metadata.category = extractCategory(file_path, path);
                        library.blocks.push_back(metadata);
                        
                        // Add to category map
                        library.blocks_by_category[metadata.category].push_back(
                            &library.blocks.back()
                        );
                    } else {
                        std::cerr << "Failed to parse " << file_path 
                                  << ": " << metadata.error_message << std::endl;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning directory: " << e.what() << std::endl;
    }
    
    return library;
}

std::string BlockLibraryScanner::extractCategory(
    const std::string& file_path, 
    const std::string& root_path) 
{
    namespace fs = std::filesystem;
    
    fs::path file(file_path);
    fs::path root(root_path);
    fs::path relative = fs::relative(file.parent_path(), root);
    
    if (relative == ".") {
        return "Uncategorized";
    }
    
    // Convert path to category string (e.g., "sources/dsp" -> "Sources/DSP")
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
    
    return category;
}

BlockLibraryScanner::Library BlockLibraryScanner::scanDesktopBlocks() {
    // Scan the built-in desktop_blocks directory
    std::string desktop_blocks_path = "/home/alon/repos/cler/desktop_blocks";
    Library lib = scanDirectory(desktop_blocks_path, "Desktop Blocks");
    lib.is_builtin = true;
    return lib;
}

} // namespace clerflow