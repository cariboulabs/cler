# CLER Flow Architecture Improvements Plan

## Overview
A phased approach to fix architectural issues and build a robust, scalable foundation for CLER Flow. Prioritizes stability and correctness over features.

## Critical Context: ImGui Performance Considerations

You're right to question the performance concerns. ImGui is immediate mode, which means:
- **Already culled by ImGui**: ImGui only renders what's in the clip rect
- **Vertex buffers are rebuilt each frame**: Optimization is less about caching, more about reducing draw calls
- **CPU-side calculations matter**: We should minimize expensive calculations per frame

### Revised Performance Focus
- Reduce expensive calculations (bezier math, text size calculations)
- Minimize string operations in render loop
- Batch similar operations where possible
- Let ImGui handle the culling (it already does this well)

---

## Phase 0: Quick Wins (2-3 hours)
*Small fixes with immediate impact*

### 0.1 Add Connection Validation
```cpp
// In FlowCanvas::DrawConnections()
bool FlowCanvas::IsConnectionValid(const Connection& conn) {
    return nodes.find(conn.from_node_id) != nodes.end() &&
           nodes.find(conn.to_node_id) != nodes.end() &&
           conn.from_port_index < nodes[conn.from_node_id]->output_ports.size() &&
           conn.to_port_index < nodes[conn.to_node_id]->input_ports.size();
}
```
**Impact**: Prevents crashes from invalid connections

### 0.2 Cache Text Sizes
```cpp
class VisualNode {
    struct CachedMetrics {
        float title_width = -1.0f;
        std::vector<float> input_widths;
        std::vector<float> output_widths;
        
        void Invalidate() { title_width = -1.0f; }
    };
    mutable CachedMetrics metrics_cache;
};
```
**Impact**: Avoid recalculating text sizes every frame

### 0.3 Reduce String Allocations
```cpp
// Instead of creating strings every frame for abbreviated names
class VisualNode {
    mutable std::unordered_map<int, std::string> abbreviated_names_cache;
    
    const std::string& GetCachedAbbreviation(const std::string& name, int rotation) const {
        auto key = std::hash<std::string>{}(name) ^ rotation;
        if (auto it = abbreviated_names_cache.find(key); it != abbreviated_names_cache.end()) {
            return it->second;
        }
        return abbreviated_names_cache[key] = GetAbbreviatedName(name, rotation);
    }
};
```
**Impact**: Reduce string allocations in render loop

---

## Phase 1: Connection System Robustness (4-5 hours)
*Fix the fragile connection system*

### 1.1 Stable Port Identification System

**Current Problem**: Ports identified by index, breaks when ports change

**Solution**: Unique port IDs
```cpp
// New port identification system
using PortUID = uint32_t;

struct PortIdentifier {
    std::string name;      // Stable name from BlockSpec
    PortUID uid;           // Unique ID generated at creation
    size_t index;          // Current index (for compatibility)
    
    bool operator==(const PortIdentifier& other) const {
        return uid == other.uid;
    }
};

struct VisualPort {
    PortIdentifier id;     // Stable identification
    std::string display_name;
    DataType data_type;
    ImVec2 position;
    bool is_connected = false;
};
```

### 1.2 Robust Connection Structure

```cpp
struct Connection {
    // Keep backward compatibility
    size_t from_node_id;
    size_t to_node_id;
    
    // New stable identification
    PortUID from_port_uid;
    PortUID to_port_uid;
    
    // Cached for performance
    DataType data_type;
    ConnectionType routing_type;  // For optimized rendering
    
    // Validation
    bool validated = false;
    bool valid = false;
    
    bool Validate(const FlowCanvas& canvas) {
        validated = true;
        valid = canvas.ValidateConnection(*this);
        return valid;
    }
};
```

### 1.3 Connection Manager Class

```cpp
class ConnectionManager {
private:
    std::vector<Connection> connections;
    std::unordered_map<size_t, std::vector<size_t>> node_connections; // node_id -> connection indices
    mutable std::vector<size_t> invalid_connections;
    
public:
    void AddConnection(Connection conn);
    void RemoveConnection(size_t index);
    void RemoveNodeConnections(size_t node_id);
    void ValidateAll(const FlowCanvas& canvas);
    const std::vector<Connection>& GetConnections() const { return connections; }
    std::vector<Connection> GetNodeConnections(size_t node_id) const;
};
```

