/******************************************************************************************
*                                                                                         *
*    VisualNode - Implementation of visual node rendering                                *
*                                                                                         *
******************************************************************************************/

#include "visual_node.hpp"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace clerflow {

VisualNode::VisualNode(size_t id, std::shared_ptr<BlockSpec> spec, ImVec2 position)
    : id(id), spec(spec), position(position)
{
    // Generate default instance name
    instance_name = spec->display_name + "_" + std::to_string(id);
    
    // Initialize default parameter values
    for (const auto& param : spec->template_params) {
        template_values[param.name] = param.default_value;
    }
    for (const auto& param : spec->constructor_params) {
        param_values[param.name] = param.default_value;
    }
    
    // Initialize ports from spec
    InitializePorts();
    
    // Calculate initial size
    UpdatePortPositions();
}

void VisualNode::InitializePorts()
{
    // Create input ports
    input_ports.clear();
    for (const auto& port_spec : spec->input_ports) {
        VisualPort port;
        port.name = port_spec.name;
        port.display_name = port_spec.display_name;
        port.data_type = port_spec.data_type;
        port.is_connected = false;
        input_ports.push_back(port);
    }
    
    // Create output ports
    output_ports.clear();
    for (const auto& port_spec : spec->output_ports) {
        VisualPort port;
        port.name = port_spec.name;
        port.display_name = port_spec.display_name;
        port.data_type = port_spec.data_type;
        port.is_connected = false;
        output_ports.push_back(port);
    }
}

void VisualNode::UpdatePortPositions()
{
    // Only recalculate size if not currently resizing
    if (!resizing) {
        // Calculate node size based on content
        float max_input_width = 0;
        float max_output_width = 0;
        
        for (const auto& port : input_ports) {
            float width = ImGui::CalcTextSize(port.display_name.c_str()).x;
            max_input_width = std::max(max_input_width, width);
        }
        
        for (const auto& port : output_ports) {
            float width = ImGui::CalcTextSize(port.display_name.c_str()).x;
            max_output_width = std::max(max_output_width, width);
        }
        
        // Base size (before rotation)
        float title_width = ImGui::CalcTextSize(spec->display_name.c_str()).x;
        float content_width = max_input_width + max_output_width + 60; // Padding
        float base_width = std::max({150.0f, title_width + 40, content_width});
        float port_count = std::max(input_ports.size(), output_ports.size());
        float base_height = TITLE_HEIGHT + (port_count * PORT_SPACING) + NODE_WINDOW_PADDING * 2;
        
        // Apply rotation to size
        if (rotation == 90 || rotation == 270) {
            size.x = base_height;
            size.y = base_width;
        } else {
            size.x = base_width;
            size.y = base_height;
        }
        
        // Update min_size to current calculated size
        min_size = ImVec2(base_width * 0.8f, base_height * 0.8f);  // Allow shrinking to 80% of calculated size
    }
    
    // Position ports based on rotation
    float y_offset = TITLE_HEIGHT + NODE_WINDOW_PADDING;
    
    switch (rotation) {
        case 0:  // Normal orientation
            for (auto& port : input_ports) {
                port.position = ImVec2(0, y_offset);
                y_offset += PORT_SPACING;
            }
            y_offset = TITLE_HEIGHT + NODE_WINDOW_PADDING;
            for (auto& port : output_ports) {
                port.position = ImVec2(size.x, y_offset);
                y_offset += PORT_SPACING;
            }
            break;
            
        case 90:  // Rotated right - inputs on top, outputs on bottom
            y_offset = NODE_WINDOW_PADDING;
            for (auto& port : input_ports) {
                port.position = ImVec2(y_offset, 0);
                y_offset += PORT_SPACING;
            }
            y_offset = NODE_WINDOW_PADDING;
            for (auto& port : output_ports) {
                port.position = ImVec2(y_offset, size.y);
                y_offset += PORT_SPACING;
            }
            break;
            
        case 180:  // Upside down - inputs on right, outputs on left
            for (auto& port : input_ports) {
                port.position = ImVec2(size.x, y_offset);
                y_offset += PORT_SPACING;
            }
            y_offset = TITLE_HEIGHT + NODE_WINDOW_PADDING;
            for (auto& port : output_ports) {
                port.position = ImVec2(0, y_offset);
                y_offset += PORT_SPACING;
            }
            break;
            
        case 270:  // Rotated left - inputs on bottom, outputs on top
            y_offset = NODE_WINDOW_PADDING;
            for (auto& port : input_ports) {
                port.position = ImVec2(y_offset, size.y);
                y_offset += PORT_SPACING;
            }
            y_offset = NODE_WINDOW_PADDING;
            for (auto& port : output_ports) {
                port.position = ImVec2(y_offset, 0);
                y_offset += PORT_SPACING;
            }
            break;
    }
}

