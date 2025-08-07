# Connection System Enhancement Plan

## Overview
Upgrade the connection rendering system in CLER Flow to match the sophistication of core-nodes, with proper bezier curves, smart routing, and collision detection.

## Current Issues
1. Fixed handle distances don't scale properly with zoom
2. No collision avoidance - connections pass through nodes
3. Simple routing logic - only basic left-right and inverted cases
4. Using wrong ImGui function (AddBezierCubic vs AddBezierCurve)
5. No connection type classification system

## Phase 1: Core Connection Rendering (Priority: HIGH)

### 1.1 Fix Bezier Curve Implementation
- [ ] Switch from `AddBezierCubic` to `AddBezierCurve`
  - Current: `drawList->AddBezierCubic(p1, cp1, cp2, p2, ...)`
  - Target: `drawList->AddBezierCurve(p1, p2, p3, p4, ...)`
  - Where p2 and p3 are control points, not p1-relative

### 1.2 Dynamic Handle Calculation
- [ ] Implement distance-based rounding like core-nodes:
  ```cpp
  float linkDistance = distance / 150.0f;
  float rounding = 25.0f * linkDistance / zoom;
  float handleLength = rounding * zoom;
  ```
- [ ] Make handles purely horizontal for clean curves
- [ ] Scale handles with zoom level properly

### 1.3 Connection Type Classification
- [ ] Add `ConnectionType` enum:
  ```cpp
  enum class ConnectionType {
      NORMAL,           // Left to right, simple curve
      VERTICAL_DOWN,    // Nearly vertical, going down
      VERTICAL_UP,      // Nearly vertical, going up  
      INVERTED,         // Right to left, needs loop
      INVERTED_ABOVE,   // Loop goes above
      INVERTED_BELOW,   // Loop goes below
      STRAIGHT          // Very close, nearly straight line
  };
  ```
- [ ] Classify connections based on relative positions
- [ ] Use appropriate routing for each type

## Phase 2: Collision Detection System (Priority: HIGH)

### 2.1 Node Intersection Detection
- [ ] Create `NodeBounds` structure with padding:
  ```cpp
  struct NodeBounds {
      ImVec2 min;
      ImVec2 max;
      float padding = 10.0f;  // Clearance around nodes
      
      bool ContainsPoint(ImVec2 p) const;
      bool IntersectsLine(ImVec2 p1, ImVec2 p2) const;
      bool IntersectsBezier(ImVec2 p1, ImVec2 cp1, ImVec2 cp2, ImVec2 p2) const;
  };
  ```

### 2.2 Connection Path Testing
- [ ] Implement bezier curve sampling:
  ```cpp
  bool TestBezierCollision(ImVec2 p1, ImVec2 cp1, ImVec2 cp2, ImVec2 p2, 
                           const std::vector<NodeBounds>& obstacles);
  ```
- [ ] Sample curve at intervals (e.g., t = 0.1, 0.2, ... 0.9)
- [ ] Check if any sample point is inside a node bounds

### 2.3 Avoidance Routing
- [ ] Implement waypoint system:
  ```cpp
  struct RoutingWaypoint {
      ImVec2 position;
      bool isCorner;  // Sharp turn vs smooth curve
  };
  ```
- [ ] Generate waypoints around obstacles:
  - Top route: Go above the blocking node
  - Bottom route: Go below the blocking node
  - Multi-segment: Chain multiple waypoints for complex paths

## Phase 3: Smart Routing Algorithm (Priority: MEDIUM)

### 3.1 A* Pathfinding (Optional - Advanced)
- [ ] Create routing grid or navigation mesh
- [ ] Implement A* for optimal path finding
- [ ] Cache successful routes for performance

### 3.2 Simple Avoidance Strategy (Recommended First)
- [ ] For each connection:
  1. Try direct bezier curve
  2. If collision detected, try curving above
  3. If still colliding, try curving below
  4. If still colliding, use S-curve with larger loops
  5. Last resort: Multi-segment path around obstacles

### 3.3 Overlap Prevention
- [ ] Detect when multiple connections follow same path
- [ ] Add small offset to separate overlapping connections
- [ ] Group connections between same nodes

## Phase 4: Visual Enhancements (Priority: LOW)

### 4.1 Connection Styles
- [ ] Different thickness for different data types
- [ ] Animated flow indicators (optional)
- [ ] Connection shadows for depth
- [ ] Highlighted state for selected connections

### 4.2 Performance Optimization
- [ ] Cull connections outside viewport
- [ ] LOD system - simplify distant connections
- [ ] Batch similar connections for rendering

## Implementation Order

### Sprint 1: Foundation (2-3 hours)
1. Fix bezier curve function usage
2. Implement proper handle calculation
3. Add connection type classification
4. Test with various node arrangements

### Sprint 2: Collision Detection (3-4 hours)
1. Implement NodeBounds structure
2. Add bezier intersection testing
3. Create basic avoidance routing
4. Add waypoint system for complex routes

### Sprint 3: Polish (2-3 hours)
1. Handle edge cases (very close nodes, extreme angles)
2. Optimize performance for many connections
3. Add visual improvements
4. Fine-tune routing parameters

## Testing Scenarios

### Basic Tests
- [ ] Simple left-to-right connection
- [ ] Vertical connections (up and down)
- [ ] Inverted connections (right-to-left)
- [ ] Very short connections
- [ ] Very long connections

### Collision Tests
- [ ] Connection through single node
- [ ] Connection through multiple nodes
- [ ] Multiple connections between same nodes
- [ ] Connections in tight spaces
- [ ] Circular node arrangements

### Stress Tests
- [ ] 100+ connections
- [ ] Zoom in/out with many connections
- [ ] Dragging nodes with connections
- [ ] Rotating nodes with connections

## Success Metrics
- Connections never pass through nodes
- Smooth, aesthetically pleasing curves
- Performance: <16ms render time for 200 connections
- Readable connection paths even in complex diagrams
- Consistent behavior across zoom levels

## Reference Implementation
Study `CoreDiagram::DrawLinkBezier` and related functions in:
`/home/alon/repos/cler/cler_flow/reference/core-nodes/core-nodes/CoreDiagram.cpp`

Key insights:
- Horizontal handles for clean curves
- Distance-based handle scaling
- Multiple routing strategies based on connection type
- Sophisticated collision avoidance with waypoints