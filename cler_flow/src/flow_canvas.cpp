/******************************************************************************************
*                                                                                         *
*    FlowCanvas - Implementation focused on CLER flowgraph generation                    *
*                                                                                         *
******************************************************************************************/

#include "flow_canvas.hpp"
#include <imgui_internal.h>  // For ImBezierCubicCalc
#include <algorithm>
#include <sstream>
#include <cmath>
#include <set>
#include <cstdio>  // For printf debugging

namespace clerflow {

FlowCanvas::FlowCanvas()
{
    // Initialize with reasonable defaults
    scrolling = ImVec2(100, 100);
    zoom = 1.0f;
}

void FlowCanvas::Draw()
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    
    // Store canvas position for conversions - must be before any uses
    canvas_screen_pos = canvas_pos;
    
    // Create invisible button for interaction
    ImGui::InvisibleButton("canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft | 
                           ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool is_hovered = ImGui::IsItemHovered();
    bool is_active = ImGui::IsItemActive();
    
    // Handle drag and drop from library - MUST be right after InvisibleButton
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BLOCK_SPEC")) {
            // Get the block spec pointer
            std::shared_ptr<BlockSpec>* block_ptr = (std::shared_ptr<BlockSpec>*)payload->Data;
            if (block_ptr && *block_ptr) {
                // Convert mouse position to canvas coordinates using stored canvas_pos
                ImVec2 mouse_pos = ImGui::GetMousePos();
                ImVec2 canvas_drop_pos = ImVec2(
                    (mouse_pos.x - canvas_pos.x - scrolling.x) / zoom,
                    (mouse_pos.y - canvas_pos.y - scrolling.y) / zoom
                );
                
                // Add the node at the drop position
                size_t new_node_id = AddNode(*block_ptr, canvas_drop_pos);
                
            }
        }
        ImGui::EndDragDropTarget();
    }
    
    // Visual feedback when dragging over canvas
    if (ImGui::IsWindowHovered() && ImGui::GetDragDropPayload()) {
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload()) {
            if (payload->IsDataType("BLOCK_SPEC")) {
                // Show drop preview
                ImVec2 mouse_pos = ImGui::GetMousePos();
                draw_list->AddRectFilled(
                    ImVec2(mouse_pos.x - 50, mouse_pos.y - 20),
                    ImVec2(mouse_pos.x + 50, mouse_pos.y + 20),
                    IM_COL32(100, 200, 100, 100), 4.0f
                );
                draw_list->AddText(ImVec2(mouse_pos.x - 40, mouse_pos.y - 8), 
                                  IM_COL32(255, 255, 255, 200), "Drop here");
            }
        }
    }
    
    // Clip drawing to canvas
    draw_list->PushClipRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, 
                            canvas_pos.y + canvas_size.y), true);
    
    // Draw grid
    DrawGrid();
    
    // Handle input
    if (is_hovered) {
        HandleInput();
    }
    
    // Draw connections first (behind nodes)
    DrawConnections();
    
    // Draw nodes
    DrawNodes();
    
    // Draw ongoing connection preview
    if (isConnecting) {
        DrawConnectionPreview();
    }
    
    // Draw selection box
    if (isBoxSelecting) {
        ImVec2 box_min = CanvasToScreen(boxSelectStart);
        ImVec2 box_max = ImGui::GetMousePos();
        draw_list->AddRectFilled(box_min, box_max, IM_COL32(100, 100, 255, 30));
        draw_list->AddRect(box_min, box_max, IM_COL32(100, 100, 255, 100));
    }
    
    // Context menus
    HandleContextMenus();
    
    draw_list->PopClipRect();
}

