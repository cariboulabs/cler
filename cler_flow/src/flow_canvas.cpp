/******************************************************************************************
*                                                                                         *
*    FlowCanvas - Implementation focused on CLER flowgraph generation                    *
*                                                                                         *
******************************************************************************************/

#define IMGUI_DEFINE_MATH_OPERATORS
#include "flow_canvas.hpp"
#include <imgui_internal.h>  // For ImBezierCubicCalc and ImRect
#include <algorithm>
#include <sstream>
#include <cmath>
#include <set>

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
    
    // Draw connection preview
    if (isConnecting) {
        DrawConnectionPreview();
    }
}

void FlowCanvas::DrawConnection(const Connection& conn)
{
    auto* from_node = GetNode(conn.from_node_id);
    auto* to_node = GetNode(conn.to_node_id);
    
    // Simple validation
    if (!from_node || !to_node) return;
    if (conn.from_port_index >= from_node->output_ports.size()) return;
    if (conn.to_port_index >= to_node->input_ports.size()) return;
    
    ImVec2 p1 = from_node->output_ports[conn.from_port_index].GetScreenPos(from_node->position);
    ImVec2 p2 = to_node->input_ports[conn.to_port_index].GetScreenPos(to_node->position);
    
    p1 = CanvasToScreen(p1);
    p2 = CanvasToScreen(p2);
    
    // Cache the routing type for performance
    if (!conn.routing_cached) {
        conn.routing_type = ClassifyConnection(p1, p2, from_node, to_node);
        conn.routing_cached = true;
    }
    
    DrawBezierCurve(p1, p2, dataTypeToColor(conn.data_type), 4.0f, from_node, to_node);
}

void FlowCanvas::DrawConnectionPreview()
{
    if (!isConnecting) return;
    
    auto* from_node = GetNode(connectingFromNode);
    if (!from_node) return;
    
    ImVec2 p1, p2;
    DataType type = DataType::Float;
    ImVec2 mouse_pos = ImGui::GetMousePos();
    ImVec2 canvas_mouse = ScreenToCanvas(mouse_pos);
    
    // Check for nearby ports to snap to
    const float snap_distance = 20.0f * zoom;  // Snap when within 20 pixels
    bool snapped = false;
    ImVec2 snap_pos = mouse_pos;
    
    if (connectingFromOutput) {
        if (connectingFromPort >= from_node->output_ports.size()) return;
        p1 = from_node->output_ports[connectingFromPort].GetScreenPos(from_node->position);
        p1 = CanvasToScreen(p1);
        type = from_node->output_ports[connectingFromPort].data_type;
        
        // Look for nearby input ports to snap to
        for (auto& [id, node] : nodes) {
            if (id == connectingFromNode) continue;  // Skip self
            
            for (size_t i = 0; i < node->input_ports.size(); ++i) {
                ImVec2 port_pos = node->input_ports[i].GetScreenPos(node->position);
                float dist = std::sqrt(std::pow(port_pos.x - canvas_mouse.x, 2) + 
                                      std::pow(port_pos.y - canvas_mouse.y, 2));
                
                if (dist < snap_distance) {
                    // Check if types are compatible
                    if (CanConnect(connectingFromNode, connectingFromPort, id, i)) {
                        snap_pos = CanvasToScreen(port_pos);
                        snapped = true;
                        
                        // Visual feedback: draw a highlight circle around the port
                        ImDrawList* draw_list = ImGui::GetWindowDrawList();
                        draw_list->AddCircle(snap_pos, 8.0f * zoom, 
                                            IM_COL32(100, 255, 100, 200), 12, 2.0f);
                        break;
                    }
                }
            }
            if (snapped) break;
        }
        
        p2 = snapped ? snap_pos : mouse_pos;
    } else {
        if (connectingFromPort >= from_node->input_ports.size()) return;
        p2 = from_node->input_ports[connectingFromPort].GetScreenPos(from_node->position);
        p2 = CanvasToScreen(p2);
        type = from_node->input_ports[connectingFromPort].data_type;
        
        // Look for nearby output ports to snap to
        for (auto& [id, node] : nodes) {
            if (id == connectingFromNode) continue;  // Skip self
            
            for (size_t i = 0; i < node->output_ports.size(); ++i) {
                ImVec2 port_pos = node->output_ports[i].GetScreenPos(node->position);
                float dist = std::sqrt(std::pow(port_pos.x - canvas_mouse.x, 2) + 
                                      std::pow(port_pos.y - canvas_mouse.y, 2));
                
                if (dist < snap_distance) {
                    // Check if types are compatible
                    if (CanConnect(id, i, connectingFromNode, connectingFromPort)) {
                        snap_pos = CanvasToScreen(port_pos);
                        snapped = true;
                        
                        // Visual feedback: draw a highlight circle around the port
                        ImDrawList* draw_list = ImGui::GetWindowDrawList();
                        draw_list->AddCircle(snap_pos, 8.0f * zoom, 
                                            IM_COL32(100, 255, 100, 200), 12, 2.0f);
                        break;
                    }
                }
            }
            if (snapped) break;
        }
        
        p1 = snapped ? snap_pos : mouse_pos;
    }
    
    // Draw with slightly thicker line during preview for better visibility
    DrawBezierCurve(p1, p2, dataTypeToColor(type), 3.0f);
}

