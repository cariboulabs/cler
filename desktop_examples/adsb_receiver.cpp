#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/sources/source_hackrf.hpp"
#include "desktop_blocks/adsb/adsb_decoder.hpp"
#include "desktop_blocks/adsb/adsb_aggregate.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>

// Include HackRF header for hackrf_init/hackrf_exit
#ifdef __has_include
    #if __has_include(<libhackrf/hackrf.h>)
        #include <libhackrf/hackrf.h>
    #elif __has_include(<hackrf.h>)
        #include <hackrf.h>
    #endif
#endif

/**
 * ADSB Receiver Example
 *
 * Real-time ADS-B aircraft tracking using HackRF SDR.
 *
 * This example demonstrates:
 * 1. Receiving IQ samples from HackRF at 1090 MHz (ADS-B frequency)
 * 2. Converting complex IQ to magnitude samples
 * 3. Decoding Mode S messages using ADSBDecoderBlock
 * 4. Aggregating aircraft states and rendering an interactive map
 *
 * Usage:
 *   ./adsb_receiver [latitude] [longitude]
 *
 * Arguments:
 *   latitude  - Initial map center latitude (default: 32.0)
 *   longitude - Initial map center longitude (default: 34.0)
 *
 * Example:
 *   ./adsb_receiver              # Default: Israel (32.0°N, 34.0°E)
 *   ./adsb_receiver 37.7 -122.4  # San Francisco
 *   ./adsb_receiver 51.5 -0.1    # London
 */

// Block to convert complex<float> IQ to uint16_t magnitude
struct IQToMagnitudeBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> iq_in;

    IQToMagnitudeBlock(const char* name, size_t buffer_size = 65536)
        : BlockBase(name), iq_in(buffer_size) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint16_t>* mag_out) {
        auto [read_ptr, read_size] = iq_in.read_dbf();
        if (read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_size] = mag_out->write_dbf();
        if (write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t to_process = std::min(read_size, write_size);

        // Convert complex IQ to magnitude: sqrt(I^2 + Q^2) scaled to uint16_t
        for (size_t i = 0; i < to_process; ++i) {
            float i_val = read_ptr[i].real();
            float q_val = read_ptr[i].imag();
            float mag = std::sqrt(i_val * i_val + q_val * q_val);

            // Scale to uint16_t range (0-65535)
            mag = std::min(65535.0f, mag * 256.0f);  // Scale factor may need tuning
            write_ptr[i] = static_cast<uint16_t>(mag);
        }

        iq_in.commit_read(to_process);
        mag_out->commit_write(to_process);

        return cler::Empty{};
    }
};

// Optional callback: called when aircraft state updates
void on_aircraft_update(const ADSBState& state, void* context) {
    // Print updates to console
    std::cout << "Aircraft detected: ICAO 0x" << std::hex << state.icao << std::dec;
    if (state.callsign[0] != '\0') {
        std::cout << " | Callsign: " << state.callsign;
    }
    if (state.altitude > 0) {
        std::cout << " | Alt: " << state.altitude << " ft";
    }
    if (state.groundspeed > 0) {
        std::cout << " | Speed: " << static_cast<int>(state.groundspeed) << " kts";
    }
    std::cout << " | Messages: " << state.message_count << std::endl;
}

int main(int argc, char** argv) {
    // Parse command line arguments for initial map position
    float initial_lat = 32.0f;   // Default: Israel
    float initial_lon = 34.0f;

    if (argc >= 3) {
        initial_lat = std::atof(argv[1]);
        initial_lon = std::atof(argv[2]);
    }

    std::cout << "=== ADSB Receiver ===" << std::endl;
    std::cout << "Map center: " << initial_lat << "°N, " << initial_lon << "°E" << std::endl;
    std::cout << std::endl;

    // ADS-B frequency and settings
    constexpr uint64_t ADSB_FREQ_HZ = 1090000000;  // 1090 MHz
    constexpr uint32_t SAMPLE_RATE_HZ = 2000000;   // 2 MSPS
    constexpr int LNA_GAIN_DB = 32;                // LNA gain (0-40 dB)
    constexpr int VGA_GAIN_DB = 40;                // VGA gain (0-62 dB)

    std::cout << "Configuring HackRF:" << std::endl;
    std::cout << "  Frequency: " << ADSB_FREQ_HZ / 1e6 << " MHz" << std::endl;
    std::cout << "  Sample Rate: " << SAMPLE_RATE_HZ / 1e6 << " MSPS" << std::endl;
    std::cout << "  LNA Gain: " << LNA_GAIN_DB << " dB" << std::endl;
    std::cout << "  VGA Gain: " << VGA_GAIN_DB << " dB" << std::endl;
    std::cout << std::endl;

    try {
        // Initialize HackRF library
        if (hackrf_init() != HACKRF_SUCCESS) {
            std::cerr << "Failed to initialize HackRF library" << std::endl;
            return 1;
        }

        // Initialize GUI
        cler::GuiManager gui(1400, 800, "ADSB Aircraft Tracker");

        // Create blocks
        SourceHackRFBlock hackrf("HackRF", ADSB_FREQ_HZ, SAMPLE_RATE_HZ, LNA_GAIN_DB, VGA_GAIN_DB);
        IQToMagnitudeBlock mag_converter("IQ to Magnitude");
        ADSBDecoderBlock decoder("ADSB Decoder", 1 << 17);  // DF17 only (Extended Squitter)

        ADSBAggregateBlock aggregator(
            "ADSB Map",
            initial_lat, initial_lon,
            on_aircraft_update,
            nullptr
            // Coastline path defaults to "adsb_coastlines/ne_110m_coastline.shp"
        );

        // Configure window
        aggregator.set_initial_window(0.0f, 0.0f, 1400.0f, 800.0f);

        // Create flowgraph: HackRF → Magnitude → Decoder → Aggregator
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&hackrf, &mag_converter.iq_in),
            cler::BlockRunner(&mag_converter, &decoder.magnitude_in),
            cler::BlockRunner(&decoder, &aggregator.message_in),
            cler::BlockRunner(&aggregator)
        );

        // Start the flowgraph
        std::cout << "Starting receiver..." << std::endl;
        flowgraph.run();

        std::cout << "Tracking aircraft. Close window to exit." << std::endl;
        std::cout << "Controls:" << std::endl;
        std::cout << "  - Mouse wheel: zoom in/out" << std::endl;
        std::cout << "  - Right-click drag: pan map" << std::endl;
        std::cout << std::endl;

        // Main GUI loop
        while (!gui.should_close()) {
            gui.begin_frame();
            aggregator.render();
            gui.end_frame();

            // Sleep to avoid excessive CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 FPS
        }

        std::cout << "Shutting down..." << std::endl;
        flowgraph.stop();

        std::cout << "Total aircraft tracked: " << aggregator.aircraft_count() << std::endl;

        // Cleanup HackRF library
        hackrf_exit();

    } catch (const std::exception& e) {
        hackrf_exit();
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << std::endl;
        std::cerr << "Make sure:" << std::endl;
        std::cerr << "  1. HackRF device is connected" << std::endl;
        std::cerr << "  2. You have permissions to access USB devices" << std::endl;
        std::cerr << "     (You may need to run with sudo or add udev rules)" << std::endl;
        return 1;
    }

    return 0;
}
