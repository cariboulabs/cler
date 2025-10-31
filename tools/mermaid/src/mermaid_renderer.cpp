#include "mermaid_renderer.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace cler {

MermaidRenderer::MermaidRenderer(const std::string& direction,
                                 const std::string& fence_style)
    : direction_(direction), fence_style_(fence_style) {}

std::string MermaidRenderer::render(const FlowGraph& flowgraph) {
    return generate_mermaid(flowgraph);
}

void MermaidRenderer::render_to_file(const FlowGraph& flowgraph,
                                     const std::string& output_path) {
    std::string content = generate_mermaid(flowgraph);
    std::ofstream file(output_path + ".md");
    file << content;
}

std::string MermaidRenderer::generate_mermaid(const FlowGraph& flowgraph) {
    std::ostringstream ss;

    // Opening fence
    if (fence_style_ == "backticks") {
        ss << "```mermaid\n";
    } else if (fence_style_ == "colons") {
        ss << "::: mermaid\n";
    }

    ss << "flowchart " << direction_ << "\n";

    // Add nodes
    for (const auto& [block_name, block] : flowgraph.blocks) {
        if (!block.in_flowgraph) continue;

        std::string node_id = get_node_id(block_name);
        std::string node_label = create_node_label(block);
        auto [shape_start, shape_end] = get_node_shape(block);

        ss << "    " << node_id << shape_start << "\""
           << node_label << "\"" << shape_end << "\n";
    }

    // Add edges
    for (const auto& conn : flowgraph.connections) {
        std::string source_id = get_node_id(conn.source_block);
        std::string target_id = get_node_id(conn.target_block);
        ss << "    " << source_id << " --> " << target_id << "\n";
    }

    // Add styling
    ss << generate_styling(flowgraph);

    // Closing fence
    if (fence_style_ == "backticks") {
        ss << "```\n";
    } else if (fence_style_ == "colons") {
        ss << ":::\n";
    }

    return ss.str();
}

std::string MermaidRenderer::get_node_id(const std::string& block_name) {
    if (node_map_.find(block_name) == node_map_.end()) {
        // Create valid Mermaid ID
        std::string clean_name;
        for (char c : block_name) {
            clean_name += std::isalnum(c) ? c : '_';
        }
        node_map_[block_name] = "node_" + clean_name;
    }
    return node_map_[block_name];
}

std::string MermaidRenderer::html_escape(const std::string& text) {
    std::string result;
    result.reserve(text.size() * 1.2);  // Pre-allocate with some headroom

    for (char c : text) {
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += c;
        }
    }
    return result;
}

std::string MermaidRenderer::create_node_label(const Block& block) const {
    std::string label = block.name;

    // Add type (remove 'Block' suffix for cleaner display)
    std::string clean_type = block.type;
    size_t pos = clean_type.find("Block");
    if (pos != std::string::npos) {
        clean_type.erase(pos, 5);
    }
    label += "\\n(" + clean_type + ")";

    // Show template parameters with safe HTML escaping
    if (!block.template_params.empty()) {
        std::string escaped = html_escape(block.template_params);
        label += "\\n&lt;" + escaped + "&gt;";
    }

    return label;
}

std::pair<std::string, std::string> MermaidRenderer::get_node_shape(const Block& block) const {
    if (block.is_source()) {
        return {"([", "])"};  // Stadium shape
    } else if (block.is_sink()) {
        return {"[/", "/]"};  // Trapezoid
    } else {
        return {"[", "]"};    // Rectangle
    }
}

std::string MermaidRenderer::generate_styling(const FlowGraph& flowgraph) {
    std::ostringstream ss;

    for (const auto& [block_name, block] : flowgraph.blocks) {
        if (!block.in_flowgraph) continue;

        std::string node_id = get_node_id(block_name);

        if (block.is_source()) {
            ss << "    style " << node_id << " fill:#e1f5fe\n";
        } else if (block.is_sink()) {
            ss << "    style " << node_id << " fill:#f3e5f5\n";
        } else {
            ss << "    style " << node_id << " fill:#e8f5e8\n";
        }
    }

    return ss.str();
}

} // namespace cler
