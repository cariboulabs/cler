#include "flowgraph.h"
#include <algorithm>

namespace cler {

bool Block::is_source() const {
    return inputs.empty() && !outputs.empty();
}

bool Block::is_sink() const {
    return outputs.empty() && !inputs.empty();
}

bool FlowGraph::validate() {
    warnings.clear();
    error_message.clear();

    // Check for dangling connections
    for (const auto& conn : connections) {
        if (blocks.find(conn.source_block) == blocks.end()) {
            error_message = "Unknown source block: " + conn.source_block;
            is_valid = false;
            return false;
        }
        if (blocks.find(conn.target_block) == blocks.end()) {
            error_message = "Unknown target block: " + conn.target_block;
            is_valid = false;
            return false;
        }
    }

    // Check for isolated blocks (warning only)
    for (const auto& [name, block] : blocks) {
        if (block.in_flowgraph) {
            bool has_connections = false;
            for (const auto& conn : connections) {
                if (conn.source_block == name || conn.target_block == name) {
                    has_connections = true;
                    break;
                }
            }
            if (!has_connections) {
                warnings.push_back("Isolated block: " + name);
            }
        }
    }

    // Check for blocks declared but not in flowgraph
    for (const auto& [name, block] : blocks) {
        if (!block.in_flowgraph) {
            warnings.push_back("Block declared but not used in flowgraph: " + name);
        }
    }

    is_valid = true;
    return true;
}

} // namespace cler
