// Test with simulated node bounds to match actual behavior
#include <iostream>
#include <cmath>
#include <string>

struct Point {
    float x, y;
    Point(float x_, float y_) : x(x_), y(y_) {}
};

struct Node {
    Point pos;
    Point size;
    Node(float x, float y, float w, float h) : pos(x, y), size(w, h) {}
};

enum ConnectionType {
    NORMAL,
    NORMAL_VERTICAL,
    INVERTED_SIMPLE,
    INVERTED_OVER,
    INVERTED_UNDER,
    INVERTED_MID,
    COMPLEX_OVER,
    COMPLEX_UNDER,
    COMPLEX_AROUND,
    STRAIGHT,
    SELF_LOOP
};

const char* typeToString(ConnectionType type) {
    switch(type) {
        case NORMAL: return "NORMAL";
        case NORMAL_VERTICAL: return "NORMAL_VERTICAL";
        case INVERTED_SIMPLE: return "INVERTED_SIMPLE";
        case INVERTED_OVER: return "INVERTED_OVER";
        case INVERTED_UNDER: return "INVERTED_UNDER";
        case INVERTED_MID: return "INVERTED_MID";
        case COMPLEX_OVER: return "COMPLEX_OVER";
        case COMPLEX_UNDER: return "COMPLEX_UNDER";
        case COMPLEX_AROUND: return "COMPLEX_AROUND";
        case STRAIGHT: return "STRAIGHT";
        case SELF_LOOP: return "SELF_LOOP";
    }
    return "UNKNOWN";
}

// This mimics the actual ClassifyConnection with node bounds
ConnectionType classifyConnectionWithNodes(Point p1, Point p2, const Node* from_node, const Node* to_node) {
    float zoom = 1.0f;
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float abs_dy = std::abs(dy);
    
    const float yMargin = 30.0f * zoom;
    const float nodeMargin = 20.0f * zoom;
    const float overlapThreshold = 40.0f * zoom;
    
    // Check node bounds
    bool nodes_overlap_horizontally = false;
    bool clear_vertical_space_above = false;
    bool clear_vertical_space_below = false;
    
    if (from_node && to_node) {
        float from_max_x = from_node->pos.x + from_node->size.x;
        float to_min_x = to_node->pos.x;
        float from_max_y = from_node->pos.y + from_node->size.y;
        float from_min_y = from_node->pos.y;
        float to_min_y = to_node->pos.y;
        float to_max_y = to_node->pos.y + to_node->size.y;
        
        nodes_overlap_horizontally = (to_min_x < from_max_x);
        clear_vertical_space_above = (from_max_y + nodeMargin < to_min_y);
        clear_vertical_space_below = (from_min_y > to_max_y + nodeMargin);
    }
    
    // Very short connection
    if (distance < 30.0f * zoom) {
        return STRAIGHT;
    }
    
    // Clean left-to-right
    if (dx >= overlapThreshold) {
        if (abs_dy < dx * 0.7f) {
            return NORMAL;
        }
        return NORMAL_VERTICAL;
    }
    
    // NINV_LEFT cases: Check if nodes overlap horizontally
    if (from_node && to_node && nodes_overlap_horizontally && dx > 0) {
        if (clear_vertical_space_above) {
            return COMPLEX_OVER;
        } else if (clear_vertical_space_below) {
            return COMPLEX_UNDER;
        } else {
            return COMPLEX_AROUND;
        }
    }
    // Fall back to port-based detection if no node info
    else if (dx > 0 && dx < overlapThreshold) {
        if (abs_dy > yMargin + nodeMargin) {
            if (dy < 0) {
                return COMPLEX_OVER;
            } else {
                return COMPLEX_UNDER;
            }
        } else {
            return COMPLEX_AROUND;
        }
    }
    
    // Backward connections
    if (dx < 0) {
        if (abs_dy < yMargin * 2.0f) {
            return INVERTED_SIMPLE;
        }
        if (abs_dy > yMargin * 3.0f) {
            return (dy < 0) ? INVERTED_OVER : INVERTED_UNDER;
        }
        return INVERTED_MID;
    }
    
    // Nearly vertical
    if (std::abs(dx) < 20.0f * zoom) {
        if (abs_dy < yMargin) {
            return STRAIGHT;
        } else if (abs_dy < yMargin * 3) {
            return NORMAL_VERTICAL;
        } else {
            return (dy < 0) ? COMPLEX_OVER : COMPLEX_UNDER;
        }
    }
    
    return NORMAL;
}

