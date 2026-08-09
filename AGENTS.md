# AI Bringup Guide for Cler DSP Framework

This comprehensive guide provides context and guidance for AI assistants (Claude Code, GitHub Copilot, etc.) when working with the Cler DSP framework codebase.

## 1. Overview & Architecture

Cler is a C++17 template-based DSP flowgraph framework for SDRs and embedded systems. It prioritizes compile-time safety, zero-cost abstractions, and minimal runtime footprint suitable for everything from desktop SDR applications to bare-metal MCUs.

### Platform Support
- **Linux**: Fully supported (Ubuntu, Debian, and other distributions)
- **macOS**: Fully supported (Intel and Apple Silicon)
- **Windows**: Not natively supported. Windows users should use [Windows Subsystem for Linux (WSL2)](https://docs.microsoft.com/en-us/windows/wsl/install)
  - WSL2 provides full POSIX compliance and allows you to use CLER exactly as on Linux
  - No special Windows toolchain or modifications needed when using WSL2

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
**General-purpose blocks** - link with `cler::desktop_blocks`:

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
target_link_libraries(app PRIVATE cler::desktop_blocks)
```

### CMake Structure Examples
```cmake
# Simple executable with core framework
add_executable(simple_app main.cpp)
target_link_libraries(simple_app PRIVATE cler::cler)

# Desktop application with full blocks library
add_executable(desktop_app main.cpp)  
target_link_libraries(desktop_app PRIVATE cler::desktop_blocks)

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

The channel count is a template parameter, not a constructor argument. It has to
be: `procedure` takes its outputs as a variadic pack, so the count is already
fixed at compile time by every call site. A runtime copy of it can only ever
disagree by mistake, and the mismatch is a buffer overrun.

```cpp
template <size_t NUM_CHANNELS, size_t FILTER_SEMILENGTH>
struct PolyphaseChannelizerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        static_assert(sizeof...(OChannels) == NUM_CHANNELS);

        auto [read_ptr, read_size] = in.read_dbf();
        if (read_size < NUM_CHANNELS) return cler::Error::NotEnoughSamples;

        // Collect one contiguous write span per output, take the smallest.
        std::array<std::complex<float>*, NUM_CHANNELS> ports;
        size_t min_write_space = std::numeric_limits<size_t>::max();
        size_t idx = 0;
        auto get_write_ptrs = [&](auto*... chs) {
            ([&] {
                auto [write_ptr, write_space] = chs->write_dbf();
                ports[idx] = write_ptr;
                min_write_space = std::min(min_write_space, write_space);
                idx++;
            }(), ...);
        };
        get_write_ptrs(outs...);

        const size_t num_frames = std::min(read_size / NUM_CHANNELS, min_write_space);
        if (num_frames == 0) return cler::Error::NotEnoughSpace;

        _analyzer.execute(read_ptr, num_frames, ports.data());   // whole batch, one call

        auto commit_writes = [&](auto*... chs) { ((chs->commit_write(num_frames)), ...); };
        commit_writes(outs...);
        in.commit_read(num_frames * NUM_CHANNELS);
        return cler::Empty{};
    }
};
```

**Batch the whole span, do not loop per frame.** This block was 3x faster on a
Cortex-A9 after the per-frame `liquid` call was replaced by one batched
allocation-free kernel; see "Polyphase channelizer" under Performance Notes.

### Two Programming Models

#### 1. Flowgraph Mode (Threaded)
**Required includes**: Both `cler.hpp` and a task policy header:
```cpp
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    // Create blocks
    SourceCWBlock<float> source("Source", 1.0f, 10.0f, 1000);
    AddBlock<float, 2> adder("Adder");
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

// Choose scheduler type
config.scheduler = cler::SchedulerType::ThreadPerBlock;        // Default: one thread per block
// config.scheduler = cler::SchedulerType::FixedThreadPool;    // Fixed worker pool (num_workers required)
// config.scheduler = cler::SchedulerType::PinnedIslands;      // Core-pinned topo islands, cost-partitioned (best on core-constrained targets)

// Worker configuration (for FixedThreadPool and PinnedIslands)
config.num_workers = 4;  // Number of worker threads
// Worker-count policy (same in debug and release):
//   FixedThreadPool clamps num_workers up to 2, then down to min(DEFAULT_MAX_WORKERS, regular block count)
//   PinnedIslands   clamps num_workers up to 1, then down to the same ceiling
//   0 and oversized values are clamped, never rejected

// PinnedIslands: shorthand and tuning
// auto cfg = cler::flowgraph_config::pinned_islands(2);  // cler_utils.hpp
// config.calibration_ms = 500;         // measure block costs, then repartition once
// config.repartition_check_ms = 5000;  // periodic drift check thereafter (0 = off)
// config.cpu_id_offset = 0;            // first core to pin workers to
// PinnedIslands ALWAYS pins its workers (config.pin_workers is ignored by it).
// Blocks whose procedure() can block (hardware refill, blocking I/O) declare
// `static constexpr bool may_block = true;` and automatically get a dedicated
// thread instead of sharing a pool/island worker.

// Optional core pinning for FixedThreadPool (PinnedIslands does not consult this)
config.pin_workers = false;

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

#### Buffer Access Patterns (Benchmarked Performance Order)

**Performance Characteristics**:
- **read_dbf/write_dbf**: True zero-copy - PREFERRED DEFAULT (needs heap channel >= 4KB)
- **ReadN/WriteN**: Good baseline - use when an external API needs its own contiguous buffer
- **Peek/Commit**: ~5% faster than readN/writeN but easy to misuse (forgotten commit, ignored second segment)
- **Push/Pop**: Orders of magnitude slower (AVOID for high-throughput)

```cpp
// TECHNIQUE 1: ReadN/WriteN
// Bulk transfer with single memory copy - simple and performant
size_t transferable = std::min({in.size(), out->space(), BUFFER_SIZE});
in.readN(buffer, transferable);
// Process buffer...
for (size_t i = 0; i < transferable; ++i) {
    buffer[i] *= gain;
}
out->writeN(buffer, transferable);

// TECHNIQUE 2: read_dbf/write_dbf (PREFERRED DEFAULT)
// Doubly-mapped buffers: True zero-copy
// NOTE: If dbf is unavailable (buffer too small or stack-allocated): assert in debug, {nullptr, 0} in release
// Buffer must be >= 4KB (DOUBLY_MAPPED_MIN_SIZE) and heap-allocated
auto [read_ptr, read_size] = in.read_dbf();
auto [write_ptr, write_size] = out->write_dbf();

// Direct processing between doubly-mapped buffers
size_t to_process = std::min(read_size, write_size);
if (to_process > 0 && read_ptr && write_ptr) {
    for (size_t i = 0; i < to_process; ++i) {
        write_ptr[i] = read_ptr[i] * gain; // Process directly
    }
    in.commit_read(to_process);
    out->commit_write(to_process);
}

// TECHNIQUE 3: Peek/Commit (ZERO-COPY READ)
// Inspect before processing, still needs one copy for output
const float* ptr1, *ptr2;
size_t size1, size2;
size_t available = in.peek_read(ptr1, size1, ptr2, size2);
if (available > 0) {
    // Process first segment
    size_t from_seg1 = std::min(size1, BUFFER_SIZE);
    for (size_t i = 0; i < from_seg1; ++i) {
        buffer[i] = ptr1[i] * gain;
    }
    // Handle second segment if needed...
    in.commit_read(processed_count);
    out->writeN(buffer, processed_count);
}

// TECHNIQUE 4: Push/Pop (AVOID)
// Single sample processing - EXTREMELY SLOW
float sample;
in.pop(sample);
sample *= gain;
out->push(sample);
```

**Performance Recommendations**:
- **Use DBF as default** - zero-copy, mandatory for hardware interfaces (SDRs, ADCs, DACs), best for pure data movement and multi-IO blocks
- **Use ReadN/WriteN when** an external API (liquid-dsp, decoders) needs its own contiguous buffer anyway
- **Never use Push/Pop** - Orders of magnitude slower due to per-sample overhead
- **Skip Peek/Commit** - Easy to misuse (forgotten commit, ignored second segment), only ~5% faster than ReadN/WriteN

**Hardware Interface Guidelines**:
- **SDR Source Blocks**: Always use DBF - zero-copy is essential for maintaining sample rates
- **Hardware Sinks**: Use DBF to minimize latency to output devices
- **High-Speed Sensors**: DBF prevents buffer underruns at high data rates
- Examples: HackRF (20 MSPS), USRP (200+ MSPS), high-speed ADCs (100+ MSPS)

**Implementation Trade-offs**:
- **ReadN/WriteN**: Requires allocating and managing temporary buffers, but provides clean separation
- **DBF**: Simpler for multi-IO blocks and critical for hardware interfaces
- Choose based on your use case - hardware interfaces almost always benefit from DBF

### Channel Implementation Notes
- **read_dbf()/write_dbf()**: noexcept; when doubly-mapped buffers are unavailable they assert in debug and return `{nullptr, 0}` in release
- **Requirements**: Buffers must be heap-allocated and page-aligned (minimum DOUBLY_MAPPED_MIN_SIZE = 4KB)
- **No Fallbacks**: desktop_blocks validate buffer size at construction (`cler::panic` if too small) to enforce dbf availability

### Recommended Block Pattern
```cpp
cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
    // DEFAULT PATTERN: DBF zero-copy
    auto [rptr, rsize] = in.read_dbf();
    auto [wptr, wsize] = out->write_dbf();
    size_t n = std::min(rsize, wsize);
    if (n == 0) return cler::Error::NotEnoughSamples;

    for (size_t i = 0; i < n; ++i) {
        wptr[i] = rptr[i] * _gain;
    }

    in.commit_read(n);
    out->commit_write(n);
    return cler::Empty{};
}
```

### Progress Contract (mandatory)

**A successful return means the block moved at least one sample.** Schedulers treat
`cler::Empty{}` as evidence of progress: it resets the idle backoff ladder and wakes
parked workers. A `procedure()` that returns success after doing nothing pins a core at
100% and defeats PinnedIslands entirely.

If you consumed nothing and produced nothing, return an error:
- No input → `cler::Error::NotEnoughSamples`
- No output space → `cler::Error::NotEnoughSpace`
- Either/both, don't care which → `cler::Error::NotEnoughSpaceOrSamples`

These are non-fatal; the framework retries and backs off. This applies to every early-out:
a device timeout, an overflow with no samples recovered, a config-in-progress skip, a
callback-driven block whose `procedure()` is a no-op — all of them return an error, never
`Empty{}`.

**The contract has a second half: a retryable error must mean nothing was consumed.**
`NotEnoughSamples` / `NotEnoughSpace` / `NotEnoughSpaceOrSamples` tell the framework "call
me again"; if the block already committed reads before bailing, that data is counted twice
in the statistics and, on multi-channel hardware, the channels silently lose alignment.
Validate every channel *before* committing on any of them:

```cpp
// WRONG - channel 0 consumed, then a retryable error reported
for (size_t i = 0; i < n; ++i) {
    if (in[i].size() < need) return cler::Error::NotEnoughSamples;
    send(in[i]); in[i].commit_read(need);
}