void FlowCanvas::DrawGrid()
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    
    const float grid_size = 32.0f * zoom;
    const ImU32 grid_color = IM_COL32(50, 50, 50, 40);
    const ImU32 grid_color_thick = IM_COL32(80, 80, 80, 80);
    
    // Vertical lines
    for (float x = fmodf(scrolling.x, grid_size); x < canvas_size.x; x += grid_size) {
        bool is_thick = (int)(x / grid_size) % 4 == 0;
        draw_list->AddLine(ImVec2(canvas_pos.x + x, canvas_pos.y),
                          ImVec2(canvas_pos.x + x, canvas_pos.y + canvas_size.y),
                          is_thick ? grid_color_thick : grid_color);
    }
    
    // Horizontal lines
    for (float y = fmodf(scrolling.y, grid_size); y < canvas_size.y; y += grid_size) {
        bool is_thick = (int)(y / grid_size) % 4 == 0;
        draw_list->AddLine(ImVec2(canvas_pos.x, canvas_pos.y + y),
                          ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + y),
                          is_thick ? grid_color_thick : grid_color);
    }
}

void FlowCanvas::DrawNodes()
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Draw unselected nodes first
    for (auto& [id, node] : nodes) {
        if (!node->selected) {
            node->Draw(draw_list, scrolling, zoom);
        }
    }
    
    // Draw selected nodes on top
    for (auto& [id, node] : nodes) {
        if (node->selected) {
            node->Draw(draw_list, scrolling, zoom);
        }
    }
}

void FlowCanvas::DrawConnections()
{
    for (const auto& conn : connections) {
        DrawConnection(conn);
    }
}

void FlowCanvas::DrawConnection(const Connection& conn)
{
    auto* from_node = GetNode(conn.from_node_id);
    auto* to_node = GetNode(conn.to_node_id);
    
    if (!from_node || !to_node) return;
    if (conn.from_port_index >= from_node->output_ports.size()) return;
    if (conn.to_port_index >= to_node->input_ports.size()) return;
    
    ImVec2 p1 = from_node->output_ports[conn.from_port_index].GetScreenPos(from_node->position);
    ImVec2 p2 = to_node->input_ports[conn.to_port_index].GetScreenPos(to_node->position);
    
    p1 = CanvasToScreen(p1);
    p2 = CanvasToScreen(p2);
    
    DrawBezierCurve(p1, p2, dataTypeToColor(conn.data_type));
}

void FlowCanvas::DrawConnectionPreview()
{
    if (!isConnecting) return;
    
    auto* from_node = GetNode(connectingFromNode);
    if (!from_node) return;
    
    ImVec2 p1, p2;
    DataType type = DataType::Float;
    
    if (connectingFromOutput) {
        if (connectingFromPort >= from_node->output_ports.size()) return;
        p1 = from_node->output_ports[connectingFromPort].GetScreenPos(from_node->position);
        p1 = CanvasToScreen(p1);
        p2 = ImGui::GetMousePos();
        type = from_node->output_ports[connectingFromPort].data_type;
    } else {
        if (connectingFromPort >= from_node->input_ports.size()) return;
        p1 = ImGui::GetMousePos();
        p2 = from_node->input_ports[connectingFromPort].GetScreenPos(from_node->position);
        p2 = CanvasToScreen(p2);
        type = from_node->input_ports[connectingFromPort].data_type;
    }
    
    DrawBezierCurve(p1, p2, dataTypeToColor(type), 2.0f);
}

