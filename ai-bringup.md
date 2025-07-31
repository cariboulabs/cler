# AI Bringup Guide for Cler DSP Framework

This comprehensive guide provides context and guidance for AI assistants (Claude Code, GitHub Copilot, etc.) when working with the Cler DSP framework codebase.

## 1. Overview & Architecture

Cler is a C++17 template-based DSP flowgraph framework for SDRs and embedded systems. It prioritizes compile-time safety, zero-cost abstractions, and minimal runtime footprint suitable for everything from desktop SDR applications to bare-metal MCUs.

### Key Design Principles
- **Template-based**: Compile-time type safety and optimization
- **Two execution modes**: Flowgraph (threaded) vs Streamlined (manual control)
- **Platform agnostic**: Desktop, FreeRTOS, ThreadX, Zephyr, baremetal
- **Channel ownership**: Blocks own input channels, output channels passed as parameters
- **Variadic outputs**: Blocks can have multiple output channels via template parameters

### Core Execution Models

#### Flowgraph Mode (Threaded)
- Framework manages block execution in separate threads
- Requires task policy for platform abstraction
- Automatic flow control and error handling

#### Streamlined Mode (Manual Control)  
- User controls execution loop and data flow
- No threading overhead
- Direct procedure calls between blocks

## 2. Repository Structure

**Note**: This framework is in early development - specific block names, locations, and APIs may change. Use this as a general guide and explore the actual codebase for current structure.

### Core Framework (`/include/`)
**Header-only core framework** - link with `cler::cler`:

- **`cler.hpp`** - Main framework header containing:
  - `Error` enum and `Result<T, Error>` type for error handling
  - `ChannelBase<T>` interface and `Channel<T, N>` implementation (SPSC queues)
  - `BlockBase` - Base class for all processing blocks
  - `BlockRunner<Block, Channels...>` - Template for connecting blocks
  - `FlowGraph<TaskPolicy, BlockRunners...>` - Multi-threaded execution engine
  
- **`cler_spsc-queue.hpp`** - Lock-free single-producer single-consumer queue implementation (modified from drogalis/SPSC-Queue)

- **`cler_result.hpp`** - Result monad for error handling without exceptions

- **`cler_utils.hpp`** - Utility functions and helpers

- **`cler_desktop_utils.hpp`** - Desktop-specific utilities (requires std::ostream)

- **`cler_embedded_allocators.hpp`** - Memory allocators for embedded systems

- **`cler_embeddable_string.hpp`** - Fixed-size string implementation (`EmbeddableString<MaxLen>`) for embedded use without std::string dependency

### Task Policies (`/include/task_policies/`)
**Platform abstraction for threading**:

- **`cler_task_policy_base.hpp`** - CRTP base class for task policies
- **`cler_desktop_tpolicy.hpp`** - std::thread implementation + `make_desktop_flowgraph()`
- **`cler_freertos_tpolicy.hpp`** - FreeRTOS task implementation  
- **`cler_threadx_tpolicy.hpp`** - ThreadX thread implementation
- **`cler_zephyr_tpolicy.hpp`** - Zephyr kernel thread implementation

### Desktop Blocks Library (`/desktop_blocks/`)
**General-purpose blocks** - link with `cler::cler_desktop_blocks`:

**Note**: Block names and organization may evolve as development continues.

- **`sources/`** - Signal generators: CW, chirp, file, UDP, HackRF, CaribouLite
- **`sinks/`** - Output blocks: file, UDP, null
- **`math/`** - Math operations: add, gain, complex_demux
- **`plots/`** - ImGui visualizations: timeseries, spectrum, spectrogram
- **`channelizers/`** - DSP: polyphase channelizer (liquid-dsp)
- **`resamplers/`** - Sample rate conversion
- **`noise/`** - AWGN generator
- **`utils/`** - throttle, fanout, throughput measurement
- **`gui/`** - ImGui window management
- **`ezgmsk_demod/`** - GMSK demodulation
- **`udp/`** - Network communication blocks

### Examples and Applications

- **`/desktop_examples/`** - Key examples: hello_world, flowgraph (variadic), streamlined, polyphase_channelizer, SDR apps (HackRF/CaribouLite), UDP networking, GUI plots
- **`/embedded_examples/`** - Platform examples: baremetal, FreeRTOS, ThreadX, Zephyr

