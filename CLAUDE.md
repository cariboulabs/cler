# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
  - `EmbeddableString<MaxLen>` - Fixed-size string for embedded use
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

- **`sources/`** - Signal generators and input blocks:
  - `source_cw.hpp` - Continuous wave generator
  - `source_chirp.hpp` - Chirp signal generator  
  - `source_file.hpp` - File reader
  - `source_udp.hpp` - UDP network receiver
  - `source_hackrf.hpp` - HackRF SDR interface
  - `source_cariboulite.hpp` - CaribouLite SDR interface

- **`sinks/`** - Output and storage blocks:
  - `sink_file.hpp` - File writer
  - `sink_udp.hpp` - UDP network transmitter
  - `sink_null.hpp` - Discard data (null sink)

- **`math/`** - Mathematical operations:
  - `add.hpp` - Multi-input adder block
  - `gain.hpp` - Gain/scaling block
  - `complex_demux.hpp` - Complex signal demultiplexer

- **`plots/`** - Visualization blocks (ImGui-based):
  - `plot_timeseries.hpp` - Time-domain plotting
  - `plot_cspectrum.hpp` - Complex spectrum analyzer
  - `plot_cspectrogram.hpp` - Spectrogram waterfall
  - `spectral_windows.hpp` - Window functions for FFT

- **`channelizers/`** - DSP processing:
  - `polyphase_channelizer.hpp` - Multi-channel channelizer (liquid-dsp)

- **`resamplers/`** - Sample rate conversion:
  - `multistage_resampler.hpp` - Efficient resampling

- **`noise/`** - Noise generation:
  - `awgn.hpp` - Additive white Gaussian noise

- **`utils/`** - Utility blocks:
  - `throttle.hpp` - Rate limiting
  - `fanout.hpp` - Split signal to multiple outputs
  - `throughput.hpp` - Performance measurement

- **`gui/`** - GUI management:
  - `gui_manager.hpp` - ImGui window management

- **`ezgmsk_demod/`** - Specialized demodulator:
  - `ezgmsk_demod.hpp` - GMSK demodulation block

- **`udp/`** - Network communication:
  - `sink_udp.hpp`, `source_udp.hpp` - UDP blocks
  - `shared.hpp` - Common UDP utilities

### Examples and Applications

- **`/desktop_examples/`** - Complete applications demonstrating usage:
  - `hello_world.cpp` - Basic flowgraph with GUI plot
  - `flowgraph.cpp` - Multi-output threading example
  - `streamlined.cpp` - Manual control loop example  
  - `polyphase_channelizer.cpp` - Advanced DSP processing
  - `mass_spring_damper.cpp` - Physics simulation
  - `udp.cpp` - Network communication
  - `cariboulite_receiver.cpp` - SDR receiver application
  - `hackrf_receiver.cpp` - HackRF SDR application

- **`/embedded_examples/`** - Embedded/RTOS examples:
  - `baremetal_examples/` - No OS, direct hardware
  - `freertos_examples/` - FreeRTOS integration
  - `threadx_examples/` - ThreadX integration  
  - `zephyr_examples/` - Zephyr RTOS integration

### Development Tools (`/tools/`)
**Python-based development utilities**:

- **`cler_tools/linter/`** - Flowgraph validation:
  - `validate.py` - Main validation engine
  - `rules.yaml` - Validation rules configuration
  
- **`cler_tools/viz/`** - Flowgraph visualization:
  - `visualize.py` - SVG diagram generation
  - `graph_builder.py` - Graph construction logic
  - `svg_renderer.py` - SVG rendering engine
  - `layout.py` - Layout algorithms

- **`cler_tools/common/`** - Shared utilities:
  - `cpp_parser.py` - C++ parsing logic
  - `patterns.py` - Regex patterns for code analysis

- **`integration/`** - Build system integrations:
  - `pre-commit-hook.sh` - Git pre-commit validation
  - `cmake-integration.cmake` - CMake integration
  - `github-action.yml` - CI/CD workflow
  - `Makefile.example` - Make integration

