/******************************************************************************************
*                                                                                         *
*    FlowCanvas - Main canvas for flowgraph editing                                      *
*                                                                                         *
*    Modernized version of CoreDiagram with improved architecture                        *
*                                                                                         *
******************************************************************************************/

#pragma once

#include "block_spec.hpp"
#include "visual_node.hpp"
#include <imgui.h>
#include <memory>
#include <vector>
#include <unordered_map>

namespace clerflow {

// Connection between nodes
struct Connection {
    size_t from_node_id;
    size_t from_port_index;
    size_t to_node_id;
    size_t to_port_index;
    DataType data_type;
    
    // Store port names for stability when specs change
    std::string from_port_name;
    std::string to_port_name;
};

class FlowCanvas {
public:
    FlowCanvas();
    ~FlowCanvas() = default;
    
    // Main draw function
    void Draw();
    
    // Node management
    size_t AddNode(std::shared_ptr<BlockSpec> spec, ImVec2 position);
    void RemoveNode(size_t node_id);
    void ClearAll();
    
    // Connection management
    bool CanConnect(size_t from_node, size_t from_port, 
                   size_t to_node, size_t to_port) const;
    void AddConnection(size_t from_node, size_t from_port,
                      size_t to_node, size_t to_port);
    void RemoveConnection(size_t from_node, size_t from_port,
                         size_t to_node, size_t to_port);
    
    // Selection
    void SelectNode(size_t node_id, bool add_to_selection = false);
    void ClearSelection();
    std::vector<size_t> GetSelectedNodes() const { return selectedNodes; }
    
    // Serialization
    std::string ToJSON() const;
    void FromJSON(const std::string& json);
    
    // Code generation
    std::string GenerateCppCode() const;
    
private:
    // Canvas state
    ImVec2 scrolling{0.0f, 0.0f};
    float zoom = 1.0f;
    mutable ImVec2 canvas_screen_pos{0.0f, 0.0f};  // Cached for coordinate conversion
    
    // Nodes and connections
    std::unordered_map<size_t, std::unique_ptr<VisualNode>> nodes;
    std::vector<Connection> connections;
    size_t nextNodeId = 1;
    
    // Interaction state
    bool isPanning = false;
    bool isConnecting = false;
    bool isDraggingNode = false;
    bool isResizingNode = false;
    size_t resizingNodeId = 0;
    size_t connectingFromNode = 0;
    size_t connectingFromPort = 0;
    bool connectingFromOutput = true;
    
    // Selection
    std::vector<size_t> selectedNodes;
    bool isBoxSelecting = false;
    ImVec2 boxSelectStart;
    
    // Grid
    void DrawGrid();
    
    // Node rendering
    void DrawNodes();
    void DrawNode(VisualNode* node);
    
    // Connection rendering (with core-nodes style splines)
    void DrawConnections();
    void DrawConnection(const Connection& conn);
    void DrawConnectionPreview();
    void DrawBezierCurve(ImVec2 p1, ImVec2 p2, ImU32 color, float thickness = 3.0f);
    
    // Input handling
    void HandleInput();
    void HandleNodeInteraction();
    void HandleCanvasInteraction();
    
    // Context menus
    void HandleContextMenus();
    void ShowCanvasContextMenu();
    void ShowNodeContextMenu(size_t node_id);
    ImVec2 contextMenuPos;
    size_t contextNodeId = 0;
    
    // Helpers
    VisualNode* GetNode(size_t id);
    ImVec2 ScreenToCanvas(ImVec2 pos) const;
    ImVec2 CanvasToScreen(ImVec2 pos) const;
    
    // Connection repair - tries to fix indices using port names
    void RepairConnections();
};

} // namespace clerflow