// RIGHT - check all, then commit all
for (size_t i = 0; i < n; ++i) {
    if (in[i].size() < need) return cler::Error::NotEnoughSamples;
}
for (size_t i = 0; i < n; ++i) { send(in[i]); in[i].commit_read(need); }
```

A block that has already made progress must report success, then surface the shortfall on
the next call. Also watch for paths that *compute* their way to zero — a ratio or frame
size that truncates to `0` items — not just paths guarded on an empty channel.

```cpp
// WRONG - scheduler sees progress, never parks
size_t n = in.size();
in.commit_read(n);
return cler::Empty{};

// RIGHT
size_t n = in.size();
if (n == 0) return cler::Error::NotEnoughSamples;
in.commit_read(n);
return cler::Empty{};
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
Different embedded platforms require different threading models:

```cpp
// Desktop (Linux/macOS): std::thread
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
AddBlock<float, NUM_INPUTS> adder("Adder");  // Input count is a template parameter
GainBlock<float> gain("Gain", gain_value);
ComplexDemuxBlock demux("Demux");

// DSP processing
PolyphaseChannelizerBlock<NUM_CHANNELS, FILTER_SEMILEN> channelizer("PFB", attenuation);
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
    AddBlock<float, 2> adder("Adder");
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
- **Pinning**: optional, via `config.pin_workers = true`

#### PinnedIslands
- **Best for**: Core-constrained targets and imbalanced chains (the Pluto/ARM case)
- **Characteristics**: blocks split into contiguous topo-order islands, one pinned worker per island; costs measured during `calibration_ms`, then one repartition, then a drift check every `repartition_check_ms` (0 disables)
- **Pinning**: **attempted always** — PinnedIslands pins every worker to `cpu_id_offset + worker_id` by design and does not consult `config.pin_workers`. Affinity failures are counted (`affinity_failure_count()`), never fatal. If you want optional pinning, use FixedThreadPool with `pin_workers`.
  - Real pinning exists only where `TaskPolicy::pin_to_core` is implemented: **desktop/Linux** (`pthread_setaffinity_np`, so it works on embedded Linux targets like Pluto and RPi). The FreeRTOS, ThreadX and Zephyr policies inherit the base implementation, which pins nothing and returns `false` — so on those targets `affinity_failure_count()` equals the worker count and the scheduler is "PinnedIslands" in name only. The base used to return `true`, which made a no-op indistinguishable from success.
- **Telemetry**: per-block cost sampling (`block_costs()`) is only collected under this scheduler; ThreadPerBlock/FixedThreadPool skip it and report zeros
- **Idle**: workers escalate through the backoff ladder and then park on a futex with a 1 ms timeout
- **Cost units**: block weight is `ns / items_moved`, and *items moved* is derived automatically — no block-side annotation. Because a producer's output channel is physically owned by the consuming block, edge derivation already identifies each block's inputs; the scheduler counts a block's input consumption where it has resolved inputs, and falls back to output writes otherwise. This keeps every block in the same unit:
  - **sources** have no inputs → output writes
  - **sinks** have no outputs → input reads (previously collapsed to `ns/call`, a different unit)
  - **fanout/channelizer** → input reads, so weight does not shrink as output count grows
  - **multi-input blocks** take the **max of the per-call deltas**, not the sum, since an N-input block consumes N items per input for one item's worth of work. It must be the max of the deltas, never the delta of the lifetime maxima — an input holding the largest lifetime count while a different input advances would otherwise report zero.
  - a block whose input edge failed to resolve, or that has a resolved input it never reads (a control channel), falls back to output writes rather than reporting zero

#### Repartition barrier: the invariant

`sched::RepartitionBarrier` exists to enforce one rule: **block ownership must not
change until every regular worker has stopped executing its old island.** Each
`Channel` is an SPSC queue with exactly one reader and one writer; if a worker is
still running a block while another worker takes ownership of it, that queue
briefly has two consumers and the stream silently duplicates or reorders.

The protocol: a generation counter is packed with an arrival count into one
64-bit word. Every worker CASes its arrival. Non-leaders park on
`_partition_epoch` until the generation advances. The leader spins until
`arrived == worker_count`, and only then repartitions, publishes a new
generation, and bumps the epoch to release the others.

`arrive()` takes `is_leader` explicitly rather than assuming worker 0, and takes
stop / wake / repartition as callables so the barrier owns the protocol and
nothing else.

#### Repartition barrier: how to test changes to it

The generation-keyed barrier transfers SPSC endpoint ownership between workers.
A bug there does not crash — it leaves two workers owning one endpoint, and
surfaces as reordered or duplicated samples. `tests/scheduler/test_repartition_stress.cpp`
drives many repartitions under shifting per-block cost and asserts the stream
stays strictly sequential end to end.

Before changing barrier code, confirm the test still *fails* when the barrier is
broken (delete the leader's wait loop and it must report a backwards jump), and
confirm it fails **repeatably** — run the broken build 5-6 times, not once. The
detector is probabilistic: an earlier, gentler version of this test caught a
broken barrier only 1 run in 6, which is indistinguishable from a passing test
on any single run. Detection depends on the heavy/light cost contrast being
large enough that partitions genuinely change; the current constants detect 6/6.

Run it under ThreadSanitizer too. ASLR breaks TSan on recent kernels, so:

```bash
g++ -std=c++17 -O1 -g -fsanitize=thread -Iinclude stress.cpp -o stress -lpthread
setarch $(uname -m) -R env TSAN_OPTIONS="halt_on_error=0" ./stress
```

TSan slows execution ~10x, which suppresses the drift check — verify the run
actually reached a high `repartition_count()` (hundreds), otherwise it never
exercised the barrier regardless of a clean report.

#### Observability accessors and when they are valid
`partition()`, `stats()`, `block_costs()`, `repartition_count()`, `total_park_events()` and `affinity_failure_count()` are safe to read after `stop()` has joined the workers. During a run they are best-effort: `block_costs()` and the stats counters are approximate (updated by workers without synchronization to the reader), and `partition()` is unsynchronized — a drift repartition can rewrite it while you read. Read them after `stop()` when you need exact values. All of them are reset at the start of every `run()`.

### Execution Statistics and Block Performance
```cpp
// Configure for optimal performance based on workload
cler::FlowGraphConfig config;

