// Test for horizontally-aligned backward connections
// These should route AROUND the blocks, not between them
#include <iostream>
#include <cmath>
#include <vector>

struct Point {
    float x, y;
    Point(float x_, float y_) : x(x_), y(y_) {}
};

// Simulate block bounds
struct Block {
    Point pos;
    Point size;
    Block(float x, float y, float w, float h) : pos(x, y), size(w, h) {}
    
    float top() const { return pos.y; }
    float bottom() const { return pos.y + size.y; }
    float left() const { return pos.x; }
    float right() const { return pos.x + size.x; }
};

void testHorizontalRouting() {
    // Test case from to_look4.png - horizontally aligned blocks
    Block leftBlock(50, 100, 150, 50);   // Gain1 block
    Block rightBlock(300, 100, 150, 50); // Gain block
    
    // Ports
    Point outputPort(leftBlock.right(), leftBlock.pos.y + 25);  // Output on right
    Point inputPort(rightBlock.left(), rightBlock.pos.y + 25);   // Input on left
    
    std::cout << "=== Horizontally Aligned Backward Connection Test ===\n\n";
    std::cout << "Left block: (" << leftBlock.left() << "," << leftBlock.top() 
              << ") to (" << leftBlock.right() << "," << leftBlock.bottom() << ")\n";
    std::cout << "Right block: (" << rightBlock.left() << "," << rightBlock.top() 
              << ") to (" << rightBlock.right() << "," << rightBlock.bottom() << ")\n";
    std::cout << "Output port: (" << outputPort.x << "," << outputPort.y << ")\n";
    std::cout << "Input port: (" << inputPort.x << "," << inputPort.y << ")\n\n";
    
    // This is a backward connection (output.x > input.x)
    float dx = inputPort.x - outputPort.x;
    float dy = inputPort.y - outputPort.y;
    
    std::cout << "dx = " << dx << " (negative = backward)\n";
    std::cout << "dy = " << dy << " (zero = perfectly aligned)\n\n";
    
    // For polyline routing
    float zoom = 1.0f;
    float dHandle = 10.0f * zoom;
    float xMargin = dHandle * 0.8f;
    
    // Extension from blocks (should be small - 7 pixels)
    float extend = 7.0f * zoom + std::abs(dx) * 0.02f;
    
    float x1 = outputPort.x + extend;
    float x2 = x1 + dHandle;
    float x4 = inputPort.x - extend - dHandle;
    float x3 = inputPort.x - extend;
    
    std::cout << "Vertical line positions:\n";
    std::cout << "  From output: x = " << x2 << " (at " << (x2 - outputPort.x) << " pixels from port)\n";
    std::cout << "  To input: x = " << x4 << " (at " << (inputPort.x - x4) << " pixels from port)\n\n";
    
    // Calculate middle line position
    // For COMPLEX_UNDER (routing below), yM should be BELOW both blocks
    float blockBottom = std::max(leftBlock.bottom(), rightBlock.bottom());
    float yM_correct = blockBottom + xMargin * 2;  // Below both blocks
    
    // What the current code might do (midpoint - WRONG for aligned blocks)
    float yM_wrong = (outputPort.y + inputPort.y) * 0.5f;
    
    std::cout << "Middle horizontal line (yM) position:\n";
    std::cout << "  WRONG (between blocks): yM = " << yM_wrong << "\n";
    std::cout << "  CORRECT (below blocks): yM = " << yM_correct << "\n";
    std::cout << "  Block bottom = " << blockBottom << "\n\n";
    
    // Check if the routing goes around or through
    if (yM_wrong >= leftBlock.top() && yM_wrong <= leftBlock.bottom()) {
        std::cout << "ERROR: Middle line at " << yM_wrong << " goes THROUGH the blocks!\n";
    }
    
    if (yM_correct > blockBottom) {
        std::cout << "CORRECT: Middle line at " << yM_correct << " goes BELOW the blocks (around them)\n";
    }
    
    // Test fillet directions
    std::cout << "\n=== Fillet Direction Test ===\n";
    
    // For COMPLEX_UNDER routing
    float yHandle = dHandle;  // Positive for going down
    
    // Point 3: First elbow from output
    float p3_y = outputPort.y + yHandle;
    std::cout << "Point 3 (first elbow): y = " << p3_y << " (should be below output for UNDER routing)\n";
    
    // Check approach to destination
    float yApproachDest = inputPort.y - yHandle;  // This is the fixed formula
    std::cout << "Point 10 (approach to input): y = " << yApproachDest << "\n";
    
    // The fillet at the input should curve properly
    if (yApproachDest < inputPort.y && yHandle > 0) {
        std::cout << "CORRECT: Approaching from above (y=" << yApproachDest << ") to port (y=" << inputPort.y << ")\n";
    } else {
        std::cout << "ERROR: Wrong approach direction for fillet!\n";
    }
}

int main() {
    testHorizontalRouting();
    
    std::cout << "\n=== Test with slight vertical offset ===\n";
    // Test with slight offset
    Block leftBlock2(50, 100, 150, 50);
    Block rightBlock2(300, 110, 150, 50);  // 10 pixels lower
    
    Point outputPort2(leftBlock2.right(), leftBlock2.pos.y + 25);
    Point inputPort2(rightBlock2.left(), rightBlock2.pos.y + 25);
    
    float dy2 = inputPort2.y - outputPort2.y;
    std::cout << "dy = " << dy2 << " (small positive offset)\n";
    
    // Should still route around (below) both blocks
    float blockBottom2 = std::max(leftBlock2.bottom(), rightBlock2.bottom());
    float yM_correct2 = blockBottom2 + 8.0f * 2;  // Below both blocks
    
    std::cout << "Block bottoms: left=" << leftBlock2.bottom() << ", right=" << rightBlock2.bottom() << "\n";
    std::cout << "Correct yM (below both): " << yM_correct2 << "\n";
    
    return 0;
}