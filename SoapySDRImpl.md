# SoapySDR Implementation Plan

Simple plan to add SoapySDR source and sink blocks to CLER.

## Files to Create

### 1. `desktop_blocks/sources/source_soapysdr_block.hpp`
```cpp
#pragma once
#include "cler.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <complex>
#include <vector>

template<typename T>
struct SourceSoapySDRBlock : public cler::BlockBase {
    // No input channels
    
    SourceSoapySDRBlock(const char* name, 
                        const std::string& args,
                        double freq,
                        double rate,
                        double gain = 20.0);
    ~SourceSoapySDRBlock();
    
    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs);
    
    void set_frequency(double freq);
    void set_gain(double gain);
    void set_sample_rate(double rate);
    
private:
    SoapySDR::Device* device;
    SoapySDR::Stream* stream;
    std::vector<T> buffer;
    size_t mtu;
};
```

### 2. `desktop_blocks/sinks/sink_soapysdr_block.hpp`
```cpp
#pragma once
#include "cler.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <complex>
#include <vector>

template<typename T>
struct SinkSoapySDRBlock : public cler::BlockBase {
    cler::Channel<T> in;
    
    SinkSoapySDRBlock(const char* name,
                      const std::string& args,
                      double freq,
                      double rate,
                      double gain = 0.0,
                      size_t channel_size = 8192);
    ~SinkSoapySDRBlock();
    
    cler::Result<cler::Empty, cler::Error> procedure();
    
    void set_frequency(double freq);
    void set_gain(double gain);
    void set_sample_rate(double rate);
    
private:
    SoapySDR::Device* device;
    SoapySDR::Stream* stream;
    std::vector<T> buffer;
    size_t mtu;
};
```

### 3. `desktop_examples/soapysdr_example.cpp`
Basic FM radio example using RTL-SDR:
```cpp
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_soapysdr_block.hpp"
#include "desktop_blocks/plots/plot_cspectrum_block.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

int main() {
    cler::GuiManager gui(800, 600, "SoapySDR FM Radio");
    
    // RTL-SDR source tuned to FM radio
    SourceSoapySDRBlock<std::complex<float>> sdr_source(
        "RTL-SDR",
        "driver=rtlsdr",  // Device args
        100.3e6,          // 100.3 MHz
        2.048e6,          // 2.048 MSPS
        20.0              // 20 dB gain
    );
    
    PlotCSpectrumBlock spectrum("FM Spectrum", {"Signal"}, 2.048e6, 2048);
    
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&sdr_source, &spectrum.in[0]),
        cler::BlockRunner(&spectrum)
    );
    
    flowgraph.run();
    
    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
    }
    
    flowgraph.stop();
    return 0;
}
```

## CMake Changes

### `desktop_blocks/CMakeLists.txt`
Add after liquid-dsp check:
```cmake
# Check for SoapySDR
find_package(SoapySDR QUIET)
if(SoapySDR_FOUND)
    message(STATUS "SoapySDR found - building SDR blocks")
    target_sources(cler_desktop_blocks INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/sources/source_soapysdr_block.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/sinks/sink_soapysdr_block.hpp
    )
    target_link_libraries(cler_desktop_blocks INTERFACE SoapySDR::SoapySDR)
    target_compile_definitions(cler_desktop_blocks INTERFACE CLER_HAS_SOAPYSDR)
else()
    message(STATUS "SoapySDR not found - SDR blocks disabled")
endif()
```

### `desktop_examples/CMakeLists.txt`
Add after other examples:
```cmake
if(TARGET SoapySDR::SoapySDR)
    add_executable(soapysdr_example soapysdr_example.cpp)
    target_link_libraries(soapysdr_example PRIVATE cler::cler_desktop_blocks)
endif()
```

## Implementation Notes

1. **Keep it simple** - Just basic receive/transmit functionality
2. **Use existing patterns** - Follow HackRF/CaribouLite block structure
3. **Header-only** - Keep implementation in headers like other desktop blocks
4. **Error handling** - Return proper CLER errors, don't throw exceptions
5. **Buffer sizes** - Use MTU from device for optimal performance

## Testing

1. **Manual testing** - Requires actual SDR hardware
2. **CI skips these** - No hardware in CI environment
3. **Example is self-contained** - If it compiles and runs, it works

## Device Support

SoapySDR automatically supports:
- RTL-SDR (driver=rtlsdr)
- HackRF (driver=hackrf)
- LimeSDR (driver=lime)
- USRP (driver=uhd)
- BladeRF (driver=bladerf)
- Airspy (driver=airspy)
- And many more...

## Usage Example

```cpp
// List devices
SoapySDRUtil --find

// Use specific device
SourceSoapySDRBlock<std::complex<float>> source(
    "MySDR",
    "driver=rtlsdr,serial=00000001",  // Specific device by serial
    433.92e6,                          // 433 MHz ISM band
    2e6,                               // 2 MSPS
    30.0                               // 30 dB gain
);
```