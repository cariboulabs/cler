#pragma once

#include <string>
#include <vector>
#include <map>

namespace cler {

struct Block {
    std::string name;
    std::string type;
    std::string template_params;
    std::vector<std::string> constructor_args;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    bool in_flowgraph = false;
    int line = 0;
    int column = 0;

    bool is_source() const;
    bool is_sink() const;
};

struct Connection {
    std::string source_block;
    std::string source_channel;
    std::string target_block;
    std::string target_channel;
    int channel_index = -1;
};

struct FlowGraph {
    std::string name;
    std::map<std::string, Block> blocks;
    std::vector<Connection> connections;

    // Validation state
    bool is_valid = false;
    std::string error_message;
    std::vector<std::string> warnings;

    // Validate flowgraph integrity
    bool validate();
};

} // namespace cler
