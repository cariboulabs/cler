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

// Core-nodes exact LinkType enum
enum class LinkType {
    NONE,
    // BINV: both nodes inverted.
    BINV_LEFT,
    BINV_RIGHT_OVER,
    BINV_RIGHT_UNDER,
    BINV_RIGHT_MID,
    // IINV: only input node inverted.
    IINV_RIGHT_OVER,
    IINV_LEFT_OVER,
    IINV_RIGHT_UNDER,
    IINV_LEFT_UNDER,
    IINV_MID,
    // OINV: only output node inverted.
    OINV_RIGHT_OVER,
    OINV_LEFT_OVER,
    OINV_RIGHT_UNDER,
    OINV_LEFT_UNDER,
    OINV_MID,
    // NINV: No Inversion. Location of input node wrt output node.
    NINV_RIGHT,
    NINV_LEFT_OVER,
    NINV_LEFT_UNDER,
    NINV_LEFT_MID
};

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
    
    // Cached routing information (core-nodes style)
    mutable LinkType link_type = LinkType::NONE;
    mutable bool routing_cached = false;
    mutable float xSepIn = 0;
    mutable float xSepOut = 0;
    mutable float ykSep = 0;
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
    
    // Connection rendering (core-nodes exact implementation)
    void DrawConnections();
    void DrawConnection(Connection& conn);
    void DrawConnectionPreview();
    
    // Core-nodes exact drawing functions
    void SetLinkProperties(Connection& conn, const VisualNode* from_node, const VisualNode* to_node);
    void DrawLinkBezier(const Connection& conn, ImVec2 pInput, ImVec2 pOutput, float rounding, bool invert = false) const;
    void DrawLinkIOInv(const Connection& conn, ImVec2 pInput, ImVec2 pOutput, float dHandle) const;
    void DrawLinkBNInv(const Connection& conn, ImVec2 pInput, ImVec2 pOutput, float dHandle, bool invert = false) const;
    
    // Helper functions for link property calculation
    void SetInputSepUp(Connection& conn) const;
    void SetInputSepDown(Connection& conn) const;
    void SetOutputSepUp(Connection& conn) const;
    void SetOutputSepDown(Connection& conn) const;
    void SetNodeSep(Connection& conn, const VisualNode* from_node, const VisualNode* to_node) const;
    
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
    const VisualNode* GetNode(size_t id) const;
    ImVec2 ScreenToCanvas(ImVec2 pos) const;
    ImVec2 CanvasToScreen(ImVec2 pos) const;
    
    // Connection repair - tries to fix indices using port names
    void RepairConnections();
};

} // namespace clerflow