void FlowCanvas::DrawBezierCurve(ImVec2 p1, ImVec2 p2, ImU32 color, float thickness)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Sophisticated routing like core-nodes
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    // Dynamic handle distance based on connection distance
    float handle_distance = std::min(200.0f, std::max(50.0f, distance * 0.4f));
    
    ImVec2 cp1, cp2;
    
    if (dx > 20.0f) {
        // Normal left-to-right connection
        // Adjust handle based on vertical distance for smoother curves
        float vertical_factor = std::min(1.0f, std::abs(dy) / 200.0f);
        float adjusted_handle = handle_distance * (1.0f + vertical_factor * 0.5f);
        
        cp1 = ImVec2(p1.x + adjusted_handle, p1.y);
        cp2 = ImVec2(p2.x - adjusted_handle, p2.y);
    } else if (dx > -20.0f) {
        // Nearly vertical connection
        float offset = 40.0f + std::abs(dy) * 0.2f;
        if (p1.y < p2.y) {
            // Downward - curve to the right
            cp1 = ImVec2(p1.x + offset, p1.y + offset);
            cp2 = ImVec2(p2.x - offset, p2.y - offset);
        } else {
            // Upward - curve to the left  
            cp1 = ImVec2(p1.x + offset, p1.y - offset);
            cp2 = ImVec2(p2.x - offset, p2.y + offset);
        }
    } else {
        // Right-to-left (inverted) - sophisticated loop routing
        float loop_size = std::max(60.0f, std::min(300.0f, (std::abs(dx) + std::abs(dy)) * 0.3f));
        
        // Create S-curve for inverted connections
        if (std::abs(dy) < 50.0f) {
            // Horizontal S-curve
            cp1 = ImVec2(p1.x + loop_size, p1.y);
            cp2 = ImVec2(p2.x - loop_size, p2.y);
        } else {
            // Vertical component - create smoother loop
            float y_offset = dy * 0.25f;
            cp1 = ImVec2(p1.x + loop_size, p1.y + y_offset);
            cp2 = ImVec2(p2.x - loop_size, p2.y - y_offset);
        }
    }
    
    // Draw shadow/outline for better visibility
    if (thickness > 1.5f) {
        draw_list->AddBezierCubic(p1, cp1, cp2, p2, 
                                  IM_COL32(0, 0, 0, 80), (thickness + 2.0f) * zoom);
    }
    
    // Draw main connection line
    draw_list->AddBezierCubic(p1, cp1, cp2, p2, color, thickness * zoom);
    
    // Optional: Add flow direction indicator (small arrow)
    if (distance > 100.0f && zoom > 0.7f) {
        // Calculate midpoint on bezier curve
        float t = 0.5f;
        ImVec2 mid = ImBezierCubicCalc(p1, cp1, cp2, p2, t);
        
        // Calculate tangent at midpoint for arrow direction
        ImVec2 tangent = ImVec2(
            3.0f * (1-t)*(1-t) * (cp1.x - p1.x) + 
            6.0f * (1-t)*t * (cp2.x - cp1.x) + 
            3.0f * t*t * (p2.x - cp2.x),
            3.0f * (1-t)*(1-t) * (cp1.y - p1.y) + 
            6.0f * (1-t)*t * (cp2.y - cp1.y) + 
            3.0f * t*t * (p2.y - cp2.y)
        );
        
        // Normalize tangent
        float len = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
        if (len > 0.01f) {
            tangent.x /= len;
            tangent.y /= len;
            
            // Draw small arrow
            float arrow_size = 6.0f * zoom;
            ImVec2 arrow_p1 = ImVec2(mid.x - tangent.x * arrow_size - tangent.y * arrow_size * 0.5f,
                                     mid.y - tangent.y * arrow_size + tangent.x * arrow_size * 0.5f);
            ImVec2 arrow_p2 = ImVec2(mid.x - tangent.x * arrow_size + tangent.y * arrow_size * 0.5f,
                                     mid.y - tangent.y * arrow_size - tangent.x * arrow_size * 0.5f);
            
            draw_list->AddTriangleFilled(mid, arrow_p1, arrow_p2, color);
        }
    }
}

void FlowCanvas::HandleInput()
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos = ImGui::GetMousePos();
    ImVec2 canvas_mouse = ScreenToCanvas(mouse_pos);
    
    // Check if hovering over resize zone and change cursor
    bool hovering_resize = false;
    for (auto& [id, node] : nodes) {
        if (node->IsInResizeZone(canvas_mouse)) {
            hovering_resize = true;
            break;
        }
    }
    
    if (hovering_resize) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
    }
    
    // Pan with middle mouse
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        scrolling.x += io.MouseDelta.x;
        scrolling.y += io.MouseDelta.y;
    }
    
    // Zoom with mouse wheel
    if (io.MouseWheel != 0) {
        float zoom_delta = io.MouseWheel * 0.1f;
        float new_zoom = zoom + zoom_delta;
        new_zoom = std::max(0.3f, std::min(3.0f, new_zoom));
        
        // Zoom towards mouse position
        ImVec2 mouse_before = ScreenToCanvas(mouse_pos);
        zoom = new_zoom;
        ImVec2 mouse_after = ScreenToCanvas(mouse_pos);
        
        scrolling.x += (mouse_after.x - mouse_before.x) * zoom;
        scrolling.y += (mouse_after.y - mouse_before.y) * zoom;
    }
    
    // Node interaction
    HandleNodeInteraction();
    
    // Canvas interaction
    HandleCanvasInteraction();
}

