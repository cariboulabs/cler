# SPSC Queue Tests

Comprehensive test suite for the CLER SPSC (Single Producer Single Consumer) Queue implementation.

## Overview

The SPSC Queue is a high-performance lock-free circular buffer designed for single-producer, single-consumer scenarios. This test suite verifies:

- **Basic Operations**: `push`/`pop`, `try_push`/`try_pop`, `emplace`, `force_push`/`force_pop`
- **Batch Operations**: `writeN`/`readN` for bulk data transfer  
- **Peek/Commit Pattern**: `peek_write`/`commit_write` and `peek_read`/`commit_read`
- **Bounds Safety**: Memory safety with different queue sizes and data types
- **Data Integrity**: No sample loss verification in concurrent scenarios
- **Wraparound Behavior**: Proper handling of circular buffer wraparound

## Test Files

- **`test_spsc_basic.cpp`**: Basic push/pop operations, wraparound, concurrent safety
- **`test_spsc_batch.cpp`**: Batch operations (writeN/readN), peek/commit patterns, mixed operations
- **`test_spsc_bounds.cpp`**: Bounds checking, capacity limits, memory safety, large data sizes

## Building the Tests

### Prerequisites

- CMake 3.16 or newer
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Internet connection (for downloading Google Test)

### Build Options

From the project root directory:

```bash
# Enable tests in CMake configuration
cmake -B build -DCLER_BUILD_TESTS=ON

# Build the project including tests
cmake --build build

# Alternative: Enable tests with other CLER components
cmake -B build -DCLER_BUILD_TESTS=ON -DCLER_BUILD_EXAMPLES=ON -DCLER_BUILD_BLOCKS=ON

# Build only the tests
cmake --build build --target spsc_basic_test spsc_batch_test spsc_bounds_test
```

### Test Executables

After building, the following test executables are created in `build/tests/spsc-queue/`:

- `spsc_basic_test` - Basic operations tests
- `spsc_batch_test` - Batch and peek/commit tests  
- `spsc_bounds_test` - Bounds and safety tests
- `spsc_all_tests` - Combined test runner

## Running the Tests

### Individual Test Suites

```bash
# Run basic operations tests
./build/tests/spsc-queue/spsc_basic_test

# Run batch operations tests  
./build/tests/spsc-queue/spsc_batch_test

# Run bounds safety tests
./build/tests/spsc-queue/spsc_bounds_test

# Run all tests in one executable
./build/tests/spsc-queue/spsc_all_tests
```

### Using CTest

```bash
# Run all SPSC tests via CTest
cd build && ctest -R "spsc_" --output-on-failure

# Run with verbose output
cd build && ctest -R "spsc_" --output-on-failure --verbose

# Run specific test
cd build && ctest -R "spsc_basic_test" --output-on-failure
```

### Using Make Targets

```bash
# From build directory
make run_spsc_tests                # Run all SPSC tests
make run_spsc_tests_verbose       # Run with verbose output
make spsc_performance_test        # Run performance-focused tests  
make spsc_memory_test            # Run memory safety tests
make run_all_cler_tests          # Run all CLER tests
```

### Google Test Filters

Run specific test cases using Google Test filters:

```bash
# Run only no sample loss tests
./build/tests/spsc-queue/spsc_basic_test --gtest_filter="*NoSampleLoss*"

# Run only concurrent tests
./build/tests/spsc-queue/spsc_basic_test --gtest_filter="*Concurrent*"

# Run bounds tests with specific capacity
./build/tests/spsc-queue/spsc_bounds_test --gtest_filter="*ExactCapacity*"

# List all available tests
./build/tests/spsc-queue/spsc_basic_test --gtest_list_tests
```

## Key Test Scenarios

### Data Integrity Tests
- **Sequential Operations**: Verify order preservation in single-threaded scenarios
- **Concurrent Operations**: Producer/consumer threads with order verification
- **Batch Operations**: Bulk data transfer without sample loss
- **Mixed Operations**: Combination of single and batch operations