void FlowCanvas::DrawBezierCurve(ImVec2 p1, ImVec2 p2, ImU32 color, float thickness,
                                 const VisualNode* from_node, const VisualNode* to_node)
{
    // Calculate core-nodes style parameters
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float linkDistance = distance / 150.0f;
    // Increased rounding factor for smoother transition between modes
    float rounding = 40.0f * linkDistance / zoom;  // Was 25.0f, now 40.0f for more pronounced curves
    
    // Classify connection type
    ConnectionType type = ClassifyConnection(p1, p2, from_node, to_node);
    
    // Route based on type (matching core-nodes decision tree)
    switch (type) {
        // Simple bezier cases (like NINV_RIGHT, BINV_LEFT)
        case ConnectionType::NORMAL:
            DrawBezierConnection(p1, p2, color, thickness, rounding);
            break;
            
        case ConnectionType::NORMAL_VERTICAL:
            // Vertical forward - use bezier with adjusted handles
            DrawBezierConnection(p1, p2, color, thickness, rounding);
            break;
            
        case ConnectionType::INVERTED_SIMPLE:
            // Backward but clear (BINV_LEFT) - use bezier with inversion
            DrawBezierConnection(p1, p2, color, thickness, rounding, true);
            break;
            
        case ConnectionType::INVERTED_MID:
            // Medium vertical backward (IINV_MID/OINV_MID) - use bezier with no rounding
            DrawBezierConnection(p1, p2, color, thickness, 0.0f, true);
            break;
            
        // Polyline cases (like NINV_LEFT_*, BINV_RIGHT_*)
        case ConnectionType::COMPLEX_OVER:
        case ConnectionType::COMPLEX_UNDER:
        case ConnectionType::COMPLEX_AROUND:
            // Use polyline for routing around obstacles
            DrawPolylineConnection(p1, p2, color, thickness, type, from_node, to_node);
            break;
            
        case ConnectionType::INVERTED_OVER:
        case ConnectionType::INVERTED_UNDER:
            // These map to polyline routing
            DrawPolylineConnection(p1, p2, color, thickness, 
                                 type == ConnectionType::INVERTED_OVER ? ConnectionType::COMPLEX_OVER :
                                 ConnectionType::COMPLEX_UNDER, from_node, to_node);
            break;
            
        case ConnectionType::STRAIGHT:
            // Very short - minimal curve
            DrawBezierConnection(p1, p2, color, thickness, distance * 0.05f);
            break;
            
        default:
            DrawBezierConnection(p1, p2, color, thickness, rounding);
            break;
    }
    
    // Arrow indicator removed for now - connection type determines routing
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
        const float snap_distance = 20.0f * zoom;  // Same snap distance as preview
        bool connected = false;
        
        for (auto& [id, node] : nodes) {
            if (id == connectingFromNode) continue;  // Skip self
            
            if (connectingFromOutput) {
                // First try exact hit
                int input_port = node->GetInputPortAt(canvas_mouse);
                if (input_port >= 0) {
                    AddConnection(connectingFromNode, connectingFromPort, id, input_port);
                    connected = true;
                    break;
                }
                
                // If no exact hit, check snap distance
                for (size_t i = 0; i < node->input_ports.size(); ++i) {
                    ImVec2 port_pos = node->input_ports[i].GetScreenPos(node->position);
                    float dist = std::sqrt(std::pow(port_pos.x - canvas_mouse.x, 2) + 
                                          std::pow(port_pos.y - canvas_mouse.y, 2));
                    
                    if (dist < snap_distance) {
                        // Check if types are compatible
                        if (CanConnect(connectingFromNode, connectingFromPort, id, i)) {
                            AddConnection(connectingFromNode, connectingFromPort, id, i);
                            connected = true;
                            break;
                        }
                    }
                }
            } else {
                // First try exact hit
                int output_port = node->GetOutputPortAt(canvas_mouse);
                if (output_port >= 0) {
                    AddConnection(id, output_port, connectingFromNode, connectingFromPort);
                    connected = true;
                    break;
                }
                
                // If no exact hit, check snap distance
                for (size_t i = 0; i < node->output_ports.size(); ++i) {
                    ImVec2 port_pos = node->output_ports[i].GetScreenPos(node->position);
                    float dist = std::sqrt(std::pow(port_pos.x - canvas_mouse.x, 2) + 
                                          std::pow(port_pos.y - canvas_mouse.y, 2));
                    
                    if (dist < snap_distance) {
                        // Check if types are compatible
                        if (CanConnect(id, i, connectingFromNode, connectingFromPort)) {
                            AddConnection(id, i, connectingFromNode, connectingFromPort);
                            connected = true;
                            break;
                        }
                    }
                }
            }
            
            if (connected) break;
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
                
                // Invalidate routing cache for all connections involving this node
                for (auto& conn : connections) {
                    if (conn.from_node_id == id || conn.to_node_id == id) {
                        conn.routing_cached = false;
                    }
                }
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
    
    // Check for duplicate
    for (const auto& conn : connections) {
        if (conn.from_node_id == from_node && conn.from_port_index == from_port &&
            conn.to_node_id == to_node && conn.to_port_index == to_port) {
            return; // Already exists
        }
    }
    
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
    
    // Store port names for stability
    auto* from = GetNode(from_node);
    auto* to = GetNode(to_node);
    
    if (from && from_port < from->output_ports.size()) {
        conn.data_type = from->output_ports[from_port].data_type;
        conn.from_port_name = from->output_ports[from_port].name;
    }
    
    if (to && to_port < to->input_ports.size()) {
        conn.to_port_name = to->input_ports[to_port].name;
    }
    
    connections.push_back(conn);
}

void FlowCanvas::RemoveConnection(size_t from_node, size_t from_port,
                                 size_t to_node, size_t to_port)
{
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [=](const Connection& c) {
                return c.from_node_id == from_node && c.from_port_index == from_port &&
                       c.to_node_id == to_node && c.to_port_index == to_port;
            }),
        connections.end()
    );
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
    
    // After loading, repair connections using port names
    RepairConnections();
}