void FlowCanvas::HandleNodeInteraction()
{
    ImVec2 mouse_pos = ImGui::GetMousePos();
    ImVec2 canvas_mouse = ScreenToCanvas(mouse_pos);
    
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Check ports first
        for (auto& [id, node] : nodes) {
            // Check output ports
            int output_port = node->GetOutputPortAt(canvas_mouse);
            if (output_port >= 0) {
                isConnecting = true;
                connectingFromNode = id;
                connectingFromPort = output_port;
                connectingFromOutput = true;
                return;
            }
            
            // Check input ports
            int input_port = node->GetInputPortAt(canvas_mouse);
            if (input_port >= 0) {
                // Disconnect existing connection
                for (auto it = connections.begin(); it != connections.end(); ) {
                    if (it->to_node_id == id && it->to_port_index == input_port) {
                        it = connections.erase(it);
                    } else {
                        ++it;
                    }
                }
                
                isConnecting = true;
                connectingFromNode = id;
                connectingFromPort = input_port;
                connectingFromOutput = false;
                return;
            }
        }
        
        // Check for resize handles first
        bool found = false;
        for (auto& [id, node] : nodes) {
            if (node->IsInResizeZone(canvas_mouse)) {
                isResizingNode = true;
                resizingNodeId = id;
                node->resizing = true;
                found = true;
                break;
            }
        }
        
        // Check node selection if not resizing
        if (!found) {
            for (auto& [id, node] : nodes) {
                if (node->ContainsPoint(canvas_mouse)) {
                    SelectNode(id, ImGui::GetIO().KeyShift);
                    found = true;
                    isDraggingNode = true;  // Start dragging immediately when clicking on a node
                    break;
                }
            }
        }
        
        // Start box selection if clicking empty space
        if (!found) {
            if (!ImGui::GetIO().KeyShift) {
                ClearSelection();
            }
            isBoxSelecting = true;
            boxSelectStart = canvas_mouse;
        }
    }
    
    // Finish connection
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && isConnecting) {
        for (auto& [id, node] : nodes) {
            if (connectingFromOutput) {
                int input_port = node->GetInputPortAt(canvas_mouse);
                if (input_port >= 0 && id != connectingFromNode) {
                    AddConnection(connectingFromNode, connectingFromPort, id, input_port);
                }
            } else {
                int output_port = node->GetOutputPortAt(canvas_mouse);
                if (output_port >= 0 && id != connectingFromNode) {
                    AddConnection(id, output_port, connectingFromNode, connectingFromPort);
                }
            }
        }
        isConnecting = false;
    }
    
    // Resize node
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && isResizingNode) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        delta.x /= zoom;
        delta.y /= zoom;
        
        auto* node = GetNode(resizingNodeId);
        if (node) {
            node->size.x = std::max(node->min_size.x, node->size.x + delta.x);
            node->size.y = std::max(node->min_size.y, node->size.y + delta.y);
            node->UpdatePortPositions();
        }
    }
    
    // Drag selected nodes
    else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && isDraggingNode && !isConnecting && !isBoxSelecting && !isResizingNode) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        delta.x /= zoom;
        delta.y /= zoom;
        
        for (size_t id : selectedNodes) {
            auto* node = GetNode(id);
            if (node) {
                node->position.x += delta.x;
                node->position.y += delta.y;
            }
        }
    }
    
    // Reset flags on mouse release
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        isDraggingNode = false;
        
        if (isResizingNode) {
            isResizingNode = false;
            auto* node = GetNode(resizingNodeId);
            if (node) {
                node->resizing = false;
            }
            resizingNodeId = 0;
        }
    }
    
    // Finish box selection
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && isBoxSelecting) {
        ImVec2 box_min = ImVec2(std::min(boxSelectStart.x, canvas_mouse.x),
                               std::min(boxSelectStart.y, canvas_mouse.y));
        ImVec2 box_max = ImVec2(std::max(boxSelectStart.x, canvas_mouse.x),
                               std::max(boxSelectStart.y, canvas_mouse.y));
        
        for (auto& [id, node] : nodes) {
            if (node->position.x >= box_min.x && node->position.x + node->size.x <= box_max.x &&
                node->position.y >= box_min.y && node->position.y + node->size.y <= box_max.y) {
                SelectNode(id, true);
            }
        }
        
        isBoxSelecting = false;
    }
}