### Development Tools (`/tools/`)
- **`cler_tools/`** - Python tools: flowgraph validation (`cler-validate`) and visualization (`cler-viz`)
- **`integration/`** - Build system hooks: pre-commit, CMake, GitHub Actions

### Performance and Utilities

- **`/performance/`** - Benchmarking:
  - `cler_throughput.cpp` - Performance measurement application

- **`/logger/`** - Logging utilities:
  - `logger.h/.c` - C logging interface
  - `zf_log/` - Zero-allocation logging library

### Testing Infrastructure

- **`/tests/`** - Unit and integration tests:
  - `test_channel.cpp` - Channel implementation tests
  - `test_result.cpp` - Result type tests
  - `test_embeddable_string.cpp` - String implementation tests
  - Test runner and CMake integration

### Documentation

- **`/docs/`** - Web documentation and marketing site
- **`README.md`** - Project overview and quick start guide
- **`License`** - Project licensing information

## 3. Build System & Compilation

### Basic Build Commands
```bash
# Standard build (Release mode is default with -O3)
mkdir build && cd build
cmake ..
make -j"$(nproc --ignore=1)"

# Debug build with -g symbols
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

### Running Examples
```bash
cd build/desktop_examples
./hello_world                    # Basic flowgraph with GUI plot
./flowgraph                      # Multi-output variadic example  
./polyphase_channelizer          # N-channel DSP processing
./streamlined                    # Manual control loop
./mass_spring_damper             # Physics simulation
./udp                           # Network communication
```

## 4. CMake Integration Patterns

### Library Targets
```cmake
# Core framework only (header-only)
target_link_libraries(app PRIVATE cler::cler)

# Desktop development with GUI/plots/hardware support
target_link_libraries(app PRIVATE cler::cler_desktop_blocks)
```

### CMake Structure Examples
```cmake
# Simple executable with core framework
add_executable(simple_app main.cpp)
target_link_libraries(simple_app PRIVATE cler::cler)

# Desktop application with full blocks library
add_executable(desktop_app main.cpp)  
target_link_libraries(desktop_app PRIVATE cler::cler_desktop_blocks)

# Custom block library
add_library(my_blocks INTERFACE)
target_include_directories(my_blocks INTERFACE include/)
target_link_libraries(my_blocks INTERFACE cler::cler)
```

## 5. Core Functionality & Block Implementation

### Block Implementation Pattern
Blocks inherit from `cler::BlockBase` and implement `procedure()` with variadic output channels:

```cpp
struct MyBlock : public cler::BlockBase {
    cler::Channel<float> in;  // Input channels owned by block instance
    
    MyBlock(const char* name) : BlockBase(name), in(BUFFER_SIZE) {}
    
    // Output channels provided as variadic parameters to procedure()
    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        
        // Process data: read from input channels, write to output channels
        size_t transferable = std::min({in.size(), outs->space()...});
        
        for (size_t i = 0; i < transferable; ++i) {
            float sample;
            in.pop(sample);
            
            // Process sample...
            
            // Push to all outputs using fold expression
            ((outs->push(processed_sample)), ...);
        }
        