// Example 1: Uniform workload (e.g., simple signal processing chain)
config.scheduler = cler::SchedulerType::FixedThreadPool;
config.num_workers = 4;

flowgraph.run(config);

// After stopping, get detailed report with performance metrics
flowgraph.stop();
cler::print_flowgraph_execution_report(flowgraph);

// BlockExecutionStats now includes (calculated post-execution):
// - Successful/failed procedure counts
// - CPU utilization percentage
// - Average execution time per procedure
// - Throughput in samples/second
```

### Benchmarking
```bash
# Run comprehensive performance suite
cd build/performance

# Compare different read/write techniques
./perf_read_write_techniques

# Compare scheduler configurations
./perf_simple_linear_flow

# Compare fanout workload strategies  
./perf_fanout_workloads
```

**Read/Write Technique Performance**:
- **read_dbf/write_dbf**: PREFERRED - zero-copy
- **readN/writeN**: Good - use when external API needs a contiguous scratch buffer
- **peek/commit**: Easy to misuse - Only ~5% faster than readN/writeN, not worth it
- **push/pop**: AVOID - orders of magnitude slower

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
- **Expected**: Good performance, easy debugging

#### Fanout with Uniform Processing (Source → Fanout → [N similar paths] → Sinks)
- **Scheduler**: FixedThreadPool with workers = min(N/2, CPU cores)
- **Expected**: Better than ThreadPerBlock due to reduced thread overhead

#### Fanout with Imbalanced Processing (different complexity per path)
- **Scheduler**: PinnedIslands (cost-based partition isolates the heavy path after calibration)
- **Workers**: CPU cores. Do NOT subtract one for a may_block source.
- **Expected**: Significantly better than FixedThreadPool for imbalanced loads

Measured on a 2-core PlutoSDR (Cortex-A9), `SourcePluto(may_block) -> Mix ->
FIR -> Sink`, at a rate every config could sustain (3 reps, spread +/- 0.004
cores):

| config | CPU cores | meets rate (light chain) | meets rate (loaded chain) |
|---|---|---|---|
| PinnedIslands(1) | 1.405 | yes | **no** (1.723 of 2.083 MSPS) |
| PinnedIslands(2) | 1.419 | yes | **yes** (2.078) |
| ThreadPerBlock | 1.524 | yes | no (1.897) |
| FixedThreadPool(2) | 1.544 | yes | no (1.760) |

`cores - 1` saves ~1% CPU when the chain has slack, and costs 17% of capacity
when it does not. Take the extra worker: the may_block source spends nearly all
its time blocked in the driver (measured at 0.196 cores for a 3 MS/s libiio
source), so it does not need a core reserved for it. `embedded_optimized()` is
`pinned_islands(2)` and is the right default on a 2-core target.

The cost-based repartition is what makes 2 workers win: with the barrier
suppressed, the same config drops to 1.760 MSPS. It only matters when per-block
costs are uneven -- on a balanced chain it is pure overhead.

#### Many Blocks (>20 blocks in flowgraph)
- **Scheduler**: PinnedIslands or FixedThreadPool
- **Workers**: 4-8 depending on CPU
- **Rationale**: ThreadPerBlock creates too many threads

#### Sparse/Intermittent Data (sensors, network packets)
- **Scheduler**: PinnedIslands (idle workers park on a futex instead of spinning)
- **Tuning**: `config.park_after_zero_passes` (default 4) trades wake latency for idle CPU
- **Expected**: near-zero CPU while the graph has nothing to do

### Sizing a block's input channel against a blocking driver

A block downstream of a hardware source must give its input channel **at least
the driver's buffer size**, or the driver's refill stalls the whole graph.

`SourcePlutoBlock` allocates a 16384-sample iio buffer and `iio_buffer_refill`
blocks for roughly one buffer duration -- 5.5 ms at 3 MS/s. With the polyphase
channelizer's default input channel of `DOUBLY_MAPPED_MIN_SIZE/8 * M` = 2560
samples (0.85 ms of stream at that rate) the consumer drained the channel and
starved through every refill, capping the graph at 98% of the required rate.

The symptom is diagnostic and worth recognising: **throughput short of the rate
while the worker sits well under 1.0 core.** That is never a compute problem.
Raising the input channel to 16385 or above fixed it with no other change.

### Polyphase channelizer: batch the span, do not loop per frame

Measured on a PlutoSDR (2x Cortex-A9 @ 667 MHz), M=5, 3.0 MS/s in: the block
went from 194.3 to 600.1 kS/s per port, 3.09x, by replacing a per-frame
`firpfbch_crcf_analyzer_execute` with one batched kernel. The kernel alone is
~10x (1.33 -> 13.3 MS/s on-device).

Almost none of that was arithmetic -- the total real-multiply count only fell
1.7x. The rest was liquid's per-call plumbing: per 5 input samples it pushed 5
`windowcf` buffers, ran 5 dot products through a runtime-selected function
pointer, and dispatched a DFT, all to perform 30 multiply-accumulates.

The structural trick is to fold the subfilter bank over the frame index. liquid
keeps M sliding windows advancing one sample per frame; substituting `k = M-1-i`
into its own output reversal gives

```
X[k](j) = sum over n of h[n*M + M-1-k] * x[(j-n)*M + k]
```

so the taps are contiguous slices of the prototype and the data is `p`
consecutive input frames read **in place from the caller's read span**. No
windows, no per-channel state, no delay line beyond `p-1` carried frames. Both
`M` and the filter semilength are template parameters, so every buffer is a
`std::array` and the block never touches the heap.

General lesson for any liquid-backed block: if you are calling a liquid
`_execute` once per sample or once per frame, the wrapper is probably costing
more than the DSP.

## 12. Development Guidelines & Code Style

### Core Principles
- **Templates over virtual functions** for performance-critical paths
- **Avoid `std::function`** - use function pointers or lambdas when needed
- **Composition over inheritance** except for simple interfaces like `BlockBase`
- **Heavy implementations in `.cpp`** when dealing with single data types

### Style Rules (mandatory)
- **No throw/try/catch in our code.** Recoverable runtime errors → `cler::Result`. Unrecoverable init/invariant failures → `cler::panic(msg)` (`cler_desktop_utils.hpp`; prints + aborts; desktop-only — embedded targets lack printf). try/catch is fine at the boundary with external libraries whose intended API is exception-based (UHD, SoapySDR) — catch their exceptions, never our own.
- **Minimal comments.** Prefer self-evident code. Keep a comment only for non-obvious constraints: hardware quirks, units, protocol/timing requirements, DSP math rationale.
- **Prefer `read_dbf`/`write_dbf` over `readN`/`writeN`** in `procedure()` when the channel is heap-allocated and >= 4KB (always true for desktop_blocks defaults). Mandatory for hardware interfaces. `readN`/`writeN` acceptable when an external API needs a separate contiguous buffer anyway.
- **Never push/pop in hot paths.**

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
1. **CRITICAL: Never allocate memory in `procedure()`** - It's the hot path called repeatedly
   - Generally speaking we prefer c-arrays with new/delete,
   but if there are many failure points, and RAII principles simplify by alot, cpp vectors can be used
2. Use bulk read/write operations (`readN`/`writeN`)
3. Prefer `peek_read`/`peek_write` for zero-copy processing
4. Avoid single-sample `push`/`pop` in hot paths
5. Process multiple samples per `procedure()` call
6. Use compile-time channel sizes when possible
7. Use PinnedIslands when idle CPU matters - its workers park instead of spinning

### Common Pitfalls
1. **Allocating memory in `procedure()`** - Use member variables instead (hot path!)
2. Forgetting task policy include for flowgraph mode
3. Incorrect `peek_write` usage (pass by reference, not pointer)
4. Not checking channel space before writing
5. Missing `BlockRunner` for a block
6. Type mismatch between connected channels
7. Not handling terminal errors appropriately

This comprehensive guide provides accurate context for AI assistants working with the Cler DSP framework, with all corrections applied based on the actual codebase structure and API usage.