void FlowCanvas::HandleCanvasInteraction()
{
    // Delete selected nodes
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        for (size_t id : selectedNodes) {
            RemoveNode(id);
        }
        selectedNodes.clear();
    }
    
    // Select all
    if (ImGui::IsKeyPressed(ImGuiKey_A) && ImGui::GetIO().KeyCtrl) {
        for (auto& [id, node] : nodes) {
            SelectNode(id, true);
        }
    }
    
    // Rotate selected nodes
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        if (ImGui::GetIO().KeyShift) {
            // Rotate left (counter-clockwise)
            for (size_t id : selectedNodes) {
                auto* node = GetNode(id);
                if (node) {
                    node->RotateLeft();
                }
            }
        } else {
            // Rotate right (clockwise)
            for (size_t id : selectedNodes) {
                auto* node = GetNode(id);
                if (node) {
                    node->RotateRight();
                }
            }
        }
    }
}

void FlowCanvas::HandleContextMenus()
{
    // Check for right-click on nodes
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        ImVec2 canvas_mouse = ScreenToCanvas(mouse_pos);
        
        // Check if right-clicking on a node
        bool found_node = false;
        for (auto& [id, node] : nodes) {
            if (node->ContainsPoint(canvas_mouse)) {
                contextNodeId = id;
                ImGui::OpenPopup("node_context");
                found_node = true;
                break;
            }
        }
        
        // If not on a node, open canvas context menu
        if (!found_node) {
            contextMenuPos = canvas_mouse;
            ImGui::OpenPopup("canvas_context");
        }
    }
    
    // Node context menu
    if (ImGui::BeginPopup("node_context")) {
        ShowNodeContextMenu(contextNodeId);
        ImGui::EndPopup();
    }
    
    // Canvas context menu
    if (ImGui::BeginPopup("canvas_context")) {
        ShowCanvasContextMenu();
        ImGui::EndPopup();
    }
}

