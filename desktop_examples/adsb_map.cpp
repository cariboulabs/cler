#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/adsb/adsb_decoder.hpp"
#include "desktop_blocks/adsb/adsb_aggregate.hpp"
#include <iostream>
#include <chrono>

/**
 * ADSB Map Example
 *
 * This example demonstrates:
 * 1. Reading magnitude samples from a binary file using SourceFileBlock
 * 2. Decoding Mode S messages using ADSBDecoderBlock
 * 3. Aggregating aircraft states and rendering an interactive map using ADSBAggregateBlock
 *
 * To run this example, you need a binary file containing uint16_t magnitude samples.
 * Example: Download ADSB recording from:
 *   https://github.com/antirez/dump1090/tree/master/samples
 * Or convert a recorded signal using:
 *   rtl_sdr -f 1090000000 -s 2000000 -g 50 output.iq
 *   (then convert I/Q to magnitude samples)
 *
 * Usage:
 *   ./adsb_map <binary_file_path>
 */

// Optional callback: called when aircraft state updates
void on_aircraft_update(const ADSBState& state, void* context) {
    // Silently track updates (logging for every message would be verbose)
    // Uncomment below for debug output:
    // std::cout << "ICAO: 0x" << std::hex << state.icao << std::dec << " | "
    //           << state.callsign << " | Alt: " << state.altitude << " ft" << std::endl;
}

int main(int argc, char** argv) {
    // Input validation
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <binary_magnitude_file>" << std::endl;
        std::cerr << "Example: " << argv[0] << " samples.bin" << std::endl;
        return 1;
    }

    const char* input_file = argv[1];
    std::cout << "Loading ADSB data from: " << input_file << std::endl;

    // Initialize GUI
    cler::GuiManager gui(1400, 800, "ADSB Aircraft Map");

    // Create blocks
    // SourceFileBlock reads magnitude samples from binary file
    SourceFileBlock<uint16_t> source("Magnitude Source", input_file, true);  // true = repeat/loop file

    // ADSBDecoderBlock decodes Mode S messages
    // Filter: 1 << 17 = DF17 only (Extended Squitter - most common for ADS-B)
    // Alternative filters:
    //   0 = all DF types
    //   (1 << 17) | (1 << 18) = DF17 and DF18
    ADSBDecoderBlock decoder("ADSB Decoder", 1 << 17);

    // ADSBAggregateBlock aggregates messages and renders map
    ADSBAggregateBlock aggregator(
        "ADSB Map",
        32.0f, 34.0f,                                          // Initial map center (Israel)
        on_aircraft_update,                                    // Optional state-change callback
        nullptr                                                // Callback context (nullptr for this example)
        // Coastline path defaults to "adsb_data/ne_110m_coastline.shp"
    );

    // Configure window positions
    aggregator.set_initial_window(0.0f, 0.0f, 1400.0f, 800.0f);

    // Create flowgraph: source → decoder → aggregator
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &decoder.magnitude_in),      // source → decoder.magnitude_in
        cler::BlockRunner(&decoder, &aggregator.message_in),   // decoder → aggregator.message_in
        cler::BlockRunner(&aggregator)                          // aggregator (sink, no outputs)
    );

    // Start the flowgraph in background thread
    std::cout << "Starting flowgraph..." << std::endl;
    flowgraph.run();

    // Main GUI loop
    std::cout << "Rendering map. Close window to exit." << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  - Mouse wheel: zoom in/out" << std::endl;
    std::cout << "  - Right-click drag: pan map" << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();

        // Render the interactive map
        aggregator.render();

        gui.end_frame();

        // Sleep to avoid excessive CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 FPS
    }

    std::cout << "Shutting down..." << std::endl;
    return 0;
}
