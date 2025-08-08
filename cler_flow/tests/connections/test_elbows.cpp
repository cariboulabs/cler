// Comprehensive test program to verify elbow directions for polyline connections
#include <iostream>
#include <cmath>
#include <string>
#include <vector>

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

ConnectionType classifyConnection(Point p1, Point p2) {
    float zoom = 1.0f;
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float abs_dy = std::abs(dy);
    
    const float yMargin = 30.0f * zoom;
    const float nodeMargin = 20.0f * zoom;
    const float overlapThreshold = 40.0f * zoom;
    
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
    
    // Forward but overlapping
    if (dx > 0 && dx < overlapThreshold) {
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

void testElbowDirection(const char* name, Point p1, Point p2) {
    float zoom = 1.0f;
    float dHandle = 10.0f * zoom;
    float xMargin = dHandle * 0.8f;
    
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    
    // Classify the connection
    ConnectionType type = classifyConnection(p1, p2);
    
    // Only test polyline types
    if (type != COMPLEX_OVER && type != COMPLEX_UNDER && type != COMPLEX_AROUND &&
        type != INVERTED_OVER && type != INVERTED_UNDER) {
        std::cout << "\n=== " << name << " ===" << std::endl;
        std::cout << "Type: " << typeToString(type) << " (not polyline, skipping)" << std::endl;
        return;
    }
    
    // For INVERTED types, map to COMPLEX for polyline
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
    
    // Calculate y coordinate for middle section and handle direction
    float yM = (p1.y + p2.y) * 0.5f;
    float yHandle = dHandle;
    
    if (polylineType == COMPLEX_OVER) {
        yHandle = -dHandle;  // Negative to go up
        if (std::abs(dy) < xMargin * 2) {
            yM = std::min(p1.y, p2.y) - xMargin;
        }
    } else if (polylineType == COMPLEX_UNDER) {
        yHandle = dHandle;  // Positive to go down
        if (std::abs(dy) < xMargin * 2) {
            yM = std::max(p1.y, p2.y) + xMargin;
        }
    } else if (polylineType == COMPLEX_AROUND) {
        yHandle = dHandle;  // Positive to go down
        float node_bottom = std::max(p1.y, p2.y);
        yM = node_bottom + xMargin * 2;
    }
    
    // Build points with approach logic
    float yApproachLeft = (p1.y < yM) ? (yM - dHandle) : (yM + dHandle);
    float yLeaveRight = (p2.y < yM) ? (yM - dHandle) : (yM + dHandle);
    
    Point points[] = {
        Point(p1.x, p1.y),                     // 0
        Point(x1, p1.y),                        // 1
        Point(x2, p1.y),                        // 2
        Point(x2, p1.y + yHandle),              // 3 - first vertical segment
        Point(x2, yApproachLeft),               // 4 - approach middle
        Point(x2, yM),                          // 5 - middle left
        Point(x1, yM),                          // 6
        Point(x3, yM),                          // 7
        Point(x4, yM),                          // 8 - middle right
        Point(x4, yLeaveRight),                 // 9 - leave middle
        Point(x4, p2.y - yHandle),              // 10 - approach output
        Point(x4, p2.y),                        // 11
        Point(x3, p2.y),                        // 12
        Point(p2.x, p2.y)                       // 13
    };
    
    std::cout << "\n=== " << name << " ===" << std::endl;
    std::cout << "From (" << p1.x << ", " << p1.y << ") to (" << p2.x << ", " << p2.y << ")" << std::endl;
    std::cout << "dx=" << dx << ", dy=" << dy << std::endl;
    std::cout << "Type: " << typeToString(type) << " -> Polyline: " << typeToString(polylineType) << std::endl;
    std::cout << "yHandle: " << yHandle << ", yM: " << yM << std::endl;
    
    bool hasError = false;
    
    // Check first elbow (from horizontal to vertical at output)
    float firstElbowDir = points[3].y - points[2].y;
    std::cout << "First elbow (output side): point[3].y - point[2].y = " << firstElbowDir;
    
    // For COMPLEX_OVER, we expect to go UP (negative)
    // For COMPLEX_UNDER and COMPLEX_AROUND, we expect to go DOWN (positive)
    bool firstElbowCorrect = true;
    if (polylineType == COMPLEX_OVER) {
        if (firstElbowDir > 0) {
            std::cout << " *** REVERSE ELBOW! Should go UP (negative) ***";
            firstElbowCorrect = false;
            hasError = true;
        } else {
            std::cout << " (OK - going up)";
        }
    } else {
        if (firstElbowDir < 0) {
            std::cout << " *** REVERSE ELBOW! Should go DOWN (positive) ***";
            firstElbowCorrect = false;
            hasError = true;
        } else {
            std::cout << " (OK - going down)";
        }
    }
    std::cout << std::endl;
    
    // Check the approach to middle
    float approachDir = points[5].y - points[4].y;
    std::cout << "Approach to middle: point[5].y - point[4].y = " << approachDir;
    
    // We should approach from the correct direction
    if (p1.y < yM && approachDir < 0) {
        std::cout << " *** REVERSE! Coming from above but going up ***";
        hasError = true;
    } else if (p1.y > yM && approachDir > 0) {
        std::cout << " *** REVERSE! Coming from below but going down ***";
        hasError = true;
    } else {
        std::cout << " (OK)";
    }
    std::cout << std::endl;
    
    // Check leaving middle
    float leaveDir = points[9].y - points[8].y;
    std::cout << "Leave middle: point[9].y - point[8].y = " << leaveDir;
    
    // We should leave toward the correct direction
    if (p2.y < yM && leaveDir > 0) {
        std::cout << " *** REVERSE! Going up but moving down ***";
        hasError = true;
    } else if (p2.y > yM && leaveDir < 0) {
        std::cout << " *** REVERSE! Going down but moving up ***";
        hasError = true;
    } else {
        std::cout << " (OK)";
    }
    std::cout << std::endl;
    
    // Check last elbow (approaching input port)
    float lastElbowDir = points[11].y - points[10].y;
    std::cout << "Last elbow (input side): point[11].y - point[10].y = " << lastElbowDir;
    
    // For COMPLEX_OVER, if we're coming from above, we should go down to reach port
    // For COMPLEX_UNDER, if we're coming from below, we should go up to reach port
    bool lastElbowCorrect = true;
    if (polylineType == COMPLEX_OVER) {
        if (p2.y > yM && lastElbowDir < 0) {
            std::cout << " *** REVERSE ELBOW! Should approach from above ***";
            lastElbowCorrect = false;
            hasError = true;
        } else {
            std::cout << " (OK)";
        }
    } else if (polylineType == COMPLEX_UNDER) {
        if (p2.y < yM && lastElbowDir > 0) {
            std::cout << " *** REVERSE ELBOW! Should approach from below ***";
            lastElbowCorrect = false;
            hasError = true;
        } else {
            std::cout << " (OK)";
        }
    } else {
        std::cout << " (OK)";
    }
    std::cout << std::endl;
    
    if (hasError) {
        std::cout << "***** ERRORS DETECTED IN THIS CONFIGURATION *****" << std::endl;
    }
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "Comprehensive Elbow Direction Test" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    // Test forward connections
    std::cout << "\n--- FORWARD CONNECTIONS (L->R) ---" << std::endl;
    testElbowDirection("Forward horizontal", Point(100, 100), Point(200, 100));
    testElbowDirection("Forward slight down", Point(100, 100), Point(200, 120));
    testElbowDirection("Forward down", Point(100, 100), Point(200, 200));
    testElbowDirection("Forward slight up", Point(100, 100), Point(200, 80));
    testElbowDirection("Forward up", Point(100, 100), Point(200, 0));
    
    // Test backward connections (the problematic ones)
    std::cout << "\n--- BACKWARD CONNECTIONS (R->L) ---" << std::endl;
    testElbowDirection("Backward horizontal", Point(200, 100), Point(100, 100));
    testElbowDirection("Backward slight down", Point(200, 100), Point(100, 120));
    testElbowDirection("Backward down", Point(200, 100), Point(100, 200));
    testElbowDirection("Backward slight up", Point(200, 100), Point(100, 80));
    testElbowDirection("Backward up", Point(200, 100), Point(100, 0));
    
    // Test the specific problematic case: output to the left and slightly below
    std::cout << "\n--- PROBLEMATIC CASES (output left and slightly below) ---" << std::endl;
    testElbowDirection("Left and 10px below", Point(200, 100), Point(100, 110));
    testElbowDirection("Left and 20px below", Point(200, 100), Point(100, 120));
    testElbowDirection("Left and 30px below", Point(200, 100), Point(100, 130));
    testElbowDirection("Left and 40px below", Point(200, 100), Point(100, 140));
    testElbowDirection("Left and 50px below", Point(200, 100), Point(100, 150));
    
    // Test slight overlap cases
    std::cout << "\n--- SLIGHT OVERLAP CASES ---" << std::endl;
    testElbowDirection("Small forward overlap down", Point(100, 100), Point(130, 150));
    testElbowDirection("Small forward overlap up", Point(100, 100), Point(130, 50));
    
    // Test nearly vertical
    std::cout << "\n--- NEARLY VERTICAL ---" << std::endl;
    testElbowDirection("Vertical down", Point(100, 100), Point(100, 200));
    testElbowDirection("Vertical up", Point(100, 100), Point(100, 0));
    testElbowDirection("Nearly vertical down", Point(100, 100), Point(110, 200));
    testElbowDirection("Nearly vertical up", Point(100, 100), Point(110, 0));
    
    return 0;
}