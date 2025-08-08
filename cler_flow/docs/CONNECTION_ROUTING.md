# Connection Routing System

## Overview
The CLER Flow editor uses a sophisticated connection routing system that provides clean, visually appealing connections between nodes in the flowgraph.

## Routing Rules

### Forward Connections (Output Left → Input Right)
- **Method**: Smooth Bezier curves
- **Types**: `NORMAL`, `NORMAL_VERTICAL`
- **Appearance**: Direct, flowing left-to-right connections
- **Use Case**: Standard signal flow direction

### Backward Connections (Output Right → Input Left)
- **Method**: Polyline routing with rounded corners (fillets)
- **Types**: `INVERTED_OVER`, `INVERTED_UNDER`
- **Appearance**: Routes around blocks to avoid overlap
- **Use Case**: Feedback loops, backward connections

## Key Parameters
All constants are defined in `flow_canvas.hpp` for easy tuning:

- `BASE_FILLET_RADIUS`: 10px - Radius for rounded corners
- `BACKWARD_MIN_EXTEND`: 7px - Clearance from block edges
- `BASE_Y_MARGIN`: 30px - Vertical clearance threshold
- `BACKWARD_DYNAMIC_FACTOR`: 0.02 - Dynamic extension factor

## Architecture

### Classification (`ClassifyConnection`)
Determines the routing type based on port positions:
1. Calculates dx (horizontal distance) and dy (vertical distance)
2. If dx > 0: Forward connection → Use Bezier
3. If dx < 0: Backward connection → Use Polyline

### Drawing Methods

#### `DrawBezierConnection`
- Creates smooth cubic Bezier curves
- Handles control points for natural flow
- Used for all forward connections

#### `DrawPolylineConnection`
- Creates 14-point polyline path
- Routes around blocks with proper clearance
- Maintains 7px distance from block edges
- Used for all backward connections

#### `DrawPolylineSegments`
- Renders polyline with rounded corners
- Uses `AddBezierQuadratic` for each segment
- Includes shadow rendering for depth

## Testing
Comprehensive test suite in `tests/connections/`:
- `test_forward_polyline` - Verifies routing rules (forward=bezier, backward=polyline)
- `test_fillets` - Tests corner rendering and fillet curvature
- `test_horizontal_routing` - Tests horizontally-aligned connections
- `test_all_elbows` - Verifies all elbows have correct curvature
- `test_elbows` - Tests elbow direction in polylines
- `test_elbows_with_nodes` - Tests routing with node information
- `test_vertical_fillet` - Tests vertical connection fillets

### Running Tests with CTest

Run all tests:
```bash
cd build
ctest
```

Run with detailed output:
```bash
ctest --output-on-failure
```

Run specific test:
```bash
ctest -R test_forward_polyline -V
```

Run all connection routing tests:
```bash
make test_connections
```

List all available tests:
```bash
ctest -N
```

## Visual Features
- Automatic shadow rendering for visual depth
- Zoom-aware scaling for consistent appearance
- Smooth fillets at all corners
- Tight routing close to blocks (7px clearance)