void testElbowWithNodes(const char* name, Point p1, Point p2, const Node* from, const Node* to) {
    float zoom = 1.0f;
    float dHandle = 10.0f * zoom;
    float xMargin = dHandle * 0.8f;
    
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    
    ConnectionType type = classifyConnectionWithNodes(p1, p2, from, to);
    
    // Only test polyline types
    if (type != COMPLEX_OVER && type != COMPLEX_UNDER && type != COMPLEX_AROUND &&
        type != INVERTED_OVER && type != INVERTED_UNDER) {
        std::cout << "\n=== " << name << " ===" << std::endl;
        std::cout << "Type: " << typeToString(type) << " (not polyline)" << std::endl;
        return;
    }
    
    ConnectionType polylineType = type;
    if (type == INVERTED_OVER) polylineType = COMPLEX_OVER;
    if (type == INVERTED_UNDER) polylineType = COMPLEX_UNDER;
    
    // Calculate x coordinates
    float x1 = p1.x + xMargin;
    float x2 = x1 + dHandle;
    float x3 = p2.x - xMargin;
    float x4 = x3 - dHandle;
    
    // For backward connections, extend
    if (dx < 0) {
        float extend = std::max(xMargin * 1.5f, std::abs(dx) * 0.3f + xMargin);
        x1 = p1.x + extend;
        x2 = x1 + dHandle;
        x4 = p2.x - extend - dHandle;
        x3 = p2.x - extend;
    }
    
    // Calculate y coordinate and handle direction
    float yM = (p1.y + p2.y) * 0.5f;
    float yHandle = dHandle;
    
    if (polylineType == COMPLEX_OVER) {
        yHandle = -dHandle;
        if (std::abs(dy) < xMargin * 2) {
            yM = std::min(p1.y, p2.y) - xMargin;
        }
    } else if (polylineType == COMPLEX_UNDER) {
        yHandle = dHandle;
        if (std::abs(dy) < xMargin * 2) {
            yM = std::max(p1.y, p2.y) + xMargin;
        }
    } else if (polylineType == COMPLEX_AROUND) {
        yHandle = dHandle;
        float node_bottom = std::max(p1.y, p2.y);
        yM = node_bottom + xMargin * 2;
    }
    
    // The fixed approach logic
    float yApproachLeft = (p1.y < yM) ? (yM - dHandle) : (yM + dHandle);
    float yLeaveRight = (p2.y < yM) ? (yM - dHandle) : (yM + dHandle);
    
    // Build points
    struct {
        float x, y;
    } points[] = {
        {p1.x, p1.y},                     // 0
        {x1, p1.y},                       // 1
        {x2, p1.y},                       // 2
        {x2, p1.y + yHandle},             // 3 - first elbow
        {x2, yApproachLeft},              // 4
        {x2, yM},                         // 5
        {x1, yM},                         // 6
        {x3, yM},                         // 7
        {x4, yM},                         // 8
        {x4, yLeaveRight},                // 9
        {x4, p2.y - yHandle},             // 10 - last elbow approach
        {x4, p2.y},                       // 11
        {x3, p2.y},                       // 12
        {p2.x, p2.y}                      // 13
    };
    
    std::cout << "\n=== " << name << " ===" << std::endl;
    std::cout << "Port from (" << p1.x << ", " << p1.y << ") to (" << p2.x << ", " << p2.y << ")" << std::endl;
    if (from && to) {
        std::cout << "Node from (" << from->pos.x << ", " << from->pos.y << ", w:" << from->size.x << ", h:" << from->size.y << ")";
        std::cout << " to (" << to->pos.x << ", " << to->pos.y << ", w:" << to->size.x << ", h:" << to->size.y << ")" << std::endl;
        bool overlap = (to->pos.x < from->pos.x + from->size.x);
        std::cout << "Nodes overlap horizontally: " << (overlap ? "YES" : "NO") << std::endl;
    }
    std::cout << "Type: " << typeToString(type) << " -> " << typeToString(polylineType) << std::endl;
    std::cout << "yHandle: " << yHandle << ", yM: " << yM << std::endl;
    
    bool hasError = false;
    
    // Check first elbow
    float firstElbow = points[3].y - points[2].y;
    std::cout << "First elbow: " << firstElbow;
    if (polylineType == COMPLEX_OVER && firstElbow > 0) {
        std::cout << " *** REVERSE! Should go UP ***";
        hasError = true;
    } else if ((polylineType == COMPLEX_UNDER || polylineType == COMPLEX_AROUND) && firstElbow < 0) {
        std::cout << " *** REVERSE! Should go DOWN ***";
        hasError = true;
    } else {
        std::cout << " (OK)";
    }
    std::cout << std::endl;
    
    // Check last elbow approach
    float lastElbow = points[11].y - points[10].y;
    std::cout << "Last elbow: " << lastElbow;
    
    // The last elbow should match yHandle sign (except when adjusted)
    // Actually, let's check if it makes sense geometrically
    if (polylineType == COMPLEX_OVER) {
        // Should approach from correct direction
        if (p2.y > yM && lastElbow < 0) {
            std::cout << " *** MIGHT BE REVERSE ***";
            hasError = true;
        } else {
            std::cout << " (OK)";
        }
    } else if (polylineType == COMPLEX_UNDER || polylineType == COMPLEX_AROUND) {
        if (p2.y < yM && lastElbow > 0) {
            std::cout << " *** MIGHT BE REVERSE ***";
            hasError = true;
        } else {
            std::cout << " (OK)";
        }
    }
    std::cout << std::endl;
    
    if (hasError) {
        std::cout << "*** ERRORS DETECTED ***" << std::endl;
    }
}

