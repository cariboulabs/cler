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
    for (auto& conn : connections) {
        DrawConnection(conn);
    }
    
    // Draw connection preview
    if (isConnecting) {
        DrawConnectionPreview();
    }
}

void FlowCanvas::DrawConnection(Connection& conn)
{
    auto* from_node = GetNode(conn.from_node_id);
    auto* to_node = GetNode(conn.to_node_id);
    
    // Simple validation
    if (!from_node || !to_node) return;
    if (conn.from_port_index >= from_node->output_ports.size()) return;
    if (conn.to_port_index >= to_node->input_ports.size()) return;
    
    ImVec2 pOutput = from_node->output_ports[conn.from_port_index].GetScreenPos(from_node->position);
    ImVec2 pInput = to_node->input_ports[conn.to_port_index].GetScreenPos(to_node->position);
    
    pOutput = CanvasToScreen(pOutput);
    pInput = CanvasToScreen(pInput);
    
    // Set link properties using core-nodes logic
    if (!conn.routing_cached) {
        SetLinkProperties(conn, from_node, to_node);
        conn.routing_cached = true;
    }
    
    // Calculate distance-based rounding exactly like core-nodes
    float linkDistance = std::sqrt((pInput.x - pOutput.x) * (pInput.x - pOutput.x) + 
                                   (pInput.y - pOutput.y) * (pInput.y - pOutput.y)) / 150.0f;
    float rounding = 25.0f * linkDistance / zoom;  // Note: divide by zoom (scale in core-nodes)
    float dHandle = 15.0f * zoom;  // Note: multiply by zoom (scale in core-nodes)
    
    switch (conn.link_type) {
        case LinkType::NINV_RIGHT:
            DrawLinkBezier(conn, pInput, pOutput, rounding);
            break;
            
        case LinkType::NINV_LEFT_OVER:
        case LinkType::NINV_LEFT_UNDER:
        case LinkType::NINV_LEFT_MID:
            DrawLinkBNInv(conn, pInput, pOutput, dHandle);  // No invert flag for NINV
            break;
            
        case LinkType::BINV_LEFT:
            DrawLinkBezier(conn, pInput, pOutput, rounding, true);  // Use rounding, not dHandle
            break;
            
        case LinkType::BINV_RIGHT_OVER:
        case LinkType::BINV_RIGHT_UNDER:
        case LinkType::BINV_RIGHT_MID:
            DrawLinkBNInv(conn, pInput, pOutput, dHandle, true);
            break;
            
        case LinkType::IINV_RIGHT_OVER:
        case LinkType::IINV_LEFT_OVER:
        case LinkType::IINV_RIGHT_UNDER:
        case LinkType::IINV_LEFT_UNDER:
        case LinkType::OINV_RIGHT_OVER:
        case LinkType::OINV_LEFT_OVER:
        case LinkType::OINV_RIGHT_UNDER:
        case LinkType::OINV_LEFT_UNDER:
            DrawLinkIOInv(conn, pInput, pOutput, dHandle);
            break;
            
        case LinkType::IINV_MID:
        case LinkType::OINV_MID:
            DrawLinkBezier(conn, pInput, pOutput, 0.0f);  // Zero rounding for MID cases
            break;
            
        default:
            // This shouldn't happen if classification is correct
            DrawLinkBezier(conn, pInput, pOutput, rounding);
            break;
    }
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
    
    // Use core-nodes drawing for preview as well
    Connection preview_conn;
    preview_conn.data_type = type;
    preview_conn.from_node_id = connectingFromNode;
    preview_conn.from_port_index = connectingFromPort;
    
    // Draw simple bezier for preview (we don't have full node context for classification)
    float dHandle = 50.0f * zoom;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 color = dataTypeToColor(type);
    float thickness = 3.0f * zoom;  // Slightly thicker for visibility
    
    if (connectingFromOutput) {
        // p1 is output, p2 is input
        ImVec2 handle = ImVec2(-dHandle, 0.0f);
        draw_list->AddBezierCubic(p2, ImVec2(p2.x + handle.x, p2.y + handle.y), 
                                  ImVec2(p1.x - handle.x, p1.y - handle.y), p1, color, thickness);
    } else {
        // p1 is input, p2 is output  
        ImVec2 handle = ImVec2(-dHandle, 0.0f);
        draw_list->AddBezierCubic(p1, ImVec2(p1.x + handle.x, p1.y + handle.y), 
                                  ImVec2(p2.x - handle.x, p2.y - handle.y), p2, color, thickness);
    }
}

// Old connection drawing functions removed - using core-nodes implementation

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

const VisualNode* FlowCanvas::GetNode(size_t id) const
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

// Old connection classification and drawing functions removed

} // namespace clerflow