**Benefits**:
- O(1) lookup for node's connections
- Batch validation
- Clean separation of concerns

---

## Phase 2: Runtime/Visual Separation (6-8 hours)
*Separate execution model from visualization*

### 2.1 Runtime Model

```cpp
// Pure runtime/logical representation
class RuntimeNode {
public:
    using NodeID = size_t;
    using ParamMap = std::unordered_map<std::string, std::variant<int, float, std::string>>;
    
    NodeID id;
    std::string block_type;
    std::string instance_name;
    ParamMap template_params;
    ParamMap constructor_params;
    
    // No visual information here
    RuntimeNode(NodeID id, const BlockSpec& spec);
    std::string GenerateCode() const;
    bool Validate() const;
};

class RuntimeGraph {
    std::unordered_map<size_t, std::unique_ptr<RuntimeNode>> nodes;
    ConnectionManager connections;
    
public:
    // Graph operations
    NodeID AddNode(const BlockSpec& spec);
    void RemoveNode(NodeID id);
    bool Connect(NodeID from, PortUID from_port, NodeID to, PortUID to_port);
    
    // Code generation
    std::string GenerateCpp() const;
    bool Validate() const;
    
    // Serialization
    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& j);
};
```

### 2.2 Visual Model Refactor

```cpp
class VisualNode {
    // Reference to runtime
    RuntimeNode::NodeID runtime_id;
    
    // Pure visual state
    ImVec2 position;
    ImVec2 size;
    ImVec2 min_size;
    
    struct VisualState {
        bool selected = false;
        bool collapsed = false;
        bool resizing = false;
        int rotation = 0;
    } state;
    
    // Visual-only caches
    mutable CachedMetrics metrics;
    
public:
    // Get runtime data through reference
    const RuntimeNode* GetRuntimeNode() const;
    RuntimeNode* GetRuntimeNode();
};
```

### 2.3 Canvas Integration

```cpp
class FlowCanvas {
    // Separated models
    RuntimeGraph runtime_graph;
    std::unordered_map<size_t, std::unique_ptr<VisualNode>> visual_nodes;
    
    // Single source of truth for connections
    const ConnectionManager& GetConnections() const { 
        return runtime_graph.GetConnections(); 
    }
};
```

---

## Phase 3: Type System Improvements (3-4 hours)
*Add proper type safety and validation*

### 3.1 Typed Parameter System

```cpp
enum class ParamType {
    Integer,
    Float,
    String,
    Bool,
    DataType,  // For template parameters like T
    Custom
};

struct TypedParameter {
    std::string name;
    ParamType type;
    std::variant<int, float, std::string, bool> value;
    std::optional<std::string> constraint;  // e.g., ">0", "power_of_2"
    
    bool Validate() const;
    std::string ToString() const;
};

class ParameterValidator {
    static bool ValidateConstraint(const TypedParameter& param);
    static bool ValidateTemplateParam(const std::string& param_name, 
                                     const std::string& value,
                                     const BlockSpec& spec);
};
```

### 3.2 Connection Type Safety

```cpp
class TypeChecker {
    // Check if two DataTypes can be connected
    static bool AreCompatible(DataType from, DataType to);
    
    // Check with template parameter context
    static bool AreCompatibleWithContext(
        DataType from, 
        DataType to,
        const RuntimeNode& from_node,
        const RuntimeNode& to_node
    );
    
    // Get actual type after template substitution
    static DataType ResolveTemplateType(
        const std::string& type_str,
        const RuntimeNode& node
    );
};
```

---

## Phase 4: Performance Optimizations (2-3 hours)
*Optimize for large graphs*

### 4.1 Lazy Evaluation

```cpp
class VisualNode {
    mutable struct {
        bool ports_dirty = true;
        bool size_dirty = true;
        bool metrics_dirty = true;
    } dirty_flags;
    
    void InvalidatePorts() const { dirty_flags.ports_dirty = true; }
    void UpdatePortsIfNeeded() const {
        if (dirty_flags.ports_dirty) {
            UpdatePortPositions();
            dirty_flags.ports_dirty = false;
        }
    }
};
```