void VisualNode::Draw(ImDrawList* draw_list, ImVec2 scroll, float zoom)
{
    // Calculate screen position
    ImVec2 canvas_pos = ImGui::GetWindowPos();
    canvas_pos.x += ImGui::GetWindowContentRegionMin().x;
    canvas_pos.y += ImGui::GetWindowContentRegionMin().y;
    
    ImVec2 node_rect_min = ImVec2(canvas_pos.x + position.x * zoom + scroll.x, 
                                   canvas_pos.y + position.y * zoom + scroll.y);
    ImVec2 node_rect_max = ImVec2(node_rect_min.x + size.x * zoom,
                                   node_rect_min.y + size.y * zoom);
    
    // Draw shadow
    DrawShadow(draw_list, node_rect_min, ImVec2(size.x * zoom, size.y * zoom));
    
    // Node background
    ImU32 node_bg_color = selected ? IM_COL32(75, 75, 150, 255) : IM_COL32(50, 50, 50, 255);
    draw_list->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 4.0f);
    
    // Node border
    ImU32 node_border_color = selected ? IM_COL32(255, 200, 100, 255) : IM_COL32(100, 100, 100, 255);
    draw_list->AddRect(node_rect_min, node_rect_max, node_border_color, 4.0f, 0, 2.0f);
    
    // Title bar
    DrawTitle(draw_list, node_rect_min, ImVec2(size.x * zoom, TITLE_HEIGHT * zoom));
    
    // Draw ports
    DrawPorts(draw_list, node_rect_min, zoom);
    
    // Check if mouse is hovering over resize zone for visual feedback
    ImVec2 mouse_pos = ImGui::GetMousePos();
    ImVec2 canvas_mouse = ImVec2(
        (mouse_pos.x - canvas_pos.x - scroll.x) / zoom,
        (mouse_pos.y - canvas_pos.y - scroll.y) / zoom
    );
    bool hovering_resize = IsInResizeZone(canvas_mouse);
    
    // Draw resize handle (grip lines in corner)
    const float grip_size = 15.0f * zoom;
    const float grip_thickness = 2.0f * zoom;
    ImU32 resize_color = resizing ? IM_COL32(255, 200, 100, 255) : 
                        (hovering_resize ? IM_COL32(200, 200, 100, 255) :
                        (selected ? IM_COL32(150, 150, 150, 200) : IM_COL32(100, 100, 100, 150)));
    
    // Draw three diagonal lines in corner like a standard resize grip
    for (int i = 0; i < 3; ++i) {
        float offset = i * 4.0f * zoom;
        draw_list->AddLine(
            ImVec2(node_rect_max.x - grip_size + offset, node_rect_max.y),
            ImVec2(node_rect_max.x, node_rect_max.y - grip_size + offset),
            resize_color, grip_thickness
        );
    }
}

void VisualNode::DrawTitle(ImDrawList* draw_list, ImVec2 node_screen_pos, ImVec2 title_size)
{
    // Title background
    ImVec2 title_rect_max = ImVec2(node_screen_pos.x + title_size.x, 
                                    node_screen_pos.y + title_size.y);
    
    ImU32 title_color = spec->is_source ? IM_COL32(60, 100, 60, 255) :
                       spec->is_sink ? IM_COL32(100, 60, 60, 255) :
                       IM_COL32(60, 60, 100, 255);
    
    draw_list->AddRectFilled(node_screen_pos, title_rect_max, title_color, 4.0f, ImDrawFlags_RoundCornersTop);
    
    // Title text with shadow (like core-nodes)
    ImVec2 text_pos = ImVec2(node_screen_pos.x + 10, node_screen_pos.y + 4);
    // Draw shadow first
    draw_list->AddText(ImVec2(text_pos.x + 1, text_pos.y + 1), 
                      IM_COL32(0, 0, 0, 180), spec->display_name.c_str());
    // Draw actual text
    draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), spec->display_name.c_str());
    
    // Category badge (if not default)
    if (!spec->category.empty() && spec->category != "General") {
        ImVec2 badge_size = ImGui::CalcTextSize(spec->category.c_str());
        ImVec2 badge_pos = ImVec2(title_rect_max.x - badge_size.x - 10, text_pos.y);
        draw_list->AddText(badge_pos, IM_COL32(150, 150, 150, 200), spec->category.c_str());
    }
}

void VisualNode::DrawShadow(ImDrawList* draw_list, ImVec2 node_screen_pos, ImVec2 node_size)
{
    // Simple shadow effect
    const float shadow_offset = 4.0f;
    const float shadow_alpha = 0.3f;
    
    ImVec2 shadow_min = ImVec2(node_screen_pos.x + shadow_offset, 
                                node_screen_pos.y + shadow_offset);
    ImVec2 shadow_max = ImVec2(shadow_min.x + node_size.x, 
                                shadow_min.y + node_size.y);
    
    draw_list->AddRectFilled(shadow_min, shadow_max, 
                             IM_COL32(0, 0, 0, (int)(255 * shadow_alpha)), 4.0f);
}

void VisualNode::DrawPorts(ImDrawList* draw_list, ImVec2 node_screen_pos, float zoom)
{
    // Draw input ports
    for (const auto& port : input_ports) {
        DrawPort(draw_list, port, node_screen_pos, false, zoom);
    }
    
    // Draw output ports
    for (const auto& port : output_ports) {
        DrawPort(draw_list, port, node_screen_pos, true, zoom);
    }
}

