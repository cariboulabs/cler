// Comprehensive test to catch ALL reverse elbow cases
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

ConnectionType classifyConnection(Point p1, Point p2) {
    float zoom = 1.0f;
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float abs_dy = std::abs(dy);
    
    const float yMargin = 30.0f * zoom;
    const float nodeMargin = 20.0f * zoom;
    const float overlapThreshold = 40.0f * zoom;
    
    if (distance < 30.0f * zoom) {
        return STRAIGHT;
    }
    
    if (dx >= overlapThreshold) {
        if (abs_dy < dx * 0.7f) {
            return NORMAL;
        }
        return NORMAL_VERTICAL;
    }
    
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
    
    if (dx < 0) {
        if (abs_dy < yMargin * 2.0f) {
            return INVERTED_SIMPLE;
        }
        if (abs_dy > yMargin * 3.0f) {
            return (dy < 0) ? INVERTED_OVER : INVERTED_UNDER;
        }
        return INVERTED_MID;
    }
    
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

struct TestResult {
    bool passed;
    std::string error;
};

TestResult testPolylineElbows(const char* name, Point p1, Point p2, ConnectionType expectedType = ConnectionType(-1)) {
    float zoom = 1.0f;
    float dHandle = 10.0f * zoom;
    float xMargin = dHandle * 0.8f;
    
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    
    ConnectionType type = classifyConnection(p1, p2);
    
    // If we have an expected type and it doesn't match, fail
    if (expectedType != ConnectionType(-1) && type != expectedType) {
        return {false, std::string("Expected ") + typeToString(expectedType) + " but got " + typeToString(type)};
    }
    
    // Only test polyline types
    if (type != COMPLEX_OVER && type != COMPLEX_UNDER && type != COMPLEX_AROUND &&
        type != INVERTED_OVER && type != INVERTED_UNDER) {
        return {true, ""};  // Not a polyline, skip
    }
    
    ConnectionType polylineType = type;
    if (type == INVERTED_OVER) polylineType = COMPLEX_OVER;
    if (type == INVERTED_UNDER) polylineType = COMPLEX_UNDER;
    
    // Calculate x coordinates
    float x1 = p1.x + xMargin;
    float x2 = x1 + dHandle;
    float x3 = p2.x - xMargin;
    float x4 = x3 - dHandle;
    
    if (dx < 0) {
        float extend = std::max(xMargin * 1.5f, std::abs(dx) * 0.3f + xMargin);
        x1 = p1.x + extend;
        x2 = x1 + dHandle;
        x4 = p2.x - extend - dHandle;
        x3 = p2.x - extend;
    }
    
    // Calculate y coordinates
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
    
    // Calculate approach points
    float yApproachLeft = (p1.y < yM) ? (yM - dHandle) : (yM + dHandle);
    float yLeaveRight = (p2.y < yM) ? (yM - dHandle) : (yM + dHandle);
    
    // Match EXACTLY what the actual code does
    float yApproachDest;
    if (type == COMPLEX_OVER || type == INVERTED_OVER) {
        yApproachDest = p2.y + yHandle;  // yHandle is negative
    } else {
        yApproachDest = p2.y + std::abs(yHandle);  // Always positive
    }
    
    // Build points array
    struct { float x, y; } points[14] = {
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
        {x4, yApproachDest},              // 10 - approach dest
        {x4, p2.y},                       // 11
        {x3, p2.y},                       // 12
        {p2.x, p2.y}                      // 13
    };
    
    // Validate elbows
    std::vector<std::string> errors;
    
    // Check first elbow (points 2->3)
    float firstElbow = points[3].y - points[2].y;
    if (polylineType == COMPLEX_OVER) {
        if (firstElbow > 0) {
            errors.push_back("First elbow: goes DOWN but should go UP for OVER routing");
        }
    } else if (polylineType == COMPLEX_UNDER || polylineType == COMPLEX_AROUND) {
        if (firstElbow < 0) {
            errors.push_back("First elbow: goes UP but should go DOWN for UNDER routing");
        }
    }
    
    // Check approach to middle (points 4->5)
    float approachMiddle = points[5].y - points[4].y;
    if (p1.y < yM && approachMiddle < 0) {
        errors.push_back("Approach middle: coming from above but going up (reverse!)");
    } else if (p1.y > yM && approachMiddle > 0) {
        errors.push_back("Approach middle: coming from below but going down (reverse!)");
    }
    
    // Check leave middle (points 8->9)
    float leaveMiddle = points[9].y - points[8].y;
    if (p2.y < yM && leaveMiddle > 0) {
        errors.push_back("Leave middle: going to above but moving down (reverse!)");
    } else if (p2.y > yM && leaveMiddle < 0) {
        errors.push_back("Leave middle: going to below but moving up (reverse!)");
    }
    
    // Check last elbow (points 10->11)
    float lastElbow = points[11].y - points[10].y;
    
    // THIS IS THE KEY CHECK - the last elbow must make geometric sense
    if (polylineType == COMPLEX_OVER) {
        // For OVER routing, we should approach from above
        // So point[10] should be above point[11] (p2.y)
        if (points[10].y > p2.y) {
            errors.push_back("Last elbow: approaching from BELOW for OVER routing (REVERSE!)");
        }
        // The elbow direction should be positive (going down to reach port)
        if (lastElbow < 0) {
            errors.push_back("Last elbow: going UP to reach port from above (REVERSE!)");
        }
    } else if (polylineType == COMPLEX_UNDER || polylineType == COMPLEX_AROUND) {
        // For UNDER routing, we should approach from below
        // So point[10] should be below point[11] (p2.y)
        if (points[10].y < p2.y) {
            errors.push_back("Last elbow: approaching from ABOVE for UNDER routing (REVERSE!)");
        }
        // The elbow direction should be negative (going up to reach port)
        if (lastElbow > 0) {
            errors.push_back("Last elbow: going DOWN to reach port from below (REVERSE!)");
        }
    }
    
    if (!errors.empty()) {
        std::string fullError = "\n";
        fullError += "  Type: " + std::string(typeToString(polylineType)) + "\n";
        fullError += "  From (" + std::to_string(p1.x) + ", " + std::to_string(p1.y) + 
                     ") to (" + std::to_string(p2.x) + ", " + std::to_string(p2.y) + ")\n";
        fullError += "  yHandle=" + std::to_string(yHandle) + ", yM=" + std::to_string(yM) + "\n";
        fullError += "  point[10].y=" + std::to_string(points[10].y) + ", p2.y=" + std::to_string(p2.y) + "\n";
        for (const auto& e : errors) {
            fullError += "  ERROR: " + e + "\n";
        }
        return {false, fullError};
    }
    
    return {true, ""};
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Comprehensive Reverse Elbow Test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    struct TestCase {
        const char* name;
        Point from;
        Point to;
        ConnectionType expectedType;
    };
    
    std::vector<TestCase> testCases = {
        // The exact case from the screenshot - source on right going to sink on left-below
        {"Screenshot case: right to left-below", Point(350, 50), Point(150, 180), ConnectionType(-1)},
        
        // All backward cases that might use polylines
        {"Backward horizontal", Point(300, 100), Point(100, 100), ConnectionType(-1)},
        {"Backward 10px down", Point(300, 100), Point(100, 110), ConnectionType(-1)},
        {"Backward 30px down", Point(300, 100), Point(100, 130), ConnectionType(-1)},
        {"Backward 50px down", Point(300, 100), Point(100, 150), ConnectionType(-1)},
        {"Backward 70px down", Point(300, 100), Point(100, 170), ConnectionType(-1)},
        {"Backward 100px down", Point(300, 100), Point(100, 200), ConnectionType(-1)},
        {"Backward 150px down", Point(300, 100), Point(100, 250), ConnectionType(-1)},
        
        {"Backward 10px up", Point(300, 100), Point(100, 90), ConnectionType(-1)},
        {"Backward 30px up", Point(300, 100), Point(100, 70), ConnectionType(-1)},
        {"Backward 50px up", Point(300, 100), Point(100, 50), ConnectionType(-1)},
        {"Backward 100px up", Point(300, 100), Point(100, 0), ConnectionType(-1)},
        
        // Forward overlap cases
        {"Forward overlap 20px", Point(100, 100), Point(120, 150), ConnectionType(-1)},
        {"Forward overlap 30px", Point(100, 100), Point(130, 150), ConnectionType(-1)},
        {"Forward overlap up", Point(100, 100), Point(130, 50), ConnectionType(-1)},
        
        // Vertical cases
        {"Vertical down", Point(100, 100), Point(100, 250), ConnectionType(-1)},
        {"Vertical up", Point(100, 100), Point(100, 0), ConnectionType(-1)},
        {"Nearly vertical down", Point(100, 100), Point(110, 250), ConnectionType(-1)},
        {"Nearly vertical up", Point(100, 100), Point(110, 0), ConnectionType(-1)},
    };
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& test : testCases) {
        TestResult result = testPolylineElbows(test.name, test.from, test.to, test.expectedType);
        
        if (result.passed) {
            std::cout << "✓ " << test.name << std::endl;
            passed++;
        } else {
            std::cout << "✗ " << test.name << result.error << std::endl;
            failed++;
        }
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    
    if (failed > 0) {
        std::cout << "\nREVERSE ELBOWS DETECTED! The polyline routing has bugs." << std::endl;
        return 1;
    } else {
        std::cout << "\nAll tests passed! No reverse elbows detected." << std::endl;
        return 0;
    }
}