void FlowCanvas::RepairConnections()
{
    // Try to fix connection indices using port names
    for (auto& conn : connections) {
        auto* from_node = GetNode(conn.from_node_id);
        auto* to_node = GetNode(conn.to_node_id);
        
        if (!from_node || !to_node) continue;
        
        // Try to find ports by name if names are stored
        if (!conn.from_port_name.empty()) {
            for (size_t i = 0; i < from_node->output_ports.size(); ++i) {
                if (from_node->output_ports[i].name == conn.from_port_name) {
                    conn.from_port_index = i;
                    break;
                }
            }
        }
        
        if (!conn.to_port_name.empty()) {
            for (size_t i = 0; i < to_node->input_ports.size(); ++i) {
                if (to_node->input_ports[i].name == conn.to_port_name) {
                    conn.to_port_index = i;
                    break;
                }
            }
        }
        
        // Clear routing cache when repairing
        conn.routing_cached = false;
    }
}

/*******************************************************************************
 * CONNECTION ROUTING STRATEGY
 * 
 * This flowgraph editor uses two distinct routing methods based on the 
 * relative positions of connected ports:
 * 
 * 1. FORWARD CONNECTIONS (output on LEFT, input on RIGHT):
 *    - Always use smooth Bezier curves
 *    - Direct, flowing left-to-right connections
 *    - Types: NORMAL, NORMAL_VERTICAL
 * 
 * 2. BACKWARD CONNECTIONS (output on RIGHT, input on LEFT):
 *    - Always use polyline routing with rounded corners (fillets)
 *    - Routes around blocks to avoid visual overlap
 *    - Types: INVERTED_OVER, INVERTED_UNDER
 *    - Polylines consist of 14 points forming a path that:
 *      - Extends backward from the output port
 *      - Routes above or below the blocks
 *      - Approaches the input port from behind
 * 
 * Key Design Decisions:
 * - 7 pixels clearance from block edges for tight, clean routing
 * - 10 pixel fillet radius for smooth corners
 * - Automatic shadow rendering for visual depth
 * - All routing respects zoom level for consistent appearance
 * 
 * The classification happens in ClassifyConnection() and determines which
 * drawing method to use: DrawBezierConnection() or DrawPolylineConnection()
 ******************************************************************************/