        return cler::Empty{};
    }
};
```

**Key Architecture Points:**
- **Input channels**: owned by block instance (`block.in`, `block.in[0]`)
- **Output channels**: passed as variadic parameters to `procedure()`
- **Multiple outputs**: Use variadic templates and fold expressions
- **Channel ownership**: Blocks own input channels, output channels owned by downstream blocks

### Example: Polyphase Channelizer with N Outputs
```cpp
struct PolyphaseChannelizerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;
    
    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        assert(num_outs == _num_channels);
        
        // Process one frame at a time
        if (in.size() < _num_channels) return cler::Error::NotEnoughSamples;
        
        size_t n_frames_by_space = std::min({outs->space()...});
        if (n_frames_by_space == 0) return cler::Error::NotEnoughSpace;
        
        // Read input frame, process with liquid-dsp, distribute to outputs
        in.readN(_tmp_in, _num_channels);
        firpfbch_crcf_analyzer_execute(_pfch, _tmp_in, _tmp_out);
        
        // Push outputs using lambda with fold expression
        size_t idx = 0;
        auto push_outputs = [&](auto*... chs) {
            ((chs->push(_tmp_out[idx++])), ...);
        };
        push_outputs(outs...);
        
        return cler::Empty{};
    }
};
```

### Two Programming Models

#### 1. Flowgraph Mode (Threaded)
**Required includes**: Both `cler.hpp` and a task policy header:
```cpp
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    // Create blocks
    SourceCWBlock<float> source("Source", 1.0f, 10.0f, 1000);
    AddBlock<float> adder("Adder", 2);
    ThrottleBlock<float> throttle("Throttle", 1000);
    PlotTimeSeriesBlock plot("Plot", {"Signal"}, 1000, 3.0f);
    
    // Create flowgraph with variadic outputs
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &adder.in[0]),           // single output
        cler::BlockRunner(&source2, &adder.in[1]),          
        cler::BlockRunner(&adder, &throttle.in),            
        cler::BlockRunner(&throttle, &plot.in[0]),
        cler::BlockRunner(&channelizer,                     // multiple outputs
            &plot1.in[0], &plot1.in[1], &plot1.in[2]),     // variadic parameters
        cler::BlockRunner(&plot)                            // no outputs (sink)
    );
    
    // Configure and run
    cler::FlowGraphConfig config;
    // config.adaptive_sleep = true;  // Optional: for sparse/intermittent data only
    flowgraph.run(config);
    
    // GUI loop...
    while (!gui.should_close()) {
        gui.begin_frame();
        plot.render();
        gui.end_frame();
    }
    
    flowgraph.stop();
    return 0;
}
```

#### 2. Streamlined Mode (Manual Control)
```cpp
#include "cler.hpp"
// No task policy needed for streamlined mode

int main() {
    SourceBlock source("Source");
    AdderBlock adder("Adder");
    GainBlock gain("Gain", 2.0f);
    SinkBlock sink("Sink");
    
    // Manual control loop
    while (true) {
        auto res1 = source.procedure(&adder.in0, &adder.in1);  // multiple outputs
        auto res2 = adder.procedure(&gain.in);                 // single output
        auto res3 = gain.procedure(&sink.in);
        auto res4 = sink.procedure();                          // no outputs
        
        // Handle errors if needed...
    }
}
```

### Required Includes for Flowgraph Mode
```cpp
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"  // Platform-specific policy
```

### Flowgraph Construction and Scheduler Configuration
```cpp
auto flowgraph = cler::make_desktop_flowgraph(
    cler::BlockRunner(&source, &adder.in[0]),           // single output
    cler::BlockRunner(&source2, &adder.in[1]),          
    cler::BlockRunner(&adder, &throttle.in),            
    cler::BlockRunner(&channelizer,                     // multiple outputs
        &plot1.in[0], &plot1.in[1], &plot1.in[2]),     // variadic parameters
    cler::BlockRunner(&plot)                            // no outputs (sink)
);

// Configure scheduler and performance options
cler::FlowGraphConfig config;

// Choose scheduler type (new in recent versions)
config.scheduler = cler::SchedulerType::ThreadPerBlock;        // Default: one thread per block
// config.scheduler = cler::SchedulerType::FixedThreadPool;    // Fixed worker pool (num_workers required)
// config.scheduler = cler::SchedulerType::AdaptiveLoadBalancing; // Dynamic work distribution

// Worker configuration (for FixedThreadPool and AdaptiveLoadBalancing)
config.num_workers = 4;  // Number of worker threads (minimum 2, ignored for ThreadPerBlock)

// Adaptive sleep configuration (works with all scheduler types)
// WARNING: Can significantly reduce throughput - only use for sparse/intermittent data
config.adaptive_sleep = true;  // CAUTION: reduces CPU usage but may impact throughput
config.adaptive_sleep_multiplier = 1.5;       // How aggressively to increase sleep time
config.adaptive_sleep_max_us = 5000.0;        // Maximum sleep time in microseconds
config.adaptive_sleep_fail_threshold = 10;    // Start sleeping after N consecutive fails

// Load balancing (only for AdaptiveLoadBalancing scheduler)
config.load_balancing = true;                 // Enable dynamic rebalancing
config.load_balancing_interval = 1000;        // Rebalance every N procedure calls
config.load_balancing_threshold = 0.2;        // 20% imbalance triggers rebalancing

flowgraph.run(config);
```

## 6. Channel Management & Buffer Access

### Channel Buffer Types
```cpp
// Stack allocation (compile-time size)
cler::Channel<float, 1024> static_channel;

