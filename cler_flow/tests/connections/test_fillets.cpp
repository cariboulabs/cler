// Test to detect reverse fillets (wrong curvature direction) in polyline connections
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

// Calculate the midpoint used for quadratic bezier control point
Point midpoint(const Point& a, const Point& b) {
    return Point((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

struct TestResult {
    bool passed;
    std::string error;
};

TestResult testFilletCurvature(const char* name, Point p1, Point p2) {
    float zoom = 1.0f;
    float dHandle = 10.0f * zoom;
    float xMargin = dHandle * 0.8f;
    
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    
    ConnectionType type = classifyConnection(p1, p2);
    
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
    
    // Calculate approach points (FROM ACTUAL CODE)
    float yApproachLeft = (p1.y < yM) ? (yM - dHandle) : (yM + dHandle);
    float yLeaveRight = (p2.y < yM) ? (yM - dHandle) : (yM + dHandle);
    
    // Match EXACTLY what the actual code does for point[10] - AFTER FIX
    float yApproachDest = p2.y - yHandle;  // Note the minus!
    // For COMPLEX_OVER: yHandle is negative, so p2.y - (-dHandle) = p2.y + dHandle (below)
    // For COMPLEX_UNDER: yHandle is positive, so p2.y - (+dHandle) = p2.y - dHandle (above)
    // For COMPLEX_AROUND: yHandle is positive, so p2.y - (+dHandle) = p2.y - dHandle (above)
    
    // Build points array
    Point points[14] = {
        Point(p1.x, p1.y),                     // 0
        Point(x1, p1.y),                       // 1
        Point(x2, p1.y),                       // 2
        Point(x2, p1.y + yHandle),             // 3
        Point(x2, yApproachLeft),              // 4
        Point(x2, yM),                         // 5
        Point(x1, yM),                         // 6
        Point(x3, yM),                         // 7
        Point(x4, yM),                         // 8
        Point(x4, yLeaveRight),                // 9
        Point(x4, yApproachDest),              // 10
        Point(x4, p2.y),                       // 11
        Point(x3, p2.y),                       // 12
        Point(p2.x, p2.y)                      // 13
    };
    
    std::vector<std::string> errors;
    
    // Check each fillet for correct curvature
    // The drawing code uses quadratic beziers with midpoints as control points
    // For a proper inward curve (concave), the control point should be on the "inside" of the corner
    
    // Fillet 8: points[9] -> midpoint(9,10) -> points[10]
    // This is the critical one near the destination port
    {
        Point ctrl = midpoint(points[9], points[10]);
        
        // Determine expected curvature direction
        // We're going from point[9] to point[10], both at x4
        float y_movement = points[10].y - points[9].y;
        
        // After point[10], we go to point[11] which is at (x4, p2.y)
        float next_y_movement = points[11].y - points[10].y;
        
        // Check if this creates a proper corner
        // The fillet should curve INWARD (concave)
        
        // Key insight: if we're making a left turn (mathematically), 
        // the control point should be to the right of the path
        // if we're making a right turn, control point should be to the left
        
        // From segment [9->10] to segment [10->11]:
        // Both segments are vertical (same x), so we check the y direction change
        
        if (y_movement != 0 && next_y_movement != 0) {
            // Check if direction reverses (which would need a fillet)
            bool needs_fillet = (y_movement * next_y_movement < 0);
            
            if (!needs_fillet) {
                // Actually, for this fillet, we're going from vertical [8->9] to vertical [9->10]
                // Let me reconsider...
                
                // The fillet is actually between:
                // - Coming FROM: horizontal segment [8->9] where point[8] is at (x4, yM) and point[9] is at (x4, yLeaveRight)
                // - Going TO: vertical segment [9->10] where both are at x4
                
                // Wait, that's not right either. Points [8] and [9] have different x coordinates!
                // Let me check: point[8] is at (x4, yM) and point[9] is at (x4, yLeaveRight)
                // So [8->9] is actually vertical!
                
                // The actual fillet at index 8 connects:
                // - Segment [8->9]: vertical
                // - Segment [9->10]: vertical  
                // This shouldn't need a fillet if both are vertical...
                
                // Let me look at the actual bezier indices used in drawing
            }
        }
    }
    
    // Check fillet 9: points[10] -> midpoint(10,11) -> points[11]
    // This connects vertical [9->10] to horizontal [11->12]
    {
        // From point[10] at (x4, yApproachDest) 
        // To point[11] at (x4, p2.y)
        // Then to point[12] at (x3, p2.y)
        
        // Segment [10->11] is vertical (same x)
        // Segment [11->12] is horizontal (same y)
        // This IS a 90-degree corner that needs a fillet
        
        Point ctrl = midpoint(points[10], points[11]);
        
        // Check the direction of the turn
        float vert_dir = points[11].y - points[10].y;  // Vertical direction
        float horiz_dir = points[12].x - points[11].x; // Horizontal direction (should be positive, going right)
        
        // For the curve to be concave (inward), the control point should be at the corner
        // The midpoint IS at the corner for a 90-degree turn, which is correct
        
        // But wait, the issue might be in how the bezier is drawn!
        // If we're using points[10], ctrl, points[11] as the bezier points,
        // but the corner is actually at points[11], then the curve will be wrong!
        
        // The bezier should be: points[10], points[11], points[12] with points[11] as control
        // NOT: points[10], midpoint(10,11), points[11]
        
        // Let's check if this creates a reverse fillet
        if (std::abs(vert_dir) > 0.01f && std::abs(horiz_dir) > 0.01f) {
            // We have a corner. Now check if the fillet would be reversed
            
            // The midpoint between [10] and [11] is at ((x4+x4)/2, (yApproachDest+p2.y)/2)
            // = (x4, (yApproachDest+p2.y)/2)
            
            // For COMPLEX_UNDER: yApproachDest = p2.y + dHandle (below p2.y)
            // So midpoint.y = (p2.y + dHandle + p2.y)/2 = p2.y + dHandle/2
            // This is BELOW p2.y
            
            // But point[11] is at p2.y, so the control point is below the corner
            // This would create an OUTWARD (convex) curve when it should be INWARD (concave)
            
            if (polylineType == COMPLEX_UNDER && yApproachDest > p2.y) {
                errors.push_back("Fillet 9 (approach to input port): Control point below corner creates CONVEX curve (should be concave)");
            }
            if (polylineType == COMPLEX_OVER && yApproachDest < p2.y) {
                // For OVER, yApproachDest = p2.y + yHandle where yHandle is negative
                // So yApproachDest < p2.y (above)
                // Midpoint would be above p2.y, creating wrong curvature
                errors.push_back("Fillet 9 (approach to input port): Control point above corner creates CONVEX curve (should be concave)");
            }
        }
    }
    
    if (!errors.empty()) {
        std::string fullError = "\n";
        fullError += "  Type: " + std::string(typeToString(polylineType)) + "\n";
        fullError += "  From (" + std::to_string(p1.x) + ", " + std::to_string(p1.y) + 
                     ") to (" + std::to_string(p2.x) + ", " + std::to_string(p2.y) + ")\n";
        fullError += "  yApproachDest=" + std::to_string(yApproachDest) + ", p2.y=" + std::to_string(p2.y) + "\n";
        for (const auto& e : errors) {
            fullError += "  ERROR: " + e + "\n";
        }
        return {false, fullError};
    }
    
    return {true, ""};
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Fillet Curvature Test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    struct TestCase {
        const char* name;
        Point from;
        Point to;
    };
    
    std::vector<TestCase> testCases = {
        // The exact case from the screenshot
        {"Screenshot case: right to left-below", Point(350, 50), Point(150, 180)},
        
        // Backward connections that should trigger the issue
        {"Backward horizontal", Point(300, 100), Point(100, 100)},
        {"Backward 10px down", Point(300, 100), Point(100, 110)},
        {"Backward 30px down", Point(300, 100), Point(100, 130)},
        {"Backward 50px down", Point(300, 100), Point(100, 150)},
        {"Backward 100px down", Point(300, 100), Point(100, 200)},
        
        {"Backward 10px up", Point(300, 100), Point(100, 90)},
        {"Backward 30px up", Point(300, 100), Point(100, 70)},
        {"Backward 50px up", Point(300, 100), Point(100, 50)},
        {"Backward 100px up", Point(300, 100), Point(100, 0)},
    };
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& test : testCases) {
        TestResult result = testFilletCurvature(test.name, test.from, test.to);
        
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
        std::cout << "\nREVERSE FILLETS DETECTED! The bezier curve control points are wrong." << std::endl;
        return 1;
    } else {
        std::cout << "\nAll tests passed! No reverse fillets detected." << std::endl;
        return 0;
    }
}