ConnectionType FlowCanvas::ClassifyConnection(ImVec2 p1, ImVec2 p2, 
                                              const VisualNode* from_node, 
                                              const VisualNode* to_node) const
{
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float abs_dy = std::abs(dy);
    
    // Scale thresholds by zoom (using constants from header)
    const float yMargin = BASE_Y_MARGIN * zoom;      // Vertical clearance needed
    const float nodeMargin = BASE_NODE_MARGIN * zoom;   // Space between nodes
    const float overlapThreshold = BASE_OVERLAP_THRESHOLD * zoom;  // Horizontal overlap detection
    
    // Node bounds calculation for future enhancements
    // Currently we use simple port-to-port distance for routing decisions
    // This section is preserved for potential future use when we need
    // more sophisticated obstacle avoidance
    /*
    if (from_node && to_node) {
        // Convert to screen space for comparison
        ImVec2 from_min = CanvasToScreen(from_node->position);
        ImVec2 from_max = CanvasToScreen(ImVec2(from_node->position.x + from_node->size.x, 
                                                 from_node->position.y + from_node->size.y));
        ImVec2 to_min = CanvasToScreen(to_node->position);
        ImVec2 to_max = CanvasToScreen(ImVec2(to_node->position.x + to_node->size.x, 
                                              to_node->position.y + to_node->size.y));
        
        // Could be used for:
        // - Horizontal overlap detection: (to_min.x < from_max.x)
        // - Vertical clearance above: (from_max.y + nodeMargin < to_min.y)
        // - Vertical clearance below: (from_min.y > to_max.y + nodeMargin)
    }
    */
    
    // Very short connection - minimal curve
    if (distance < BASE_SHORT_DISTANCE * zoom) {
        return ConnectionType::STRAIGHT;
    }
    
    // =================================================================
    // FORWARD CONNECTIONS (output on left, input on right)
    // Rule: ALWAYS use bezier curves for smooth, direct routing
    // =================================================================
    if (dx > 0) {
        // Check if nearly vertical
        if (abs_dy > yMargin * 2.0f && dx < overlapThreshold) {
            return ConnectionType::NORMAL_VERTICAL;
        }
        // Standard forward connection - use bezier
        return ConnectionType::NORMAL;
    }
    
    
    // =================================================================
    // BACKWARD CONNECTIONS (output on right, input on left)  
    // Rule: ALWAYS use polylines to route around blocks cleanly
    // =================================================================
    if (dx < 0) {
        // Check if nodes are roughly horizontally aligned
        // Use more generous threshold for horizontal alignment (2x yMargin)
        if (abs_dy < yMargin * VERTICAL_ALIGN_FACTOR) {
            // Horizontally aligned - route around the blocks
            // Choose direction based on relative port positions
            if (dy >= 0) {
                // Output is higher or equal - route below
                return ConnectionType::INVERTED_UNDER;  // Will map to COMPLEX_UNDER polyline
            } else {
                // Output is lower - route above
                return ConnectionType::INVERTED_OVER;   // Will map to COMPLEX_OVER polyline
            }
        }
        
        // Significant vertical separation
        if (abs_dy > yMargin * VERTICAL_SEPARATION_FACTOR) {
            // Clear vertical clearance - use complex routing
            if (dy < 0) {
                return ConnectionType::INVERTED_OVER;   // BINV_RIGHT_OVER
            } else {
                return ConnectionType::INVERTED_UNDER;  // BINV_RIGHT_UNDER
            }
        } else {
            // Medium vertical distance - still use polyline for all backward connections
            // Choose direction based on dy
            if (dy < 0) {
                return ConnectionType::INVERTED_OVER;
            } else {
                return ConnectionType::INVERTED_UNDER;
            }
        }
    }
    
    // Nearly vertical connections - these should never occur in practice
    // since we handle all dx > 0 and dx < 0 cases above
    // This is defensive code for edge cases
    const float dHandle = BASE_FILLET_RADIUS * zoom;
    const float verticalThreshold = dHandle * FILLET_CLEARANCE_FACTOR;
    
    if (std::abs(dx) < verticalThreshold) {
        if (abs_dy < yMargin) {
            return ConnectionType::STRAIGHT;
        } else {
            return ConnectionType::NORMAL_VERTICAL;
        }
    }
    
    // Default to normal bezier
    return ConnectionType::NORMAL;
}