// Heap allocation (runtime size)  
cler::Channel<float> dynamic_channel(1024);

// Used in block constructors
AdderBlock(const char* name) : BlockBase(name), 
    in0(CHANNEL_SIZE),        // heap allocated
    in1(CHANNEL_SIZE) {}      // heap allocated

struct GainBlock : public cler::BlockBase {
    cler::Channel<float, CHANNEL_SIZE> in;  // stack allocated
    // ...
};
```

#### Buffer Access Patterns (Performance Order)
```cpp
// 0. Zero-copy span access (NEW - BEST for file/network I/O when available)
auto [read_ptr, read_size] = in.read_dbf();
if (read_ptr) {
    // Direct access to contiguous data (no wrap-around with doubly mapped)
    fwrite(read_ptr, sizeof(T), read_size, fp);
    in.commit_read(read_size);
}

auto [write_ptr, write_size] = out->write_dbf();
if (write_ptr) {
    // Direct write to buffer (no wrap-around with doubly mapped)
    size_t n = fread(write_ptr, sizeof(T), write_size, fp);
    out->commit_write(n);
}

// 1. Read/Write (bulk transfer - PREFERRED)
size_t written = out->writeN(buffer, count);
size_t read = in.readN(buffer, count);

// 2. Peek/Commit (inspect before processing)
const float* ptr1, *ptr2;
size_t size1, size2;
size_t available = in.peek_read(ptr1, size1, ptr2, size2);
// Process data...
in.commit_read(processed_count);

// For writing with peek_write (NOTE: variables passed by reference, not pointers)
float* write_ptr1, *write_ptr2;
size_t write_size1, write_size2;
size_t writable = out->peek_write(write_ptr1, write_size1, write_ptr2, write_size2);
// Write data...
out->commit_write(written_count);

// 3. Push/Pop (single values - AVOID in hot paths)
float sample;
in.pop(sample);
out->push(processed_sample);
```

### Channel Implementation Notes
- **Doubly Mapped Buffers**: On Linux/macOS/FreeBSD, heap buffers ≥32KB automatically use doubly mapped memory for zero-copy wraparound access
- **read_dbf()/write_dbf()**: Return {nullptr, 0} on unsupported platforms or stack buffers
- **Transparent Fallback**: Blocks should try span methods first, then fall back to peek/commit

### Error Handling Pattern
```cpp
cler::Result<cler::Empty, cler::Error> procedure(/* outputs */) {
    // Check input availability
    if (in.size() < required_samples) {
        return cler::Error::NotEnoughSamples;  // Framework will retry
    }
    
    // Check output space
    if (out->space() < required_space) {
        return cler::Error::NotEnoughSpace;    // Framework will retry  
    }
    
    // Process data...
    
    return cler::Empty{};  // Success
    
    // For unrecoverable errors:
    // return cler::Error::TERM_ProcedureError;  // Terminates flowgraph
}
```

## 7. Platform Support & Task Policies

### Task Policy Abstraction
Different platforms require different threading models:

```cpp
// Desktop: std::thread
#include "task_policies/cler_desktop_tpolicy.hpp"
auto flowgraph = cler::make_desktop_flowgraph(/* runners */);

// FreeRTOS: xTaskCreate  
#include "task_policies/cler_freertos_tpolicy.hpp"
auto flowgraph = cler::FlowGraph<cler::FreeRTOSTaskPolicy, /* runners */>(/* runners */);

// ThreadX: tx_thread_create
#include "task_policies/cler_threadx_tpolicy.hpp"

// Zephyr: k_thread_create  
#include "task_policies/cler_zephyr_tpolicy.hpp"

// Baremetal: no threading - use streamlined mode only
```

### Embedded Considerations
- **Minimal dependencies**: C++17 standard library only
- **Static allocation**: Compile-time buffer sizing for deterministic memory
- **No exceptions**: Use `cler::Result` for error handling
- **Configurable buffer sizes**: Template parameters for memory control

### Embedded Examples Structure
```
embedded_examples/
├── baremetal_examples/     # No OS, direct hardware
├── freertos_examples/      # FreeRTOS integration  
├── threadx_examples/       # ThreadX integration
└── zephyr_examples/        # Zephyr RTOS integration
```

### Baremetal Example (No Threading)
```cpp
#include "cler.hpp"
// No task policy needed