### Performance Tests  
- **High Throughput**: 10,000+ items with 64-element queue
- **Large Batches**: 1000 batches of 50 items each
- **Concurrent Stress**: Multiple producer/consumer cycles

### Bounds Safety Tests
- **Exact Capacity**: Fill queue to exact capacity (512, 1024+ elements)
- **Large Data Types**: Structs, aligned types, variable sizes
- **Wraparound**: Circular buffer wraparound with large datasets
- **Memory Safety**: No buffer overruns or segmentation faults

### Edge Cases
- **Minimum Capacity**: Single-element queues
- **Full Queue Operations**: Operations when queue is full
- **Empty Queue Operations**: Operations when queue is empty  
- **Partial Operations**: writeN/readN with limited space/data

## Queue Features Tested

### Allocation Types
- **Heap Allocation**: `SPSCQueue<T> queue(size)` - Dynamic sizing
- **Stack Allocation**: `SPSCQueue<T, N> queue(0)` - Compile-time sizing

### Operation Types
- **Blocking**: `push()`, `pop()` - Wait for space/data
- **Non-blocking**: `try_push()`, `try_pop()` - Return immediately  
- **Overwriting**: `force_push()` - Write without checking space
- **Bulk**: `writeN()`, `readN()` - Transfer multiple elements
- **Zero-copy**: `peek_write()`/`commit_write()`, `peek_read()`/`commit_read()`

### Data Types Tested
- **Primitives**: `int`, `double`, `char`
- **Containers**: `std::string`, `std::vector`
- **Structs**: Custom structs with various sizes
- **Aligned Types**: Cache-line aligned structures

## Troubleshooting

### Build Issues

**CMake can't find Google Test:**
```bash
# Clear CMake cache and rebuild
rm -rf build
cmake -B build -DCLER_BUILD_TESTS=ON
```

**Compiler errors:**
```bash
# Ensure C++17 support
cmake -B build -DCLER_BUILD_TESTS=ON -DCMAKE_CXX_STANDARD=17
```

### Test Failures

**Concurrent tests fail:**
- System may be under high load
- Try running tests individually: `./spsc_basic_test --gtest_filter="*Sequential*"`

**Memory tests fail:**
- May indicate actual memory safety issues
- Run with sanitizers: `cmake -DCMAKE_CXX_FLAGS="-fsanitize=address"`

**Performance tests timeout:**
- Reduce test parameters or run on less loaded system
- Focus on correctness tests: `--gtest_filter="*NoSampleLoss*"`

## Integration

### Adding Custom Tests

Create new test files following the pattern:

```cpp
#include <gtest/gtest.h>
#include "../../include/cler_spsc-queue.hpp"

class MyCustomTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}  
};

TEST_F(MyCustomTest, MyTestCase) {
    dro::SPSCQueue<int> queue(10);
    // Test implementation
    EXPECT_TRUE(condition);
}
```

Add to `CMakeLists.txt`:
```cmake
add_spsc_test(my_custom_test test_my_custom.cpp)
```

### Continuous Integration

For CI/CD pipelines:

```bash
# Configure with tests enabled
cmake -B build -DCLER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release

# Build and run tests
cmake --build build --parallel
cd build && ctest --output-on-failure --timeout 60
```

## Performance Expectations

### Typical Performance (indicative)
- **Single operations**: ~10-50 ns per push/pop
- **Batch operations**: ~5-20 ns per element  
- **Memory usage**: Queue capacity + padding for cache alignment
- **Concurrency**: True lock-free operation between single producer/consumer

### Optimization Notes
- Tests run with `-O2` optimization by default
- Cache-line padding prevents false sharing
- Trivially copyable types use `memcpy` for batch operations
- Tests verify performance doesn't regress with optimization changes

## License

These tests are part of the CLER project. See the main project LICENSE file for details.