int main() {
    std::cout << "Testing with node bounds simulation\n" << std::endl;
    
    // Standard node size
    float nodeWidth = 150.0f;
    float nodeHeight = 100.0f;
    
    // Test backward connection with slight vertical offset
    // This is the problematic case: output to left and slightly below
    {
        std::cout << "\n--- BACKWARD WITH SLIGHT OFFSET (The Problem Case) ---" << std::endl;
        
        // From node at (200, 100), output port on right side
        Node from(200, 100, nodeWidth, nodeHeight);
        Point p1(200 + nodeWidth, 100 + 30);  // Output port on right side
        
        // To node at (0, 110), input port on left side - slightly below
        Node to(0, 110, nodeWidth, nodeHeight);
        Point p2(0, 110 + 30);  // Input port on left side
        
        testElbowWithNodes("Backward slightly below", p1, p2, &from, &to);
    }
    
    // Test with overlapping nodes
    {
        std::cout << "\n--- OVERLAPPING NODES ---" << std::endl;
        
        Node from(100, 100, nodeWidth, nodeHeight);
        Point p1(100 + nodeWidth, 100 + 30);
        
        Node to(120, 150, nodeWidth, nodeHeight);  // Overlaps horizontally
        Point p2(120, 150 + 30);
        
        testElbowWithNodes("Overlapping forward down", p1, p2, &from, &to);
    }
    
    // Test with nodes clearly above/below
    {
        std::cout << "\n--- CLEAR VERTICAL SEPARATION ---" << std::endl;
        
        Node from(100, 100, nodeWidth, nodeHeight);
        Point p1(100 + nodeWidth, 100 + 30);
        
        Node to(120, 250, nodeWidth, nodeHeight);  // Clear below
        Point p2(120, 250 + 30);
        
        testElbowWithNodes("Clear vertical space below", p1, p2, &from, &to);
    }
    
    // Test backward with larger vertical offset
    {
        std::cout << "\n--- BACKWARD WITH LARGE OFFSET ---" << std::endl;
        
        Node from(200, 100, nodeWidth, nodeHeight);
        Point p1(200 + nodeWidth, 100 + 30);
        
        Node to(0, 200, nodeWidth, nodeHeight);
        Point p2(0, 200 + 30);
        
        testElbowWithNodes("Backward far below", p1, p2, &from, &to);
    }
    
    // Test the specific problematic positions
    {
        std::cout << "\n--- SPECIFIC PROBLEM POSITIONS ---" << std::endl;
        
        for (int dy = 10; dy <= 50; dy += 10) {
            Node from(200, 100, nodeWidth, nodeHeight);
            Point p1(200 + nodeWidth, 100 + 30);
            
            Node to(0, 100 + dy, nodeWidth, nodeHeight);
            Point p2(0, 100 + dy + 30);
            
            char name[100];
            sprintf(name, "Backward %dpx below", dy);
            testElbowWithNodes(name, p1, p2, &from, &to);
        }
    }
    
    return 0;
}