struct SimpleBlock : public cler::BlockBase {
    cler::Channel<float, 64> in;  // Stack allocated, fixed size
    
    SimpleBlock(const char* name) : BlockBase(name) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Minimal processing...
        return cler::Empty{};
    }
};

int main() {
    SimpleBlock block("Block");
    
    // Streamlined mode only for baremetal
    while (true) {
        block.procedure(/* outputs */);
        // Hardware-specific timing...
    }
}
```

## 8. Desktop Blocks Library Details

**Philosophy**: Desktop blocks prioritize generality and ease of use over minimal resource usage. Everything that can go on the heap goes on the heap. Not optimized for minimal work sizes.

### Key Block Categories

#### Sources (No Input Channels)
```cpp
// Continuous wave generator
SourceCWBlock<float> cw_source("CW", amplitude, freq_hz, sample_rate);

// File reader
SourceFileBlock<std::complex<float>> file_source("File", "input.bin");

// Network receiver
SourceUDPBlock<float> udp_source("UDP", port, buffer_size);

// Hardware interfaces
SourceHackRFBlock hackrf("HackRF", center_freq, sample_rate);
SourceCaribouliteBlock caribou("Caribou", center_freq, sample_rate);
```

#### Processing Blocks
```cpp
// Math operations
AddBlock<float> adder("Adder", num_inputs);  // Variable number of inputs
GainBlock<float> gain("Gain", gain_value);
ComplexDemuxBlock demux("Demux");

// DSP processing
PolyphaseChannelizerBlock channelizer("PFB", num_channels, attenuation, filter_len);
MultistageResamplerBlock resampler("Resampler", input_rate, output_rate);
NoiseAWGNBlock<std::complex<float>> noise("AWGN", noise_power);

// Utilities
ThrottleBlock<float> throttle("Throttle", sample_rate);
FanoutBlock<float> fanout("Fanout", num_outputs);
ThroughputBlock<float> throughput("Throughput");  // Performance measurement
```

#### Sinks (No Output Channels) 
```cpp
// File writer
SinkFileBlock<float> file_sink("File", "output.bin");

// Network transmitter  
SinkUDPBlock<float> udp_sink("UDP", host, port);

// Null sink (discard data)
SinkNullBlock<float> null_sink("Null");

// GUI plots
PlotTimeSeriesBlock plot("TimeSeries", {"Signal1", "Signal2"}, sample_rate, duration);
PlotCSpectrumBlock spectrum("Spectrum", {"Ch1", "Ch2"}, sample_rate, fft_size);
PlotCSpectrogramBlock spectrogram("Spectrogram", sample_rate, fft_size);
```

### Superblock Pattern (Composition)
Desktop blocks can compose other blocks internally - chain their `procedure()` calls to create complex functionality.

## 9. Block Implementation Examples

### Basic Block Pattern
```cpp
struct MyBlock : public cler::BlockBase {
    cler::Channel<float> in;  // Input channels owned by block
    
    MyBlock(const char* name) : BlockBase(name), in(BUFFER_SIZE) {}
    
    // Output channels passed as variadic parameters
    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        // Check input/output availability
        if (in.size() < required_samples) return cler::Error::NotEnoughSamples;
        if (std::min({outs->space()...}) == 0) return cler::Error::NotEnoughSpace;
        
        // Process data efficiently
        size_t transferable = std::min({in.size(), outs->space()...});
        for (size_t i = 0; i < transferable; ++i) {
            float sample;
            in.pop(sample);
            float processed = process(sample);
            // Push to all outputs using fold expression
            ((outs->push(processed)), ...);
        }
        
