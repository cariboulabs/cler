#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/adsb/adsb_decoder.hpp"
#include "desktop_blocks/adsb/adsb_aggregate.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <variant>
#include <string>
#include <fstream>
#include <vector>

// Block to decimate from 40 MHz to 2 MHz and convert to magnitude
// Decimation by 20: keeps every 20th sample (40MHz / 20 = 2MHz)
struct IQToMagnitudeBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> iq_in;
    size_t sample_count = 0;
    uint16_t min_mag = 65535;
    uint16_t max_mag = 0;
    float running_max = 1e-6f;  // Running max for normalization
    static constexpr float ALPHA = 0.001f;  // Smoothing factor for running max
    static constexpr int DECIMATION = 20;  // 40 MHz → 2 MHz

    IQToMagnitudeBlock(const char* name, size_t buffer_size = 65536)
        : BlockBase(name), iq_in(buffer_size) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint16_t>* mag_out) {
        auto [read_ptr, read_size] = iq_in.read_dbf();
        if (read_size < DECIMATION) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_size] = mag_out->write_dbf();
        if (write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Process samples with decimation: keep every 20th sample (40MHz → 2MHz)
        size_t output_count = 0;
        for (size_t i = 0; i + DECIMATION <= read_size && output_count < write_size; i += DECIMATION) {
            float i_val = read_ptr[i].real();
            float q_val = read_ptr[i].imag();
            float mag = std::sqrt(i_val * i_val + q_val * q_val);

            // Update running maximum for statistics only
            running_max = (1.0f - ALPHA) * running_max + ALPHA * mag;

            // Scale by 64x to match libmodes expected input range (like RTL-SDR 8-bit magnitude)
            float scaled = mag * 64.0f;
            write_ptr[output_count] = static_cast<uint16_t>(std::min(65535.0f, scaled));

            min_mag = std::min(min_mag, write_ptr[output_count]);
            max_mag = std::max(max_mag, write_ptr[output_count]);

            output_count++;
        }

        sample_count += read_size;
        if (sample_count % 40000000 == 0) {  // Log every 40M input samples (2M output)
            std::cerr << "[IQToMagnitude] Processed: " << sample_count << " input samples (" << sample_count/DECIMATION << " output) | Min: " << min_mag << " Max: " << max_mag << " | running_max: " << running_max << std::endl;
            std::cerr.flush();
        }

        iq_in.commit_read(read_size);
        mag_out->commit_write(output_count);

        return cler::Empty{};
    }
};

// Debug block to monitor messages between decoder and aggregator
struct DebugMessageCounterBlock : public cler::BlockBase {
    cler::Channel<mode_s_msg> msg_in;
    size_t msg_count = 0;
    static constexpr size_t LOG_INTERVAL = 10000;

    DebugMessageCounterBlock(const char* name, size_t buffer_size = 1024)
        : BlockBase(name), msg_in(buffer_size) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<mode_s_msg>* msg_out) {
        size_t available = msg_in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // Read all available messages
        std::vector<mode_s_msg> buffer(available);
        msg_in.readN(buffer.data(), available);

        // Forward to output and count
        for (const auto& msg : buffer) {
            if (msg_out->space() > 0) {
                msg_out->push(msg);
                msg_count++;

                if (msg_count % LOG_INTERVAL == 0) {
                    std::cerr << "[MessageCounter] Total messages: " << msg_count << std::endl;
                    std::cerr.flush();
                }
            }
        }

        return cler::Empty{};
    }
};

// Global stats
static size_t g_total_messages = 0;
static size_t g_valid_messages = 0;

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

// Helper to create variant with proper initialization
inline auto make_source_variant(bool use_soapy, const std::string& device_args_or_filename,
                                uint64_t freq, uint32_t rate, double gain) {
    using SoapyType = SourceSoapySDRBlock<std::complex<float>>;
    using FileType = SourceFileBlock<std::complex<float>>;

    if (use_soapy) {
        return std::variant<SoapyType, FileType>(
            std::in_place_type<SoapyType>, "SoapySDR", device_args_or_filename, freq, rate, gain, 0);
    } else {
        return std::variant<SoapyType, FileType>(
            std::in_place_type<FileType>, "File", device_args_or_filename.c_str(), true);
    }
}

// Variant-based source selector block
struct SelectableSourceBlock : public cler::BlockBase {
    std::variant<SourceSoapySDRBlock<std::complex<float>>, SourceFileBlock<std::complex<float>>> source;

    SelectableSourceBlock(const char* name, bool use_soapy, const std::string& device_args_or_filename,
                         uint64_t freq = 0, uint32_t rate = 0, double gain = 0)
        : cler::BlockBase(name),
          source(make_source_variant(use_soapy, device_args_or_filename, freq, rate, gain))
    {
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
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

    if (argc >= 4) {
        initial_lat = std::atof(argv[2]);
        initial_lon = std::atof(argv[3]);
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

        // Create blocks
        SelectableSourceBlock source(
            "Source",
            use_soapy,
            use_soapy ? "" : source_arg,  // Empty string for auto-detect, or filename
            ADSB_FREQ_HZ,
            SAMPLE_RATE_HZ,
            GAIN_DB
        );

        IQToMagnitudeBlock mag_converter("IQ to Magnitude");
        ADSBDecoderBlock decoder("ADSB Decoder", 0xFFFF); //all messages
        DebugMessageCounterBlock debug_counter("MessageCounter");

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
            cler::BlockRunner(&source, &mag_converter.iq_in),
            cler::BlockRunner(&mag_converter, &decoder.magnitude_in),
            cler::BlockRunner(&decoder, &debug_counter.msg_in),
            cler::BlockRunner(&debug_counter, &aggregator.message_in),
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