void FlowCanvas::ShowNodeContextMenu(size_t node_id)
{
    auto* node = GetNode(node_id);
    if (!node) return;
    
    ImGui::Text("Node: %s", node->instance_name.c_str());
    ImGui::Separator();
    
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
        // Duplicate node
        ImVec2 new_pos = ImVec2(node->position.x + 20, node->position.y + 20);
        size_t new_id = AddNode(node->GetSpec(), new_pos);
        auto* new_node = GetNode(new_id);
        if (new_node) {
            new_node->rotation = node->rotation;
            new_node->size = node->size;
            new_node->UpdatePortPositions();
        }
    }
    
    if (ImGui::MenuItem("Delete", "Delete")) {
        RemoveNode(node_id);
    }
    
    ImGui::Separator();
    
    if (ImGui::BeginMenu("Rotate")) {
        if (ImGui::MenuItem("Rotate Right (90°)", "R")) {
            node->RotateRight();
        }
        if (ImGui::MenuItem("Rotate Left (90°)", "Shift+R")) {
            node->RotateLeft();
        }
        if (ImGui::MenuItem("Rotate 180°")) {
            node->RotateRight();
            node->RotateRight();
        }
        if (ImGui::MenuItem("Reset Rotation")) {
            node->rotation = 0;
            node->UpdatePortPositions();
        }
        ImGui::EndMenu();
    }
    
    if (ImGui::BeginMenu("Alignment")) {
        if (ImGui::MenuItem("Align Left")) {
            // Find leftmost selected node
            float min_x = node->position.x;
            for (size_t id : selectedNodes) {
                auto* n = GetNode(id);
                if (n && n->position.x < min_x) {
                    min_x = n->position.x;
                }
            }
            // Align all selected to that position
            for (size_t id : selectedNodes) {
                auto* n = GetNode(id);
                if (n) n->position.x = min_x;
            }
        }
        if (ImGui::MenuItem("Align Right")) {
            float max_x = node->position.x + node->size.x;
            for (size_t id : selectedNodes) {
                auto* n = GetNode(id);
                if (n && n->position.x + n->size.x > max_x) {
                    max_x = n->position.x + n->size.x;
                }
            }
            for (size_t id : selectedNodes) {
                auto* n = GetNode(id);
                if (n) n->position.x = max_x - n->size.x;
            }
        }
        if (ImGui::MenuItem("Align Top")) {
            float min_y = node->position.y;
            for (size_t id : selectedNodes) {
                auto* n = GetNode(id);
                if (n && n->position.y < min_y) {
                    min_y = n->position.y;
                }
            }
            for (size_t id : selectedNodes) {
                auto* n = GetNode(id);
                if (n) n->position.y = min_y;
            }
        }
        if (ImGui::MenuItem("Align Bottom")) {
            float max_y = node->position.y + node->size.y;
            for (size_t id : selectedNodes) {
                auto* n = GetNode(id);
                if (n && n->position.y + n->size.y > max_y) {
                    max_y = n->position.y + n->size.y;
                }
            }
            for (size_t id : selectedNodes) {
                auto* n = GetNode(id);
                if (n) n->position.y = max_y - n->size.y;
            }
        }
        ImGui::EndMenu();
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("Disconnect All")) {
        // Remove all connections to/from this node
        connections.erase(
            std::remove_if(connections.begin(), connections.end(),
                [node_id](const Connection& c) {
                    return c.from_node_id == node_id || c.to_node_id == node_id;
                }),
            connections.end()
        );
    }
    
    if (ImGui::MenuItem("Reset Size")) {
        node->UpdatePortPositions();  // This recalculates default size
    }
}

void FlowCanvas::ShowCanvasContextMenu()
{
    if (ImGui::MenuItem("Add Node...")) {
        // TODO: Show node picker
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, false)) {
        // TODO: Implement clipboard
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("Select All", "Ctrl+A")) {
        for (auto& [id, node] : nodes) {
            SelectNode(id, true);
        }
    }
    
    if (ImGui::MenuItem("Clear All")) {
        ClearAll();
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("Organize Layout")) {
        // TODO: Auto-layout algorithm
    }
    
    if (ImGui::MenuItem("Reset View")) {
        scrolling = ImVec2(100, 100);
        zoom = 1.0f;
    }
}

size_t FlowCanvas::AddNode(std::shared_ptr<BlockSpec> spec, ImVec2 position)
{
    size_t id = nextNodeId++;
    nodes[id] = std::make_unique<VisualNode>(id, spec, position);
    return id;
}

void FlowCanvas::RemoveNode(size_t node_id)
{
    // Remove connections
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [node_id](const Connection& c) {
                return c.from_node_id == node_id || c.to_node_id == node_id;
            }),
        connections.end()
    );
    
    // Remove node
    nodes.erase(node_id);
    
    // Remove from selection
    selectedNodes.erase(
        std::remove(selectedNodes.begin(), selectedNodes.end(), node_id),
        selectedNodes.end()
    );
}

void FlowCanvas::ClearAll()
{
    nodes.clear();
    connections.clear();
    selectedNodes.clear();
    nextNodeId = 1;
}

bool FlowCanvas::CanConnect(size_t from_node, size_t from_port, 
                           size_t to_node, size_t to_port) const
{
    auto* from = nodes.find(from_node)->second.get();
    auto* to = nodes.find(to_node)->second.get();
    
    if (!from || !to) return false;
    if (from_port >= from->output_ports.size()) return false;
    if (to_port >= to->input_ports.size()) return false;
    
    // Check data type compatibility (for CLER, we need strict type matching)
    DataType from_type = from->output_ports[from_port].data_type;
    DataType to_type = to->input_ports[to_port].data_type;
    
    return from_type == to_type;
}