        return cler::Empty{};
    }
};
```

### Multiple Output Example (Variadic)
```cpp
struct ChannelizerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;
    
    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        
        if (in.size() < num_outs) return cler::Error::NotEnoughSamples;
        
        // Read frame, process, distribute to outputs
        in.readN(_tmp_in, num_outs);
        process_channels(_tmp_in, _tmp_out);
        
        // Push outputs using fold expression
        size_t idx = 0;
        ((outs->push(_tmp_out[idx++])), ...);
        
        return cler::Empty{};
    }
};
```

### Complete Flowgraph Example
```cpp
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    // Create blocks
    SourceCWBlock<float> source("Source", 1.0f, 10.0f, 1000);
    AddBlock<float> adder("Adder", 2);
    PlotTimeSeriesBlock plot("Plot", {"Signal"}, 1000, 3.0f);
    
    // Create flowgraph with connections
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &adder.in[0]),     // single output
        cler::BlockRunner(&channelizer,               // multiple outputs
            &plot1.in[0], &plot2.in[0], &plot3.in[0]), // variadic params
        cler::BlockRunner(&plot)                      // no outputs (sink)
    );
    
    flowgraph.run();
    // GUI loop, then flowgraph.stop();
}
```

### Streamlined Mode (Manual Control)
```cpp
// Manual control without threading
while (true) {
    auto res1 = source.procedure(&adder.in0, &adder.in1);  // multiple outputs
    auto res2 = adder.procedure(&gain.in);                 // single output
    auto res3 = gain.procedure(&sink.in);
    auto res4 = sink.procedure();                          // no outputs
}
```

## 10. Development Tools

```bash
# Install tools
cd tools && uv pip install -e .

