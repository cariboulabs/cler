#pragma once

#include "flowgraph.h"
#include <string>
#include <map>

namespace cler {

class MermaidRenderer {
public:
    MermaidRenderer(const std::string& direction = "LR",
                    const std::string& fence_style = "backticks");

    std::string render(const FlowGraph& flowgraph);
    void render_to_file(const FlowGraph& flowgraph, const std::string& output_path);

private:
    std::string generate_mermaid(const FlowGraph& flowgraph);
    std::string get_node_id(const std::string& block_name);
    std::string create_node_label(const Block& block) const;
    std::pair<std::string, std::string> get_node_shape(const Block& block) const;
    std::string generate_styling(const FlowGraph& flowgraph);

    // Safe HTML escaping
    static std::string html_escape(const std::string& text);

    std::string direction_;
    std::string fence_style_;
    std::map<std::string, std::string> node_map_;
};

} // namespace cler