void FlowCanvas::AddConnection(size_t from_node, size_t from_port,
                              size_t to_node, size_t to_port)
{
    if (!CanConnect(from_node, from_port, to_node, to_port)) return;
    
    // Remove existing connection to input port (CLER inputs are single-connection)
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [to_node, to_port](const Connection& c) {
                return c.to_node_id == to_node && c.to_port_index == to_port;
            }),
        connections.end()
    );
    
    // Add new connection
    Connection conn;
    conn.from_node_id = from_node;
    conn.from_port_index = from_port;
    conn.to_node_id = to_node;
    conn.to_port_index = to_port;
    
    auto* from = GetNode(from_node);
    if (from && from_port < from->output_ports.size()) {
        conn.data_type = from->output_ports[from_port].data_type;
    }
    
    connections.push_back(conn);
}

void FlowCanvas::SelectNode(size_t node_id, bool add_to_selection)
{
    if (!add_to_selection) {
        ClearSelection();
    }
    
    auto* node = GetNode(node_id);
    if (node) {
        node->selected = true;
        selectedNodes.push_back(node_id);
    }
}

void FlowCanvas::ClearSelection()
{
    for (auto& [id, node] : nodes) {
        node->selected = false;
    }
    selectedNodes.clear();
}

VisualNode* FlowCanvas::GetNode(size_t id)
{
    auto it = nodes.find(id);
    return it != nodes.end() ? it->second.get() : nullptr;
}

ImVec2 FlowCanvas::ScreenToCanvas(ImVec2 pos) const
{
    // Use cached canvas_screen_pos for consistent conversion
    return ImVec2((pos.x - canvas_screen_pos.x - scrolling.x) / zoom,
                  (pos.y - canvas_screen_pos.y - scrolling.y) / zoom);
}

ImVec2 FlowCanvas::CanvasToScreen(ImVec2 pos) const
{
    // Use cached canvas_screen_pos for consistent conversion
    return ImVec2(pos.x * zoom + scrolling.x + canvas_screen_pos.x,
                  pos.y * zoom + scrolling.y + canvas_screen_pos.y);
}

// Generate CLER-compatible C++ code
std::string FlowCanvas::GenerateCppCode() const
{
    std::stringstream code;
    
    // Header
    code << "// Generated by CLER Flow\n";
    code << "#include <cler.hpp>\n";
    
    // Include required headers for blocks
    std::set<std::string> headers;
    for (const auto& [id, node] : nodes) {
        if (!node->GetSpec()->header_file.empty()) {
            headers.insert(node->GetSpec()->header_file);
        }
    }
    
    for (const auto& header : headers) {
        code << "#include \"" << header << "\"\n";
    }
    
    code << "\nint main() {\n";
    code << "    using namespace cler;\n\n";
    
    // Create block instances
    code << "    // Create blocks\n";
    for (const auto& [id, node] : nodes) {
        code << node->GenerateInstantiation();
    }
    
    code << "\n    // Connect blocks\n";
    
    // Generate connections (CLER uses >> operator)
    for (const auto& conn : connections) {
        auto* from = nodes.find(conn.from_node_id)->second.get();
        auto* to = nodes.find(conn.to_node_id)->second.get();
        
        if (from && to) {
            std::string from_port = from->output_ports[conn.from_port_index].name;
            std::string to_port = to->input_ports[conn.to_port_index].name;
            
            code << "    " << from->instance_name;
            if (!from_port.empty() && from_port != "out") {
                code << "->" << from_port;
            }
            code << " >> " << to->instance_name;
            if (!to_port.empty() && to_port != "in") {
                code << "->" << to_port;
            }
            code << ";\n";
        }
    }
    
    code << "\n    // Run flowgraph\n";
    code << "    BlockRunner runner;\n";
    
    // Add all blocks to runner
    for (const auto& [id, node] : nodes) {
        code << "    runner.add(" << node->instance_name << ");\n";
    }
    
    code << "    runner.run();\n";
    code << "\n    return 0;\n";
    code << "}\n";
    
    return code.str();
}

std::string FlowCanvas::ToJSON() const
{
    // TODO: Implement JSON serialization
    return "{}";
}

void FlowCanvas::FromJSON(const std::string& json)
{
    // TODO: Implement JSON deserialization
}

} // namespace clerflow