# Validate flowgraphs
cler-validate desktop_examples/*.cpp

# Generate visualizations  
cler-viz file.cpp -o output.svg
```

Tools check for: missing BlockRunners, invalid connections, unconnected channels.

## 11. Performance & Debugging

### Scheduler Types and Performance Optimization

Cler provides three scheduler types to optimize for different workload characteristics:

#### ThreadPerBlock (Default)
- **Best for**: Small flowgraphs, debugging, uniform workloads
- **Characteristics**: One dedicated thread per block
- **Pros**: Simple, predictable, no thread contention
- **Cons**: Thread overhead, poor scalability with many blocks

#### FixedThreadPool
- **Best for**: Uniform workloads with balanced processing
- **Characteristics**: Fixed number of worker threads processing blocks round-robin
- **Pros**: Lower thread overhead, better CPU cache utilization
- **Cons**: Can suffer from work imbalance
- **Requires**: `config.num_workers` (minimum 2)

### Execution Statistics and Block Performance
```cpp
// Configure for optimal performance based on workload
cler::FlowGraphConfig config;

// Example 1: Uniform workload (e.g., simple signal processing chain)
config.scheduler = cler::SchedulerType::FixedThreadPool;
config.num_workers = 4;
// config.adaptive_sleep = true;  // Optional: only if data is sparse

flowgraph.run(config);

// After stopping, get detailed report with performance metrics
flowgraph.stop();
cler::print_flowgraph_execution_report(flowgraph);

// BlockExecutionStats now includes (calculated post-execution):
// - Successful/failed procedure counts
// - CPU utilization percentage
// - Average execution time per procedure
// - Throughput in samples/second
// - Adaptive sleep final value (if enabled)
```

### Benchmarking
```bash
cd build/performance && ./cler_throughput
```

### Common Performance Patterns
```cpp
// Efficient bulk transfer with correct peek_write usage
size_t available = std::min({in.size(), out->space()});
float* write_ptr1, *write_ptr2;
size_t write_size1, write_size2;
size_t writable = out->peek_write(write_ptr1, write_size1, write_ptr2, write_size2);
size_t to_process = std::min(available, write_size1);  // Use first segment

// Process directly in output buffer
for (size_t i = 0; i < to_process; ++i) {
    float sample;
    in.pop(sample);
    write_ptr1[i] = process(sample);
}
out->commit_write(to_process);
```

### Performance Recommendations by Use Case

#### Simple Linear Chain (Source → A → B → C → Sink)
- **Scheduler**: ThreadPerBlock (simple, predictable)
- **Adaptive Sleep**: Yes for sparse data, No for continuous streams
- **Expected**: Good performance, easy debugging

#### Fanout with Uniform Processing (Source → Fanout → [N similar paths] → Sinks)
- **Scheduler**: FixedThreadPool with workers = min(N/2, CPU cores)
- **Adaptive Sleep**: Optional based on data rate
- **Expected**: Better than ThreadPerBlock due to reduced thread overhead

#### Fanout with Imbalanced Processing (different complexity per path)
- **Scheduler**: AdaptiveLoadBalancing with load_balancing enabled
- **Workers**: Start with CPU cores - 1
- **Load Balancing**: interval=500-1000, threshold=0.1-0.2
- **Expected**: Significantly better than FixedThreadPool for imbalanced loads

#### Many Blocks (>20 blocks in flowgraph)
- **Scheduler**: AdaptiveLoadBalancing or FixedThreadPool
- **Workers**: 4-8 depending on CPU
- **Rationale**: ThreadPerBlock creates too many threads

#### Sparse/Intermittent Data (sensors, network packets)
- **Scheduler**: Any (ThreadPerBlock is fine for simplicity)
- **Adaptive Sleep**: REQUIRED - aggressive settings
- **Settings**: multiplier=2.0, max_us=10000, fail_threshold=5
- **Expected**: >90% reduction in CPU usage during idle

## 12. Development Guidelines & Code Style

### Core Principles
- **Templates over virtual functions** for performance-critical paths
- **Avoid `std::function`** - use function pointers or lambdas when needed
- **Composition over inheritance** except for simple interfaces like `BlockBase`
- **No try/catch for flow control** - use `cler::Result` for recoverable errors
- **Heavy implementations in `.cpp`** when dealing with single data types

### Framework Internals
- **EmbeddableString**: Fixed-size strings without std::string dependency
- **Result<T,E>**: Error handling without exceptions
- **Template-based connections**: Type-safe at compile time
- **BlockExecutionStats**: Optimized structure storing only runtime data
  - Runtime fields: successful/failed procedures, samples processed, dead time, runtime
  - Post-processing calculations: avg execution time, CPU utilization %, throughput
  - Memory optimized: ~32 bytes smaller per block vs calculating at runtime

### Additional Implementation Notes

#### Channel Buffer Access (Corrected)
The `peek_write()` and `peek_read()` methods use a two-segment circular buffer design. Both segments must be handled:

```cpp
// Correct peek_write usage - variables passed by reference
T* ptr1, *ptr2;
size_t size1, size2;
size_t total = channel.peek_write(ptr1, size1, ptr2, size2);
// total = size1 + size2 (total writable space)

// Write to first segment
for (size_t i = 0; i < size1; ++i) {
    ptr1[i] = data[i];
}

// Write to second segment if needed
for (size_t i = 0; i < size2; ++i) {
    ptr2[i] = data[size1 + i];
}

channel.commit_write(size1 + size2);
```

#### Error Codes Reference
```cpp
enum class Error : int {
    Success = 0,
    NotEnoughSamples = 1,
    NotEnoughSpace = 2,
    
    // Terminal errors (negative values)
    TERM_ChannelClosed = -1,
    TERM_ChannelError = -2,
    TERM_ProcedureError = -3,
    TERM_Requested = -4
};
```

### Common Template Errors & Solutions
- **Missing runner**: Every block needs a `BlockRunner` 
- **Connection mismatch**: Output channel type must match input channel type
- **Missing policy**: Flowgraph mode requires task policy include
- **Template explosion**: Use LLM assistance for complex template errors

## 13. Quick Reference - Common Patterns

### Block Creation Checklist
1. Inherit from `cler::BlockBase`
2. Declare input channels as member variables
3. Initialize channels in constructor (with size)
4. Implement `procedure()` with variadic output parameters
5. Check input availability and output space
6. Process data efficiently (bulk operations preferred)
7. Return appropriate error codes

### Flowgraph Creation Checklist
1. Include `cler.hpp` and appropriate task policy
2. Create all block instances
3. Create `BlockRunner` for each block with connections
4. Use `make_desktop_flowgraph()` or construct `FlowGraph`
5. Configure and run flowgraph
6. Handle GUI loop if using plots
7. Stop flowgraph before cleanup

### Performance Tips
1. Use bulk read/write operations (`readN`/`writeN`)
2. Prefer `peek_read`/`peek_write` for zero-copy processing
3. Avoid single-sample `push`/`pop` in hot paths
4. Process multiple samples per `procedure()` call
5. Use compile-time channel sizes when possible
6. Enable adaptive sleep for better CPU usage

### Common Pitfalls
1. Forgetting task policy include for flowgraph mode
2. Incorrect `peek_write` usage (pass by reference, not pointer)
3. Not checking channel space before writing
4. Missing `BlockRunner` for a block
5. Type mismatch between connected channels
6. Not handling terminal errors appropriately

This comprehensive guide provides accurate context for AI assistants working with the Cler DSP framework, with all corrections applied based on the actual codebase structure and API usage.