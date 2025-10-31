#pragma once

#include "flowgraph.h"
#include <tree_sitter/api.h>
#include <memory>

namespace cler {

// RAII wrapper for TSTree to prevent memory leaks
class TSTreePtr {
public:
    explicit TSTreePtr(TSTree* tree) : tree_(tree) {}
    ~TSTreePtr() { if (tree_) ts_tree_delete(tree_); }

    // No copy
    TSTreePtr(const TSTreePtr&) = delete;
    TSTreePtr& operator=(const TSTreePtr&) = delete;

    // Move only
    TSTreePtr(TSTreePtr&& other) noexcept : tree_(other.tree_) {
        other.tree_ = nullptr;
    }
    TSTreePtr& operator=(TSTreePtr&& other) noexcept {
        if (this != &other) {
            if (tree_) ts_tree_delete(tree_);
            tree_ = other.tree_;
            other.tree_ = nullptr;
        }
        return *this;
    }

    TSNode root_node() const { return ts_tree_root_node(tree_); }
    operator bool() const { return tree_ != nullptr; }

private:
    TSTree* tree_;
};

class CppParser {
public:
    CppParser();
    ~CppParser();

    FlowGraph parse_file(const std::string& content, const std::string& filename);

    // Fast pre-screen before full parsing
    static bool is_flowgraph_file(const std::string& content);

private:
    // Helper for template extraction with bracket counting
    std::string extract_template_params_robust(const std::string& text) const;
    void extract_blocks(TSNode node, const std::string& content);
    void extract_flowgraph(TSNode node, const std::string& content);
    void process_declaration(TSNode node, const std::string& content);
    void process_call_expression(TSNode node, const std::string& content);
    void extract_blockrunners(TSNode node, const std::string& content);
    void extract_single_blockrunner(TSNode node, const std::string& content);
    void infer_channel_directions();

    std::string get_node_text(TSNode node) const;
    TSNode find_child_by_type(TSNode node, const char* type) const;
    std::string get_function_name(TSNode node) const;
    void walk_ast(TSNode node, void (CppParser::*visitor)(TSNode, const std::string&));

    TSParser* parser_;
    const char* source_code_;
    std::map<std::string, Block> blocks_;
    std::vector<Connection> connections_;
    std::string flowgraph_name_;
};

} // namespace cler