### Performance and Utilities

- **`/performance/`** - Benchmarking:
  - `cler_throughput.cpp` - Performance measurement application

- **`/logger/`** - Logging utilities:
  - `logger.h/.c` - C logging interface
  - `zf_log/` - Zero-allocation logging library

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
    config.adaptive_sleep = true;
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

### Flowgraph Construction
```cpp
auto flowgraph = cler::make_desktop_flowgraph(
    cler::BlockRunner(&source, &adder.in[0]),           // single output
    cler::BlockRunner(&source2, &adder.in[1]),          
    cler::BlockRunner(&adder, &throttle.in),            
    cler::BlockRunner(&channelizer,                     // multiple outputs
        &plot1.in[0], &plot1.in[1], &plot1.in[2]),     // variadic parameters
    cler::BlockRunner(&plot)                            // no outputs (sink)
);

cler::FlowGraphConfig config;
config.adaptive_sleep = true;
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
// 1. Read/Write (bulk transfer - PREFERRED)
size_t written = out->writeN(buffer, count);
size_t read = in.readN(buffer, count);

// 2. Peek/Commit (inspect before processing)
const float* ptr1, *ptr2;
size_t size1, size2;
size_t available = in.peek_read(ptr1, size1, ptr2, size2);
// Process data...
in.commit_read(processed_count);

// 3. Push/Pop (single values - AVOID in hot paths)
float sample;
in.pop(sample);
out->push(processed_sample);
```

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
Desktop blocks often use composition to create complex functionality:

```cpp
struct CustomSourceBlock : public cler::BlockBase {
    CustomSourceBlock(const char* name, float amplitude, float noise_stddev, 
                     float frequency_hz, size_t sps)
        : BlockBase(name),
          cw_source_block("CWSource", amplitude, frequency_hz, sps),
          noise_block("AWGN", noise_stddev / 100.0f),
          fanout_block("Fanout", 2)
    {}

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<std::complex<float>>* out1, 
        cler::ChannelBase<std::complex<float>>* out2) {
        
        // Chain internal blocks
        auto result = cw_source_block.procedure(&noise_block.in);
        if (result.is_err()) return result.unwrap_err();
        
        result = noise_block.procedure(&fanout_block.in);
        if (result.is_err()) return result.unwrap_err();
        
        return fanout_block.procedure(out1, out2);
    }
    
private:
    SourceCWBlock<std::complex<float>> cw_source_block;
    NoiseAWGNBlock<std::complex<float>> noise_block;  
    FanoutBlock<std::complex<float>> fanout_block;
};
```

### GUI Integration
```cpp
#include "desktop_blocks/gui/gui_manager.hpp"

int main() {
    // Create GUI manager
    cler::GuiManager gui(800, 600, "Application Title");
    
    // Create plot blocks  
    PlotTimeSeriesBlock plot("Plot", {"Signal"}, sample_rate, 3.0f);
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 300.0f);
    
    // Run flowgraph...
    
    // GUI render loop
    while (!gui.should_close()) {
        gui.begin_frame();
        plot.render();  // Plot blocks render themselves
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    return 0;
}
```

## 9. Advanced Examples & Patterns

### Multiple Output Example (Polyphase Channelizer)
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

### Complete Flowgraph Example
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
    config.adaptive_sleep = true;
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

### Streamlined Mode Example
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

### GUI Integration
```cpp
#include "desktop_blocks/gui/gui_manager.hpp"

int main() {
    cler::GuiManager gui(800, 600, "Application Title");
    
    PlotTimeSeriesBlock plot("Plot", {"Signal"}, sample_rate, 3.0f);
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 300.0f);
    
    // Run flowgraph...
    
    // GUI render loop
    while (!gui.should_close()) {
        gui.begin_frame();
        plot.render();  // Plot blocks render themselves
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    return 0;
}
```