void FlowCanvas::CalculateBezierControlPoints(ImVec2 p1, ImVec2 p2, ConnectionType type, 
                                              ImVec2& cp1, ImVec2& cp2) const
{
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    // Core-nodes exact formula for handle calculation
    float link_distance = distance / 150.0f;
    float rounding = 25.0f * link_distance / zoom;
    float handle_length = rounding * zoom;
    
    // Ensure minimum handle length for very close nodes
    if (distance < 50.0f) {
        handle_length = distance * 0.5f;
    }
    
    switch (type) {
        case ConnectionType::STRAIGHT:
            // Nearly straight line - minimal handles
            handle_length = distance * 0.2f;
            cp1 = ImVec2(p1.x + handle_length, p1.y);
            cp2 = ImVec2(p2.x - handle_length, p2.y);
            break;
            
        case ConnectionType::NORMAL:
            // Standard left-to-right with purely horizontal handles (core-nodes style)
            cp1 = ImVec2(p1.x + handle_length, p1.y);  // Horizontal from start
            cp2 = ImVec2(p2.x - handle_length, p2.y);  // Horizontal to end
            break;
            
        case ConnectionType::NORMAL_VERTICAL:
            // Nearly vertical - use outward curves
            {
                float h = std::max(handle_length, std::abs(dy) * 0.3f);
                cp1 = ImVec2(p1.x + h, p1.y);
                cp2 = ImVec2(p2.x - h, p2.y);
            }
            break;
            
        case ConnectionType::INVERTED_SIMPLE:
        case ConnectionType::INVERTED_MID:
            // Right-to-left - large horizontal loops (core-nodes BINV_LEFT style)
            {
                float loop_size = std::max(std::abs(dx) * 0.5f + 50.0f, 100.0f);
                cp1 = ImVec2(p1.x + loop_size, p1.y);  // Go right from output
                cp2 = ImVec2(p2.x - loop_size, p2.y);  // Come left to input
            }
            break;
            
        case ConnectionType::INVERTED_OVER:
            // Loop goes above
            {
                float loop_size = std::max(std::abs(dx) * 0.5f + 60.0f, 120.0f);
                float y_offset = std::abs(dy) * 0.3f;
                cp1 = ImVec2(p1.x + loop_size, p1.y - y_offset);
                cp2 = ImVec2(p2.x - loop_size, p2.y + y_offset);
            }
            break;
            
        case ConnectionType::INVERTED_UNDER:
            // Loop goes below
            {
                float loop_size = std::max(std::abs(dx) * 0.5f + 60.0f, 120.0f);
                float y_offset = std::abs(dy) * 0.3f;
                cp1 = ImVec2(p1.x + loop_size, p1.y + y_offset);
                cp2 = ImVec2(p2.x - loop_size, p2.y - y_offset);
            }
            break;
            
        default:
            // Fallback to normal
            cp1 = ImVec2(p1.x + handle_length, p1.y);
            cp2 = ImVec2(p2.x - handle_length, p2.y);
            break;
    }
}

