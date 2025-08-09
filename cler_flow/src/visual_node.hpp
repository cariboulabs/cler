/******************************************************************************************
*                                                                                         *
*    VisualNode - Visual representation of a block instance                              *
*                                                                                         *
*    Pure visualization - no runtime logic                                               *
*                                                                                         *
******************************************************************************************/

#pragma once

#include "block_spec.hpp"
#include <imgui.h>
#include <memory>
#include <string>
#include <map>
#include <unordered_map>

namespace clerflow {

// Visual representation of a port
struct VisualPort {
    std::string name;
    std::string display_name;
    DataType data_type;
    ImVec2 position;  // Relative to node position
    bool is_connected = false;
    
    ImVec2 GetScreenPos(ImVec2 node_pos) const {
        return ImVec2(node_pos.x + position.x, node_pos.y + position.y);
    }
    
    bool ContainsPoint(ImVec2 point) const {
        // Increased hit radius for easier connection (visual is still 6.0f)
        const float hit_radius = 10.0f;  // Was 6.0f, now larger for easier clicking
        float dx = point.x - position.x;
        float dy = point.y - position.y;
        return (dx * dx + dy * dy) <= (hit_radius * hit_radius);
    }
};

class VisualNode {
    
public:
    // Construction
    VisualNode(size_t id, std::shared_ptr<BlockSpec> spec, ImVec2 position);
    
    // Properties
    size_t GetId() const { return id; }
    std::shared_ptr<BlockSpec> GetSpec() const { return spec; }
    
    // Visual state
    ImVec2 position;
    ImVec2 size;
    ImVec2 min_size{100, 60};  // Minimum size for resizing
    bool selected = false;
    bool collapsed = false;
    bool moving = false;
    bool resizing = false;
    
    // Instance configuration
    std::string instance_name;
    std::map<std::string, std::string> template_values;
    std::map<std::string, std::string> param_values;
    
    // Ports (generated from spec)
    std::vector<VisualPort> input_ports;
    std::vector<VisualPort> output_ports;
    
    // Drawing
    void Draw(ImDrawList* draw_list, ImVec2 scroll, float zoom);
    void DrawProperties();  // For property inspector
    
    // Interaction
    bool ContainsPoint(ImVec2 point) const;
    int GetInputPortAt(ImVec2 point) const;
    int GetOutputPortAt(ImVec2 point) const;
    void UpdatePortPositions();
    bool IsInResizeZone(ImVec2 point) const;
    
    
    // Code generation
    std::string GenerateInstantiation() const;
    
private:
    size_t id;
    std::shared_ptr<BlockSpec> spec;
    
    // Simple cache for the most expensive calculation
    mutable float cached_title_width = -1.0f;
    
    // Visual constants
    static constexpr float NODE_WINDOW_PADDING = 4.0f;
    static constexpr float PORT_SIZE = 12.0f;
    static constexpr float PORT_SPACING = 24.0f;
    static constexpr float TITLE_HEIGHT = 24.0f;
    
    // Drawing helpers
    void DrawPorts(ImDrawList* draw_list, ImVec2 node_screen_pos, float zoom);
    void DrawPort(ImDrawList* draw_list, const VisualPort& port, ImVec2 node_screen_pos, 
                  bool is_output, float zoom);
    void DrawPortShape(ImDrawList* draw_list, ImVec2 port_pos, const std::string& data_type, 
                       bool is_connected, float zoom);
    std::string GetAbbreviatedName(const std::string& name) const;
    void DrawTitle(ImDrawList* draw_list, ImVec2 node_screen_pos, ImVec2 node_size);
    void DrawShadow(ImDrawList* draw_list, ImVec2 node_screen_pos, ImVec2 node_size);
    
    // Initialize ports from spec
    void InitializePorts();
};

} // namespace clerflow