### 4.2 Connection Rendering Optimization

```cpp
class ConnectionRenderer {
    struct BezierCache {
        ImVec2 p1, p2, cp1, cp2;
        uint32_t hash;
        
        bool IsValid(const Connection& conn) const;
    };
    
    mutable std::unordered_map<size_t, BezierCache> bezier_cache;
    
    void DrawConnection(const Connection& conn) const {
        // Check cache first
        if (auto it = bezier_cache.find(conn.id); it != bezier_cache.end()) {
            if (it->second.IsValid(conn)) {
                // Use cached bezier points
                DrawCachedBezier(it->second);
                return;
            }
        }
        
        // Calculate and cache
        auto bezier = CalculateBezier(conn);
        bezier_cache[conn.id] = bezier;
        DrawCachedBezier(bezier);
    }
};
```

### 4.3 Batch Operations

```cpp
class FlowCanvas {
    void BeginBatchOperation() { batch_mode = true; }
    void EndBatchOperation() { 
        batch_mode = false;
        if (needs_validation) ValidateAll();
        if (needs_layout) UpdateLayout();
    }
    
    // Use for multi-node operations like paste, load, etc.
};
```

---

## Phase 5: Error Handling & Robustness (3-4 hours)
*Add proper error handling throughout*

### 5.1 Result Type System

```cpp
template<typename T, typename E>
class Result {
    std::variant<T, E> data;
public:
    bool IsOk() const;
    bool IsErr() const;
    T& Value();
    E& Error();
    T ValueOr(T default_value);
};

enum class FlowError {
    Success = 0,
    NodeNotFound,
    PortNotFound,
    TypeMismatch,
    CircularDependency,
    InvalidParameter,
    ConnectionFailed
};

using FlowResult = Result<void, FlowError>;
template<typename T>
using FlowResultT = Result<T, FlowError>;
```

### 5.2 Validation System

```cpp
class GraphValidator {
    struct ValidationIssue {
        enum Severity { Warning, Error };
        Severity severity;
        std::string message;
        std::optional<size_t> node_id;
        std::optional<size_t> connection_id;
    };
    
    std::vector<ValidationIssue> ValidateGraph(const RuntimeGraph& graph);
    bool HasErrors(const std::vector<ValidationIssue>& issues);
    void DisplayIssues(const std::vector<ValidationIssue>& issues);
};
```

---

## Implementation Priority Order

### Week 1: Foundation (MUST DO)
1. **Phase 0**: Quick wins (2-3 hours)
2. **Phase 1.1-1.2**: Connection stability (3 hours)
3. **Phase 3.1**: Basic type safety (2 hours)

### Week 2: Core Improvements  
4. **Phase 2.1-2.2**: Runtime/Visual separation (5 hours)
5. **Phase 1.3**: Connection manager (2 hours)
6. **Phase 5.1**: Error handling basics (2 hours)

### Week 3: Polish
7. **Phase 4**: Performance optimizations (3 hours)
8. **Phase 3.2**: Advanced type checking (2 hours)
9. **Phase 5.2**: Validation system (2 hours)

---

## Success Metrics

### Stability
- [ ] No crashes from invalid connections
- [ ] Graceful handling of all error cases
- [ ] Can load/save complex graphs without data loss

### Performance (Revised for ImGui)
- [ ] 60+ FPS with 500 nodes (ImGui already culls)
- [ ] <16ms frame time with 1000 connections
- [ ] Smooth interaction with 50+ selected nodes

### Maintainability
- [ ] Clear separation of runtime/visual
- [ ] All connections validated
- [ ] Type-safe parameter system
- [ ] Comprehensive error messages

### Scalability
- [ ] Can handle 1000+ nodes
- [ ] Can handle 5000+ connections
- [ ] Memory usage scales linearly
- [ ] No architectural barriers to growth

---

## Migration Strategy

### Backward Compatibility
- Keep old Connection struct working during transition
- Add migration code for old save files
- Deprecate old APIs gradually

### Testing Strategy
1. Create test graphs of increasing complexity
2. Benchmark before/after each phase
3. Stress test with generated large graphs
4. Validate code generation output

### Risk Mitigation
- Each phase is independently valuable
- Can pause between phases if needed
- Old system remains functional during migration
- Incremental rollout possible