void FlowCanvas::DrawBezierConnection(ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, float rounding, bool invert)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Calculate control points exactly like core-nodes
    float handle_x = rounding * zoom;
    ImVec2 cp1 = ImVec2(p1.x + handle_x, p1.y);  // Horizontal from p1
    ImVec2 cp2 = ImVec2(p2.x - handle_x, p2.y);  // Horizontal to p2
    
    if (invert) {
        // For inverted, handles go outward more to create nice S-curve
        float loop_size = std::max(handle_x * 2.0f, 80.0f * zoom);
        cp1 = ImVec2(p1.x + loop_size, p1.y);
        cp2 = ImVec2(p2.x - loop_size, p2.y);
    }
    
    // Draw shadow for depth (optional)
    if (thickness > 1.5f) {
        draw_list->AddBezierCubic(p1, cp1, cp2, p2, 
                                  IM_COL32(0, 0, 0, 80), (thickness + 2.0f) * zoom);
    }
    
    // Draw main curve - using AddBezierCubic (equivalent to core-nodes' AddBezierCurve)
    draw_list->AddBezierCubic(p1, cp1, cp2, p2, color, thickness * zoom);
}

void FlowCanvas::DrawPolylineConnection(ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, ConnectionType type,
                                        const VisualNode* from_node, const VisualNode* to_node)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Polyline drawing parameters
    constexpr float MARGIN_FACTOR = 0.8f;  // Distance from node to first elbow as factor of fillet radius
    
    const float dHandle = BASE_FILLET_RADIUS * zoom;  // Radius of rounded corners (from class constants)
    const float xMargin = dHandle * MARGIN_FACTOR;  // Distance from node to first elbow
    
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    
    // Build waypoints exactly like core-nodes
    std::vector<ImVec2> points;
    points.reserve(14);  // Core-nodes uses 14 points
    
    // Calculate x coordinates - closer to nodes
    float x1 = p1.x + xMargin;
    float x2 = x1 + dHandle;
    float x3 = p2.x - xMargin;
    float x4 = x3 - dHandle;
    
    // For backward connections, keep vertical lines close to blocks
    if (dx < 0) {
        // Use minimal extension to keep vertical segments very close to the blocks
        // Fixed 7 pixels from block edge
        float minExtend = BACKWARD_MIN_EXTEND * zoom;
        float dynamicExtend = std::abs(dx) * BACKWARD_DYNAMIC_FACTOR;
        float extend = minExtend + dynamicExtend;
        
        x1 = p1.x + extend;
        x2 = x1 + dHandle;
        x4 = p2.x - extend - dHandle;
        x3 = p2.x - extend;
    }
    
    // Calculate y coordinate for middle section and handle direction
    float yM = (p1.y + p2.y) * 0.5f;  // Default to midpoint
    float yHandle = dHandle;  // Default positive (going down from input, up to output)
    
    if (type == ConnectionType::COMPLEX_OVER) {
        // Route above - handles go upward
        yHandle = -dHandle;  // Negative to go up
        // For backward connections, always route AROUND (above both blocks)
        if (dx < 0 || std::abs(dy) < xMargin * 2) {
            // Use actual node bounds if available
            float topOfBlocks;
            if (from_node && to_node) {
                // Convert to screen space and get actual top of blocks
                ImVec2 from_top = CanvasToScreen(from_node->position);
                ImVec2 to_top = CanvasToScreen(to_node->position);
                topOfBlocks = std::min(from_top.y, to_top.y);
            } else {
                // Fallback: estimate based on port positions
                float blockHalfHeight = 30.0f * zoom;
                topOfBlocks = std::min(p1.y - blockHalfHeight, p2.y - blockHalfHeight);
            }
            yM = topOfBlocks - xMargin * 2;  // Route above the blocks
        }
    } else if (type == ConnectionType::COMPLEX_UNDER) {
        // Route below - handles go downward
        yHandle = dHandle;  // Positive to go down
        // For backward connections, always route AROUND (below both blocks)
        if (dx < 0 || std::abs(dy) < xMargin * 2) {
            // Use actual node bounds if available
            float bottomOfBlocks;
            if (from_node && to_node) {
                // Convert to screen space and get actual bottom of blocks
                ImVec2 from_bottom = CanvasToScreen(ImVec2(from_node->position.x, 
                                                           from_node->position.y + from_node->size.y));
                ImVec2 to_bottom = CanvasToScreen(ImVec2(to_node->position.x, 
                                                         to_node->position.y + to_node->size.y));
                bottomOfBlocks = std::max(from_bottom.y, to_bottom.y);
            } else {
                // Fallback: estimate based on port positions
                float blockHalfHeight = 30.0f * zoom;
                bottomOfBlocks = std::max(p1.y + blockHalfHeight, p2.y + blockHalfHeight);
            }
            yM = bottomOfBlocks + xMargin * 2;  // Route below the blocks, close but clear
        }
    } else if (type == ConnectionType::COMPLEX_AROUND) {
        // NINV_LEFT_MID case - route around/below when nodes overlap
        // Always route below for consistency (like core-nodes NINV_LEFT_MID)
        yHandle = dHandle;  // Positive to go down
        // Push the horizontal segment well below both nodes
        float node_bottom = std::max(p1.y, p2.y);
        yM = node_bottom + xMargin * 2;  // Go well below to avoid overlap
    }
    // REMOVED the else if (p1.y > p2.y) case - this was causing reverse elbows!
    // The handle direction should be determined by the routing type (OVER/UNDER/AROUND),
    // not by the relative Y positions of the ports
    
    // Build core-nodes style point sequence with correct handle directions
    // The key insight: we need to approach yM from the correct direction
    float yApproachLeft = (p1.y < yM) ? (yM - dHandle) : (yM + dHandle);   // Where to approach middle from left
    float yLeaveRight = (p2.y < yM) ? (yM - dHandle) : (yM + dHandle);     // Where to leave middle to right
    
    // Build the polyline points
    points.push_back(p1);                               // 0
    points.push_back(ImVec2(x1, p1.y));                // 1
    points.push_back(ImVec2(x2, p1.y));                // 2  
    points.push_back(ImVec2(x2, p1.y + yHandle));      // 3 - depart from output
    points.push_back(ImVec2(x2, yApproachLeft));       // 4 - approach middle
    points.push_back(ImVec2(x2, yM));                  // 5 - middle left
    points.push_back(ImVec2(x1, yM));                  // 6
    points.push_back(ImVec2(x3, yM));                  // 7
    points.push_back(ImVec2(x4, yM));                  // 8 - middle right
    points.push_back(ImVec2(x4, yLeaveRight));         // 9 - leave middle
    
    // Point 10: Approach to destination port
    // For backward connections routing around blocks, we need to approach correctly
    float yApproachDest;
    if (dx < 0 && type == ConnectionType::COMPLEX_UNDER) {
        // Backward routing below - approach from below to avoid bad fillet
        yApproachDest = p2.y + std::abs(yHandle);
    } else if (dx < 0 && type == ConnectionType::COMPLEX_OVER) {
        // Backward routing above - approach from above
        yApproachDest = p2.y - std::abs(yHandle);
    } else {
        // Forward connections - symmetrical with point 3
        yApproachDest = p2.y - yHandle;
    }
    
    points.push_back(ImVec2(x4, yApproachDest));       // 10 - approach destination
    points.push_back(ImVec2(x4, p2.y));                // 11
    points.push_back(ImVec2(x3, p2.y));                // 12
    points.push_back(p2);                               // 13
    
    // Draw exactly like core-nodes with AddBezierQuadratic
    DrawPolylineSegments(draw_list, points, color, thickness * zoom);
}