void VisualNode::DrawPort(ImDrawList* draw_list, const VisualPort& port, 
                          ImVec2 node_screen_pos, bool is_output, float zoom)
{
    ImVec2 port_pos = ImVec2(node_screen_pos.x + port.position.x * zoom,
                             node_screen_pos.y + port.position.y * zoom);
    
    // Port circle
    float port_radius = (PORT_SIZE / 2.0f) * zoom;
    ImU32 port_color = dataTypeToColor(port.data_type);
    
    if (port.is_connected) {
        draw_list->AddCircleFilled(port_pos, port_radius, port_color);
    } else {
        draw_list->AddCircle(port_pos, port_radius, port_color, 12, 2.0f);
    }
    
    // Port label
    ImVec2 text_pos;
    if (is_output) {
        ImVec2 text_size = ImGui::CalcTextSize(port.display_name.c_str());
        text_pos = ImVec2(port_pos.x - text_size.x - 10 * zoom, port_pos.y - text_size.y / 2);
    } else {
        text_pos = ImVec2(port_pos.x + 10 * zoom, 
                         port_pos.y - ImGui::GetTextLineHeight() / 2);
    }
    
    draw_list->AddText(text_pos, IM_COL32(200, 200, 200, 255), port.display_name.c_str());
}

bool VisualNode::ContainsPoint(ImVec2 point) const
{
    return point.x >= position.x && point.x <= position.x + size.x &&
           point.y >= position.y && point.y <= position.y + size.y;
}

bool VisualNode::IsInResizeZone(ImVec2 point) const
{
    const float resize_zone = 20.0f;  // Make it bigger for easier grabbing
    return point.x >= position.x + size.x - resize_zone && 
           point.x <= position.x + size.x + 5.0f &&  // Allow slight overshoot
           point.y >= position.y + size.y - resize_zone && 
           point.y <= position.y + size.y + 5.0f;  // Allow slight overshoot
}

int VisualNode::GetInputPortAt(ImVec2 point) const
{
    ImVec2 local_point = ImVec2(point.x - position.x, point.y - position.y);
    
    for (size_t i = 0; i < input_ports.size(); ++i) {
        if (input_ports[i].ContainsPoint(local_point)) {
            return static_cast<int>(i);
        }
    }
    
    return -1;
}

int VisualNode::GetOutputPortAt(ImVec2 point) const
{
    ImVec2 local_point = ImVec2(point.x - position.x, point.y - position.y);
    
    for (size_t i = 0; i < output_ports.size(); ++i) {
        if (output_ports[i].ContainsPoint(local_point)) {
            return static_cast<int>(i);
        }
    }
    
    return -1;
}

void VisualNode::DrawProperties()
{
    ImGui::Text("Instance: %s", instance_name.c_str());
    ImGui::Separator();
    
    // Template parameters
    if (!spec->template_params.empty()) {
        ImGui::Text("Template Parameters:");
        for (auto& param : spec->template_params) {
            std::string& value = template_values[param.name];
            
            if (param.type == ParamType::DataTypeSelector) {
                if (ImGui::BeginCombo(param.display_name.c_str(), value.c_str())) {
                    for (const auto& option : param.options) {
                        bool is_selected = (value == option);
                        if (ImGui::Selectable(option.c_str(), is_selected)) {
                            value = option;
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
    }
    
    // Constructor parameters
    if (!spec->constructor_params.empty()) {
        ImGui::Text("Parameters:");
        for (auto& param : spec->constructor_params) {
            std::string& value = param_values[param.name];
            
            switch (param.type) {
                case ParamType::String:
                    ImGui::InputText(param.display_name.c_str(), 
                                    const_cast<char*>(value.c_str()), 256);
                    break;
                    
                case ParamType::Int: {
                    int int_val = std::stoi(value);
                    if (ImGui::InputInt(param.display_name.c_str(), &int_val)) {
                        value = std::to_string(int_val);
                    }
                    break;
                }
                
                case ParamType::Float: {
                    float float_val = std::stof(value);
                    if (ImGui::InputFloat(param.display_name.c_str(), &float_val)) {
                        value = std::to_string(float_val);
                    }
                    break;
                }
                
                case ParamType::Bool: {
                    bool bool_val = (value == "true");
                    if (ImGui::Checkbox(param.display_name.c_str(), &bool_val)) {
                        value = bool_val ? "true" : "false";
                    }
                    break;
                }
                
                default:
                    ImGui::Text("%s: %s", param.display_name.c_str(), value.c_str());
                    break;
            }
            
            if (ImGui::IsItemHovered() && !param.tooltip.empty()) {
                ImGui::SetTooltip("%s", param.tooltip.c_str());
            }
        }
    }
}

std::string VisualNode::GenerateInstantiation() const
{
    std::vector<std::string> template_args;
    for (const auto& param : spec->template_params) {
        template_args.push_back(template_values.at(param.name));
    }
    
    std::vector<std::string> constructor_args;
    for (const auto& param : spec->constructor_params) {
        constructor_args.push_back(param_values.at(param.name));
    }
    
    return spec->generateInstantiation(instance_name, template_args, constructor_args);
}

} // namespace clerflow