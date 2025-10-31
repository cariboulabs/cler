#include "cpp_parser.h"
#include <iostream>
#include <algorithm>
#include <cstring>

// Forward declare tree-sitter-cpp language function
extern "C" {
    const TSLanguage *tree_sitter_cpp();
}

namespace cler {

CppParser::CppParser() {
    parser_ = ts_parser_new();
    ts_parser_set_language(parser_, tree_sitter_cpp());
}

CppParser::~CppParser() {
    ts_parser_delete(parser_);
}

bool CppParser::is_flowgraph_file(const std::string& content) {
    // Fast pre-screen: look for flowgraph indicators
    return content.find("BlockRunner") != std::string::npos ||
           content.find("make_") != std::string::npos &&
           content.find("_flowgraph") != std::string::npos;
}

std::string CppParser::extract_template_params_robust(const std::string& text) const {
    size_t start = text.find('<');
    if (start == std::string::npos) return "";

    int depth = 0;
    for (size_t i = start; i < text.size(); ++i) {
        if (text[i] == '<') depth++;
        if (text[i] == '>') {
            depth--;
            if (depth == 0) {
                return text.substr(start + 1, i - start - 1);
            }
        }
    }
    return "";  // Unmatched brackets
}

FlowGraph CppParser::parse_file(const std::string& content, const std::string& filename) {
    blocks_.clear();
    connections_.clear();
    flowgraph_name_.clear();
    source_code_ = content.c_str();

    FlowGraph result;
    result.name = filename;

    try {
        // Use RAII wrapper for automatic cleanup
        TSTreePtr tree(ts_parser_parse_string(parser_, nullptr, content.c_str(), content.length()));

        if (!tree) {
            result.error_message = "Failed to parse file: tree-sitter parse error";
            result.is_valid = false;
            return result;
        }

        TSNode root = tree.root_node();

        // Extract blocks and connections
        extract_blocks(root, content);
        extract_flowgraph(root, content);

        // Infer channel directions
        infer_channel_directions();

        // Determine flowgraph name
        std::string name = flowgraph_name_.empty() ? filename : flowgraph_name_;
        size_t slash_pos = name.find_last_of('/');
        if (slash_pos != std::string::npos) {
            name = name.substr(slash_pos + 1);
        }
        size_t dot_pos = name.find(".cpp");
        if (dot_pos != std::string::npos) {
            name = name.substr(0, dot_pos);
        }

        result.name = name;
        result.blocks = blocks_;
        result.connections = connections_;

        // Validate the flowgraph
        result.validate();

    } catch (const std::exception& e) {
        result.error_message = std::string("Exception during parsing: ") + e.what();
        result.is_valid = false;
    }

    return result;
}

void CppParser::walk_ast(TSNode node, void (CppParser::*visitor)(TSNode, const std::string&)) {
    (this->*visitor)(node, "");

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        walk_ast(child, visitor);
    }
}

void CppParser::extract_blocks(TSNode node, const std::string& content) {
    walk_ast(node, &CppParser::process_declaration);
}

void CppParser::extract_flowgraph(TSNode node, const std::string& content) {
    walk_ast(node, &CppParser::process_call_expression);
}

std::string CppParser::get_node_text(TSNode node) const {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    return std::string(source_code_ + start, end - start);
}

TSNode CppParser::find_child_by_type(TSNode node, const char* type) const {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, type) == 0) {
            return child;
        }
    }
    TSNode null_node = {0};
    return null_node;
}

void CppParser::process_declaration(TSNode node, const std::string&) {
    const char* node_type = ts_node_type(node);
    if (strcmp(node_type, "declaration") != 0) {
        return;
    }

    // Look for init_declarator
    TSNode declarator = find_child_by_type(node, "init_declarator");
    if (ts_node_is_null(declarator)) {
        return;
    }

    // Get type
    TSNode type_node = find_child_by_type(node, "type_identifier");
    if (ts_node_is_null(type_node)) {
        type_node = find_child_by_type(node, "template_type");
    }
    if (ts_node_is_null(type_node)) {
        return;
    }

    std::string type_name = get_node_text(type_node);

    // For template types, extract base name
    if (strcmp(ts_node_type(type_node), "template_type") == 0) {
        TSNode template_name = find_child_by_type(type_node, "type_identifier");
        if (!ts_node_is_null(template_name)) {
            type_name = get_node_text(template_name);
        }
    }

    // Only process block types
    if (type_name.find("Block") == std::string::npos &&
        type_name.find("block") == std::string::npos) {
        return;
    }

    // Get variable name
    TSNode identifier = find_child_by_type(declarator, "identifier");
    if (ts_node_is_null(identifier)) {
        return;
    }
    std::string var_name = get_node_text(identifier);

    // Skip if already processed
    if (blocks_.find(var_name) != blocks_.end()) {
        return;
    }

    // Extract template parameters using robust bracket counting
    std::string template_params;
    if (strcmp(ts_node_type(type_node), "template_type") == 0) {
        TSNode template_args = find_child_by_type(type_node, "template_argument_list");
        if (!ts_node_is_null(template_args)) {
            std::string full = get_node_text(template_args);
            template_params = extract_template_params_robust(full);
        }
    }

    // Create block
    Block block;
    block.name = var_name;
    block.type = type_name;
    block.template_params = template_params;
    block.line = ts_node_start_point(declarator).row + 1;
    block.column = ts_node_start_point(declarator).column;

    blocks_[var_name] = block;
}