void FlowCanvas::DrawPolylineSegments(ImDrawList* draw_list, const std::vector<ImVec2>& points, 
                                      ImU32 color, float thickness)
{
    // Match core-nodes exact drawing pattern
    if (points.size() < 14) return;
    
    // Helper lambda to calculate midpoint
    auto midpoint = [](const ImVec2& a, const ImVec2& b) -> ImVec2 {
        return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    };
    
    // Helper lambda to draw the polyline segments (reduces duplication)
    auto drawSegments = [&](ImU32 col, float thick) {
        // Segment 0->1: Start to first horizontal
        draw_list->AddBezierQuadratic(points[0], midpoint(points[0], points[1]), points[1], col, thick);
        
        // Segment 1->3: Top-left corner (horizontal to vertical)
        draw_list->AddBezierQuadratic(points[1], ImVec2(points[2].x, points[1].y), points[3], col, thick);
        
        // Segment 3->4: Vertical down
        draw_list->AddBezierQuadratic(points[3], midpoint(points[3], points[4]), points[4], col, thick);
        
        // Segment 4->6: Bottom-left corner (vertical to horizontal)
        draw_list->AddBezierQuadratic(points[4], ImVec2(points[4].x, points[5].y), points[6], col, thick);
        
        // Segment 6->7: Horizontal middle
        draw_list->AddBezierQuadratic(points[6], midpoint(points[6], points[7]), points[7], col, thick);
        
        // Segment 7->9: Bottom-right corner (horizontal to vertical)
        draw_list->AddBezierQuadratic(points[7], ImVec2(points[8].x, points[7].y), points[9], col, thick);
        
        // Segment 9->10: Vertical up
        draw_list->AddBezierQuadratic(points[9], midpoint(points[9], points[10]), points[10], col, thick);
        
        // Segment 10->12: Top-right corner (vertical to horizontal)
        // The corner control point is at point[11] for proper fillet
        draw_list->AddBezierQuadratic(points[10], points[11], points[12], col, thick);
        
        // Segment 12->13: Final horizontal to destination
        draw_list->AddBezierQuadratic(points[12], midpoint(points[12], points[13]), points[13], col, thick);
    };
    
    // Shadow pass first (if thick enough)
    if (thickness > 2.0f) {
        ImU32 shadow_col = IM_COL32(0, 0, 0, 80);
        float shadow_thick = thickness + 2.0f * zoom;
        drawSegments(shadow_col, shadow_thick);
    }
    
    // Main drawing pass
    drawSegments(color, thickness);
}

} // namespace clerflow