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

// Connection routing types inspired by core-nodes
enum class ConnectionType {
    // Normal connections (no inversion)
    NORMAL,           // Standard left to right
    NORMAL_VERTICAL,  // Nearly vertical
    
    // Inverted connections (right to left)
    INVERTED_SIMPLE,  // Simple S-curve
    INVERTED_OVER,    // Route over obstacles
    INVERTED_UNDER,   // Route under obstacles
    INVERTED_MID,     // Route through middle
    
    // Complex routing (needs polyline)
    COMPLEX_OVER,     // Multi-segment routing over
    COMPLEX_UNDER,    // Multi-segment routing under
    COMPLEX_AROUND,   // Route around obstacles
    
    // Special cases
    STRAIGHT,         // Very short, nearly straight
    SELF_LOOP        // Node connecting to itself
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
    
    // Cached routing information
    mutable ConnectionType routing_type = ConnectionType::NORMAL;
    mutable bool routing_cached = false;
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
    // Connection routing constants - tuned for clean visual appearance
    static constexpr float BASE_FILLET_RADIUS = 10.0f;           // Base radius for rounded corners
    static constexpr float BASE_Y_MARGIN = 30.0f;                // Vertical clearance threshold
    static constexpr float BASE_NODE_MARGIN = 20.0f;             // Space between nodes
    static constexpr float BASE_OVERLAP_THRESHOLD = 40.0f;       // Horizontal overlap detection
    static constexpr float BASE_SHORT_DISTANCE = 30.0f;          // Threshold for straight connections
    static constexpr float FILLET_CLEARANCE_FACTOR = 4.0f;       // Multiplier for fillet clearance
    static constexpr float VERTICAL_ALIGN_FACTOR = 2.0f;         // Factor for horizontal alignment
    static constexpr float VERTICAL_SEPARATION_FACTOR = 3.0f;    // Factor for vertical separation
    static constexpr float BACKWARD_MIN_EXTEND = 7.0f;           // Min pixels from block edge for backward connections
    static constexpr float BACKWARD_DYNAMIC_FACTOR = 0.02f;      // Dynamic extension based on distance
    
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
    void DrawBezierCurve(ImVec2 p1, ImVec2 p2, ImU32 color, float thickness = 4.0f,
                         const VisualNode* from_node = nullptr, const VisualNode* to_node = nullptr);  // Increased from 3.0f
    ConnectionType ClassifyConnection(ImVec2 p1, ImVec2 p2, 
                                      const VisualNode* from_node = nullptr, 
                                      const VisualNode* to_node = nullptr) const;
    void CalculateBezierControlPoints(ImVec2 p1, ImVec2 p2, ConnectionType type, ImVec2& cp1, ImVec2& cp2) const;
    void DrawBezierConnection(ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, float rounding, bool invert = false);
    void DrawPolylineConnection(ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, ConnectionType type,
                                const VisualNode* from_node = nullptr, const VisualNode* to_node = nullptr);
    void DrawPolylineSegments(ImDrawList* draw_list, const std::vector<ImVec2>& points, ImU32 color, float thickness);
    
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