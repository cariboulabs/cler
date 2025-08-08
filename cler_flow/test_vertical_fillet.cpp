// Test vertical connection fillets
#include <iostream>
#include <cmath>
#include <vector>

struct Point {
    float x, y;
    Point(float x_, float y_) : x(x_), y(y_) {}
};

Point midpoint(const Point& a, const Point& b) {
    return Point((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

void analyzeVerticalConnection() {
    // Simulating a vertical connection like in to_look2.png
    // Source at top, sink below, slight horizontal offset
    
    Point p1(200, 100);  // Output port
    Point p2(210, 300);  // Input port - slightly to the right and below
    
    float zoom = 1.0f;
    float dHandle = 10.0f * zoom;
    float xMargin = dHandle * 0.8f;
    
    float dx = p2.x - p1.x;  // 10 - small positive
    float dy = p2.y - p1.y;  // 200 - large positive
    
    std::cout << "Connection: (" << p1.x << "," << p1.y << ") -> (" << p2.x << "," << p2.y << ")\n";
    std::cout << "dx=" << dx << ", dy=" << dy << "\n\n";
    
    // This should be classified as NORMAL_VERTICAL or similar
    // For normal forward connections, no polyline needed
    
    // But if it's using polyline for some reason, let's trace the points
    
    // Calculate x coordinates
    float x1 = p1.x + xMargin;
    float x2 = x1 + dHandle;
    float x3 = p2.x - xMargin;
    float x4 = x3 - dHandle;
    
    // For a nearly vertical connection
    float yM = (p1.y + p2.y) * 0.5f;
    float yHandle = dHandle;  // Positive for normal routing
    
    // Build points
    Point points[14] = {
        Point(p1.x, p1.y),                     // 0: (200, 100)
        Point(x1, p1.y),                       // 1: (208, 100)
        Point(x2, p1.y),                       // 2: (218, 100)
        Point(x2, p1.y + yHandle),             // 3: (218, 110)
        Point(x2, yM - dHandle),               // 4: (218, 190)
        Point(x2, yM),                         // 5: (218, 200)
        Point(x1, yM),                         // 6: (208, 200)
        Point(x3, yM),                         // 7: (202, 200)
        Point(x4, yM),                         // 8: (192, 200)
        Point(x4, yM + dHandle),               // 9: (192, 210)
        Point(x4, p2.y - yHandle),             // 10: (192, 290)
        Point(x4, p2.y),                       // 11: (192, 300)
        Point(x3, p2.y),                       // 12: (202, 300)
        Point(p2.x, p2.y)                      // 13: (210, 300)
    };
    
    std::cout << "Key points in polyline:\n";
    for (int i = 0; i < 14; i++) {
        std::cout << "  [" << i << "]: (" << points[i].x << ", " << points[i].y << ")";
        if (i == 0) std::cout << " - Start";
        if (i == 3) std::cout << " - First elbow (output)";
        if (i == 5) std::cout << " - Middle";
        if (i == 10) std::cout << " - Approach input";
        if (i == 11) std::cout << " - At input y-level";
        if (i == 13) std::cout << " - End";
        std::cout << "\n";
    }
    
    std::cout << "\nBezier segments (as drawn):\n";
    
    // Segment 8: points[10] -> control -> points[12]
    // This is the problematic corner at the input
    std::cout << "\nSegment 8 (THE PROBLEM):\n";
    std::cout << "  From [10]: (" << points[10].x << ", " << points[10].y << ")\n";
    std::cout << "  To [12]: (" << points[12].x << ", " << points[12].y << ")\n";
    std::cout << "  Control: (" << points[10].x << ", " << points[11].y << ")\n";
    std::cout << "  = (" << points[10].x << ", " << p2.y << ")\n";
    
    // Check if this makes sense
    // We're going from point[10] which is vertical at x4, to point[12] which is horizontal at p2.y
    // But we're SKIPPING point[11]!
    
    std::cout << "\nWAIT! The bezier goes from [10] to [12], skipping [11]!\n";
    std::cout << "Point [11] at (" << points[11].x << ", " << points[11].y << ") is skipped!\n";
    std::cout << "This means we're trying to curve from vertical directly to horizontal\n";
    std::cout << "The control point at (" << points[10].x << ", " << points[11].y << ") = ";
    std::cout << "(" << x4 << ", " << p2.y << ") is at the corner\n";
    
    // The issue is that we're using the WRONG POINTS for the bezier!
    // It should probably be [10] -> [11] -> [12] with [11] as control
    // OR it should be drawn differently
}

int main() {
    std::cout << "=== Vertical Connection Fillet Analysis ===\n\n";
    analyzeVerticalConnection();
    return 0;
}