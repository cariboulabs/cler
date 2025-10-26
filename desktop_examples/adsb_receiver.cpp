#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/adsb/adsb_decoder.hpp"
#include "desktop_blocks/adsb/adsb_aggregate.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <variant>
#include <string>
#include <fstream>
#include <vector>

struct IQToMagnitudeBlock : public cler::BlockBase {
    cler::Channel<std::complex<uint8_t>> in;

    // Statistics
    float sliding_window_mag = 0.0f;
    size_t sample_counter = 0;

    IQToMagnitudeBlock(const char* name)
        : BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<uint8_t>)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint16_t>* mag_out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_size] = mag_out->write_dbf();
        if (write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t to_process = std::min(read_size, write_size);

        for (size_t i = 0; i < to_process; i++) {
            // Compute magnitude directly from complex<uint16_t>
            // Simple approach: just compute magnitude and scale it
            const float mag = std::abs(read_ptr[i]);

            // Update statistics
            sliding_window_mag = sliding_window_mag * 0.99f + mag * 0.01f;
            sample_counter++;

            write_ptr[i] = mag;
        }

        if (sample_counter >= 10000) {
            std::cout << "[IQToMagnitude] Avg magnitude: " << (int)sliding_window_mag
                      << " (ratio: " << (sliding_window_mag / 65535.0f) << ")" << std::endl;
            std::cout.flush();
            sample_counter = 0;
        }

        in.commit_read(to_process);
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

using SoapyTypeCU8 = SourceSoapySDRBlock<std::complex<uint8_t>>;
using FileTypeCU8 = SourceFileBlock<std::complex<uint8_t>>;
using SourceVariant = std::variant<SoapyTypeCU8, FileTypeCU8>;

// Helper to create variant with proper initialization
inline auto make_source_variant(bool use_soapy, const std::string& device_args_or_filename,
                                uint64_t freq, uint32_t rate, double gain) {
    if (use_soapy) {
        return SourceVariant(std::in_place_type<SoapyTypeCU8>,
                                    "SoapySourceCU8", device_args_or_filename, freq, rate, gain);
        } else {
        return SourceVariant(std::in_place_type<FileTypeCU8>,
                                    "FileSourceCU8", device_args_or_filename.c_str(), true);
    }
}

// Variant-based source selector block
struct SelectableSourceBlock : public cler::BlockBase {
    std::variant<
    SourceSoapySDRBlock<std::complex<uint8_t>>,
    SourceFileBlock<std::complex<uint8_t>>
    > source;


    SelectableSourceBlock(const char* name, bool use_soapy, const std::string& device_args_or_filename,
                         uint64_t freq, uint32_t rate, double gain)
        : cler::BlockBase(name),
          source(make_source_variant(use_soapy, device_args_or_filename, freq, rate, gain))
    {
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<uint8_t>>* out) {
        return std::visit([&](auto& src) {
            return src.procedure(out);
        }, source);
    }
};

int main(int argc, char** argv) {
    // Show help if missing source argument
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <source> [latitude] [longitude]\n";
        std::cout << "\nArguments:\n";
        std::cout << "  source    - \"soapy\" for auto-detected SoapySDR device, or path to IQ file\n";
        std::cout << "  latitude  - Initial map center latitude (default: 32.0)\n";
        std::cout << "  longitude - Initial map center longitude (default: 34.0)\n";
        std::cout << "\nExamples:\n";
        std::cout << "  " << argv[0] << " soapy\n";
        std::cout << "  " << argv[0] << " adsb_recording.bin\n";
        std::cout << "  " << argv[0] << " soapy 37.7 -122.4\n";
        return 0;
    }

    // Parse command line arguments
    std::string source_arg = argv[1];
    float initial_lat = 32.0f;   // Default: Israel
    float initial_lon = 34.0f;

    if (argc >= 3) {
        initial_lat = std::atof(argv[3]);
        initial_lon = std::atof(argv[4]);
    }

    std::cout << "=== ADSB Receiver ===" << std::endl;
    std::cout << "Map center: " << initial_lat << "°N, " << initial_lon << "°E" << std::endl;
    std::cout << std::endl;

    // ADS-B frequency and settings
    constexpr uint64_t ADSB_FREQ_HZ = 1090000000;  // 1090 MHz
    constexpr uint32_t SAMPLE_RATE_HZ = 2000000;   // 2 MSPS
    constexpr double GAIN_DB = 30.0;               // RX gain in dB

    bool use_soapy = (source_arg == "soapy");

    if (use_soapy) {
        std::cout << "Source: SoapySDR (auto-detected)" << std::endl;
        std::cout << "  Frequency: " << ADSB_FREQ_HZ / 1e6 << " MHz" << std::endl;
        std::cout << "  Sample Rate: " << SAMPLE_RATE_HZ / 1e6 << " MSPS" << std::endl;
        std::cout << "  Gain: " << GAIN_DB << " dB" << std::endl;
    } else {
        std::cout << "Source: File playback" << std::endl;
        std::cout << "  File: " << source_arg << std::endl;
    }
    std::cout << std::endl;

    try {
        // Initialize GUI
        cler::GuiManager gui(1400, 800, "ADSB Aircraft Tracker");

        std::string device_args_or_filename = "";
        if (use_soapy) {
            //need to ask for u16 if dtype is cu16?
        } else {
            device_args_or_filename = source_arg;
        }

        // Create blocks
        SelectableSourceBlock source(
            "Source",
            use_soapy,
            use_soapy ? "" : source_arg,  // Empty string for auto-detect, or filename
            ADSB_FREQ_HZ,
            SAMPLE_RATE_HZ,
            GAIN_DB
        );

        IQToMagnitudeBlock iq2mag("IQ to Magnitude");
        ADSBDecoderBlock decoder("ADSB Decoder", 0xFFFF); //all messages
        SinkNullBlock<uint16_t> null_sink("Null Sink");

        ADSBAggregateBlock aggregator(
            "ADSB Map",
            initial_lat, initial_lon,
            on_aircraft_update,
            nullptr
            // Coastline path defaults to "adsb_coastlines/ne_110m_coastline.shp"
        );

        // Configure window
        aggregator.set_initial_window(0.0f, 0.0f, 1400.0f, 800.0f);

        // Create flowgraph with debug counter between decoder and aggregator
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &iq2mag.in),
            cler::BlockRunner(&iq2mag, &decoder.in),
            
            // cler::BlockRunner(&null_sink)

            cler::BlockRunner(&decoder, &aggregator.in),
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
        std::cerr << "[DONE] Receiver completed successfully" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << std::endl;
        if (use_soapy) {
            std::cerr << "Make sure:" << std::endl;
            std::cerr << "  1. SoapySDR device is connected" << std::endl;
            std::cerr << "  2. SoapySDR drivers are installed for your device" << std::endl;
            std::cerr << "  3. You have permissions to access USB devices" << std::endl;
        }
        return 1;
    }

    return 0;
}