## 10. Development Tools & Validation

### Installation & Usage
```bash
# Install Python tools (3.8+ required)
cd tools && uv pip install -e .

# Validate flowgraph structure
cler-validate desktop_examples/*.cpp
cler-validate --json src/*.cpp

# Generate flowgraph visualizations  
cler-viz file.cpp -o output.svg
cler-viz *.cpp --output-dir ./diagrams/

# Run test suites
cd tools/cler_tools/linter/tests && ./run_tests.sh
cd tools/cler_tools/viz/tests && ./run_tests.sh
```

### Validation Rules
The `cler-validate` tool checks for common flowgraph mistakes:
- Missing BlockRunner for declared blocks
- BlockRunner not added to flowgraph
- Invalid channel connections
- Unconnected inputs/outputs

### Integration Examples
```bash
# Pre-commit hook
tools/integration/pre-commit-hook.sh

# GitHub Actions workflow  
cp tools/integration/github-action.yml .github/workflows/

# CMake integration
include(tools/integration/cmake-integration.cmake)
add_cler_validation(my_target)
```

## 11. Performance & Debugging

### Execution Statistics
```cpp
// Enable adaptive sleep for better CPU usage
cler::FlowGraphConfig config;
config.adaptive_sleep = true;
config.adaptive_sleep_ramp_up_factor = 1.5;
config.adaptive_sleep_max_us = 5000.0;
flowgraph.run(config);

// After stopping, get detailed report
flowgraph.stop();
cler::print_flowgraph_execution_report(flowgraph);
throughput_block.report();
```

### Benchmarking
```bash
cd build/performance && ./cler_throughput
```

### Common Performance Patterns
```cpp
// Efficient bulk transfer
size_t available = std::min({in.size(), out->space()});
float* write_ptr;
size_t write_size;
out->peek_write(write_ptr, write_size, nullptr, nullptr);
size_t to_process = std::min(available, write_size);

// Process directly in output buffer
for (size_t i = 0; i < to_process; ++i) {
    float sample;
    in.pop(sample);
    write_ptr[i] = process(sample);
}
out->commit_write(to_process);
```

## 12. Development Guidelines & Code Style

### Core Principles
- **Templates over virtual functions** for performance-critical paths
- **Avoid `std::function`** - use function pointers or lambdas when needed
- **Composition over inheritance** except for simple interfaces like `BlockBase`
- **No try/catch for flow control** - use `cler::Result` for recoverable errors
- **Heavy implementations in `.cpp`** when dealing with single data types

### Framework Internals (for tool developers)
```cpp
// EmbeddableString for names (avoids std::string dependency)
cler::EmbeddableString<64> block_name = "MyBlock";
cler::EmbeddableString<64> combined = block_name + "_Suffix";

// Result type for error handling
cler::Result<OutputType, cler::Error> result = operation();
if (result.is_err()) {
    cler::Error error = result.unwrap_err();
    // Handle error...
} else {
    OutputType value = result.unwrap();
    // Use value...
}
```

### Code Generation Patterns (for cler-flow and similar tools)
```cpp
// Generate block declarations
${BlockType}<${DataType}> ${instanceName}("${displayName}", ${parameters...});

// Generate variadic connections  
cler::BlockRunner(&${sourceBlock}, 
    &${destBlock1}.in[${channel1}],
    &${destBlock2}.in[${channel2}],
    // ... more outputs
),

// Generate policy include based on target
#include "task_policies/cler_${platform}_tpolicy.hpp"
```

### Common Template Errors & Solutions
- **Missing runner**: Every block needs a `BlockRunner` 
- **Connection mismatch**: Output channel type must match input channel type
- **Missing policy**: Flowgraph mode requires task policy include
- **Template explosion**: Use LLM assistance for complex template errors

This comprehensive guide provides progressive depth from high-level architecture to implementation details, following the logical learning progression for working effectively with the Cler DSP framework.