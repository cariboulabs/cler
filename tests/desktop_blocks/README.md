# Desktop Blocks Testing

Comprehensive test suite for CLER desktop blocks ensuring correctness and reliability.

## Testing Philosophy

**"Place inputs, and then equate on the outputs"** - Tests verify not just execution but mathematical and functional correctness:

- **Input-Output Verification**: Test data flows through blocks correctly
- **Mathematical Correctness**: Verify operations produce expected results
- **Edge Case Handling**: Test empty inputs, error conditions, boundary cases
- **Data Integrity**: Ensure samples arrive one-to-one without loss or corruption
- **Performance Validation**: Verify blocks work with doubly-mapped buffers (dbf)

## Current Test Status

### ✅ Completed Tests
- **Math Blocks** (`test_math_blocks.cpp`)
  - `AddBlock`: Float/complex addition with multiple inputs
  - `GainBlock`: Scalar multiplication for float/complex
  - `ComplexToMagPhaseBlock`: Complex demux (MagPhase/RealImag modes)
  - Status: All tests passing

- **Utility Blocks** (`test_utility_blocks.cpp`)
  - `FanoutBlock`: Multi-output duplication (2/4 outputs, float/complex)
  - `ThrottleBlock`: Sample rate throttling with timing validation
  - `ThroughputBlock`: Passthrough with sample counting
  - Status: Core functionality tested, timing tests working

- **Sink Blocks** (`test_sink_blocks.cpp`)
  - `SinkFileBlock`: File writing with readback verification
  - `SinkNullBlock`: Null sink (callback issues noted)
  - Status: File sink working, null sink needs investigation

- **Noise Blocks** (`test_noise_blocks.cpp`)
  - `NoiseAWGNBlock`: AWGN generation with statistical validation
  - Status: Tests created, statistical properties verified

- **Resampler Blocks** (`test_resampler_blocks.cpp`)
  - `MultiStageResamplerBlock`: Up/down sampling, float/complex
  - Status: Working with dbf fix, comprehensive ratio testing

- **Channelizer Blocks** (`test_channelizer_blocks.cpp`)
  - `PolyphaseChannelizerBlock`: Multi-channel frequency separation
  - Tests: 2/4/8 channel configurations, frequency separation, error conditions
  - Status: All tests passing, dbf compatibility verified

### 🚀 Implementation Complete - All Major Block Types Tested

#### Skipped Blocks (By Design)
1. **UDP Network Blocks** (`desktop_blocks/udp/`)
   - **Files**: `sink_udp.hpp`, `source_udp.hpp`
   - **Reason**: Complex network blocks requiring UDP server setup, socket management, and network error handling
   - **Complexity**: Would need mock servers, network timeouts, and platform-specific socket testing
   - **Status**: Deferred - requires network infrastructure for proper testing

2. **Hardware Source Blocks** (Various directories)
   - **Examples**: SDR sources, audio device sources, hardware-specific interfaces
   - **Reason**: Hardware-dependent blocks cannot be tested in CI/CD environment
   - **Status**: Skipped as planned

## Known Issues & Investigations

1. **SinkNullBlock Callback**: Double-free error in callback mechanism
2. **Timing Tests**: Platform-dependent timing variations in ThrottleBlock
3. **Statistical Tests**: NoiseAWGNBlock may need longer sequences for stable statistics
4. **dbf Buffer Size**: Some tests require large buffers (4KB+) for doubly-mapped buffer support

## Build & Run

```bash
# Build all tests
cd build
make math_blocks_test utility_blocks_test sink_blocks_test noise_blocks_test resampler_blocks_test channelizer_blocks_test

# Run individual test suites
./tests/desktop_blocks/math_blocks_test
./tests/desktop_blocks/utility_blocks_test
./tests/desktop_blocks/sink_blocks_test
./tests/desktop_blocks/noise_blocks_test
./tests/desktop_blocks/resampler_blocks_test
./tests/desktop_blocks/channelizer_blocks_test

# Run all tests (if integrated with ctest)
ctest --test-dir tests/desktop_blocks
```

## Implementation Notes

- **Buffer Sizes**: Use 4096+ bytes for dbf compatibility
- **Test Data**: Generate deterministic test signals for reproducible results
- **Error Handling**: Test constructor validation and runtime error conditions
- **Complex Data**: Verify both real and imaginary components separately
- **Multi-Run Tests**: Ensure blocks maintain state correctly across multiple procedure() calls

## Future Enhancements

1. **Performance Benchmarks**: Add timing measurements for throughput analysis
2. **Memory Usage**: Validate memory allocation patterns and leaks
3. **Thread Safety**: Test concurrent access patterns where applicable
4. **Integration Tests**: Multi-block pipeline testing
5. **Fuzzing**: Random input testing for robustness validation