void CppParser::process_call_expression(TSNode node, const std::string&) {
    const char* node_type = ts_node_type(node);
    if (strcmp(node_type, "call_expression") != 0) {
        return;
    }

    std::string func_name = get_function_name(node);

    if (func_name.find("make_") != std::string::npos &&
        func_name.find("_flowgraph") != std::string::npos) {
        // Extract flowgraph name
        size_t make_pos = func_name.find("make_");
        size_t flowgraph_pos = func_name.find("_flowgraph");
        if (make_pos != std::string::npos && flowgraph_pos != std::string::npos) {
            flowgraph_name_ = func_name.substr(make_pos + 5, flowgraph_pos - make_pos - 5);
        }

        // Extract BlockRunner calls
        extract_blockrunners(node, "");
    } else if (func_name.find("BlockRunner") != std::string::npos) {
        extract_single_blockrunner(node, "");
    }
}

std::string CppParser::get_function_name(TSNode node) const {
    if (ts_node_child_count(node) == 0) {
        return "";
    }

    TSNode func_expr = ts_node_child(node, 0);
    const char* func_type = ts_node_type(func_expr);

    if (strcmp(func_type, "identifier") == 0 ||
        strcmp(func_type, "scoped_identifier") == 0) {
        return get_node_text(func_expr);
    } else if (strcmp(func_type, "field_expression") == 0) {
        TSNode field_id = find_child_by_type(func_expr, "field_identifier");
        if (!ts_node_is_null(field_id)) {
            return get_node_text(field_id);
        }
    }

    return get_node_text(func_expr);
}

void CppParser::extract_blockrunners(TSNode node, const std::string&) {
    TSNode args_list = find_child_by_type(node, "argument_list");
    if (ts_node_is_null(args_list)) {
        return;
    }

    uint32_t child_count = ts_node_child_count(args_list);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode arg = ts_node_child(args_list, i);
        if (strcmp(ts_node_type(arg), "call_expression") == 0) {
            std::string func_name = get_function_name(arg);
            if (func_name.find("BlockRunner") != std::string::npos) {
                extract_single_blockrunner(arg, "");
            }
        }
    }
}

void CppParser::extract_single_blockrunner(TSNode node, const std::string&) {
    TSNode args_list = find_child_by_type(node, "argument_list");
    if (ts_node_is_null(args_list)) {
        return;
    }

    std::vector<std::string> args;
    uint32_t child_count = ts_node_child_count(args_list);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(args_list, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, ",") != 0 &&
            strcmp(child_type, "(") != 0 &&
            strcmp(child_type, ")") != 0) {
            std::string arg_text = get_node_text(child);
            if (!arg_text.empty() && arg_text[0] == '&') {
                arg_text = arg_text.substr(1);
            }
            args.push_back(arg_text);
        }
    }

    if (args.empty()) {
        return;
    }

    // First argument is source
    std::string source_block = args[0];
    if (blocks_.find(source_block) != blocks_.end()) {
        blocks_[source_block].in_flowgraph = true;
    }

    // Remaining are targets
    for (size_t i = 1; i < args.size(); i++) {
        std::string target_arg = args[i];
        size_t dot_pos = target_arg.find('.');
        if (dot_pos == std::string::npos) {
            continue;
        }

        std::string target_block = target_arg.substr(0, dot_pos);
        std::string target_channel = target_arg.substr(dot_pos + 1);

        if (blocks_.find(target_block) != blocks_.end()) {
            blocks_[target_block].in_flowgraph = true;
        }

        Connection conn;
        conn.source_block = source_block;
        conn.source_channel = "out";
        conn.target_block = target_block;
        conn.target_channel = target_channel;

        // Extract array index
        size_t bracket_pos = target_channel.find('[');
        if (bracket_pos != std::string::npos) {
            size_t close_bracket = target_channel.find(']');
            if (close_bracket != std::string::npos) {
                std::string index_str = target_channel.substr(bracket_pos + 1,
                                                              close_bracket - bracket_pos - 1);
                try {
                    conn.channel_index = std::stoi(index_str);
                    conn.target_channel = target_channel.substr(0, bracket_pos);
                } catch (...) {
                    // Keep original channel name if parse fails
                }
            }
        }

        // Avoid duplicates
        bool duplicate = false;
        for (const auto& c : connections_) {
            if (c.source_block == conn.source_block &&
                c.target_block == conn.target_block &&
                c.target_channel == conn.target_channel &&
                c.channel_index == conn.channel_index) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            connections_.push_back(conn);
        }
    }
}

void CppParser::infer_channel_directions() {
    for (const auto& conn : connections_) {
        // Add outputs to source
        auto source_it = blocks_.find(conn.source_block);
        if (source_it != blocks_.end()) {
            auto& outputs = source_it->second.outputs;
            if (std::find(outputs.begin(), outputs.end(), conn.source_channel) == outputs.end()) {
                outputs.push_back(conn.source_channel);
            }
        }

        // Add inputs to target
        auto target_it = blocks_.find(conn.target_block);
        if (target_it != blocks_.end()) {
            std::string channel_name = conn.target_channel;
            if (conn.channel_index >= 0) {
                channel_name += "[" + std::to_string(conn.channel_index) + "]";
            }
            auto& inputs = target_it->second.inputs;
            if (std::find(inputs.begin(), inputs.end(), channel_name) == inputs.end()) {
                inputs.push_back(channel_name);
            }
        }
    }
}

} // namespace cler
