// Test to ensure forward connections ALWAYS use polylines, never bezier
#include <iostream>
#include <cmath>
#include <vector>
#include <string>

struct Point {
    float x, y;
    Point(float x_, float y_) : x(x_), y(y_) {}
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

bool isPolylineType(ConnectionType type) {
    return type == COMPLEX_OVER || type == COMPLEX_UNDER || type == COMPLEX_AROUND ||
           type == INVERTED_OVER || type == INVERTED_UNDER;
}

bool isBezierType(ConnectionType type) {
    return type == NORMAL || type == NORMAL_VERTICAL || 
           type == INVERTED_SIMPLE || type == INVERTED_MID;
}

ConnectionType classifyConnection(Point p1, Point p2) {
    float zoom = 1.0f;
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float abs_dy = std::abs(dy);
    
    // Constants from the actual code
    constexpr float BASE_Y_MARGIN = 30.0f;
    constexpr float BASE_NODE_MARGIN = 20.0f;
    constexpr float BASE_OVERLAP_THRESHOLD = 40.0f;
    constexpr float BASE_SHORT_DISTANCE = 30.0f;
    
    const float yMargin = BASE_Y_MARGIN * zoom;
    const float nodeMargin = BASE_NODE_MARGIN * zoom;
    const float overlapThreshold = BASE_OVERLAP_THRESHOLD * zoom;
    
    // Very short connection
    if (distance < BASE_SHORT_DISTANCE * zoom) {
        return STRAIGHT;
    }
    
    // Forward connections (dx > 0) should ALWAYS use bezier
    if (dx > 0) {
        // Check if nearly vertical
        if (abs_dy > yMargin * 2.0f && dx < overlapThreshold) {
            return NORMAL_VERTICAL;
        }
        // Standard forward connection - use bezier
        return NORMAL;
    }
    
    // Backward connections (dx < 0) should ALWAYS use polylines
    if (dx < 0) {
        if (abs_dy < yMargin * 2.0f) {
            // Horizontally aligned - use polyline
            return (dy >= 0) ? INVERTED_UNDER : INVERTED_OVER;
        }
        // All other backward connections also use polyline
        return (dy < 0) ? INVERTED_OVER : INVERTED_UNDER;
    }
    
    return NORMAL;  // Shouldn't reach here
}

void testForwardConnection(const char* name, Point from, Point to, bool expectPolyline = true) {
    ConnectionType type = classifyConnection(from, to);
    bool isPolyline = isPolylineType(type);
    bool isBezier = isBezierType(type);
    
    std::cout << name << ": ";
    std::cout << "(" << from.x << "," << from.y << ") -> ";
    std::cout << "(" << to.x << "," << to.y << ") = ";
    std::cout << typeToString(type);
    
    if (expectPolyline && !isPolyline) {
        std::cout << " *** ERROR: Expected polyline but got bezier! ***";
    } else if (!expectPolyline && !isBezier) {
        std::cout << " *** ERROR: Expected bezier but got polyline! ***";
    } else {
        std::cout << " ✓";
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "=== Connection Routing Test ===" << std::endl;
    std::cout << "Forward connections (output LEFT, input RIGHT) should use BEZIER" << std::endl;
    std::cout << "Backward connections (output RIGHT, input LEFT) should use POLYLINES\n" << std::endl;
    
    std::cout << "--- Forward connections (should be BEZIER) ---" << std::endl;
    // All these should use bezier (NORMAL types)
    testForwardConnection("Forward horizontal", Point(100, 100), Point(300, 100), false);  // Expect bezier
    testForwardConnection("Forward slight down", Point(100, 100), Point(300, 120), false);  // Expect bezier
    testForwardConnection("Forward down", Point(100, 100), Point(300, 200), false);  // Expect bezier
    testForwardConnection("Forward up", Point(100, 100), Point(300, 50), false);  // Expect bezier
    testForwardConnection("Forward far horizontal", Point(100, 100), Point(500, 100), false);  // Expect bezier
    testForwardConnection("Forward far diagonal", Point(100, 100), Point(500, 300), false);  // Expect bezier
    
    std::cout << "\n--- Backward connections (should be POLYLINES) ---" << std::endl;
    // Test case from solve_this.png - backward connection should use polyline!
    testForwardConnection("Backward (like solve_this.png)", Point(400, 100), Point(100, 50), true);  // Expect polyline
    
    // All backward connections should use polylines
    testForwardConnection("Backward horizontal", Point(300, 100), Point(100, 100), true);  // Should be polyline
    testForwardConnection("Backward down", Point(300, 100), Point(100, 200), true);  // Should be polyline
    testForwardConnection("Backward up", Point(300, 100), Point(100, 50), true);  // Should be polyline
    testForwardConnection("Backward far", Point(500, 100), Point(100, 100), true);  // Should be polyline
    testForwardConnection("Backward diagonal", Point(400, 200), Point(100, 100), true);  // Should be polyline
    
    return 0;
}