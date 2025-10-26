#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/adsb/adsb_decoder.hpp"
#include "desktop_blocks/adsb/adsb_aggregate.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/sinks/sink_file.hpp"

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

using SoapyTypeCS16 = SourceSoapySDRBlock<std::complex<int16_t>>;
using FileTypeCS16 = SourceFileBlock<std::complex<int16_t>>;
using SourceVariant = std::variant<SoapyTypeCS16, FileTypeCS16>;

// Helper to create variant with proper initialization
inline auto make_source_variant(bool use_soapy, const std::string& device_args_or_filename,
                                uint64_t freq, uint32_t rate, double gain) {
    if (use_soapy) {
        return SourceVariant(std::in_place_type<SoapyTypeCS16>,
                                    "SoapySourceCS16", device_args_or_filename, freq, rate, gain);
    } else {
        return SourceVariant(std::in_place_type<FileTypeCS16>,
                                    "FileSourceCS16", device_args_or_filename.c_str(), true);
    }
}

// Variant-based source selector block
struct SelectableSourceBlock : public cler::BlockBase {
    SourceVariant source;

    SelectableSourceBlock(const char* name, bool use_soapy, const std::string& device_args_or_filename,
                         uint64_t freq, uint32_t rate, double gain)
        : cler::BlockBase(name),
          source(make_source_variant(use_soapy, device_args_or_filename, freq, rate, gain))
    {
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<int16_t>>* out) {
        return std::visit([&](auto& src) {
            return src.procedure(out);
        }, source);
    }
};

struct IQToMagnitudeBlock : public cler::BlockBase {
    cler::Channel<std::complex<int16_t>> in;

    // DC offset removal filter state (1 Hz high-pass)
    float z1_I = 0.0f;
    float z1_Q = 0.0f;
    float dc_a;
    float dc_b;

    IQToMagnitudeBlock(const char* name, uint32_t sample_rate = 2000000)
        : BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<int16_t>)) {

        // Initialize DC filter coefficients (1 Hz high-pass filter)
        dc_b = expf(-2.0f * M_PI * 1.0f / sample_rate);
        dc_a = 1.0f - dc_b;
    }

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

        for (size_t k = 0; k < to_process; k++) {
            // Normalize int16_t to [-1.0, 1.0] range
            float fI = read_ptr[k].real() / 32768.0f;
            float fQ = read_ptr[k].imag() / 32768.0f;

            // DC offset removal (1 Hz high-pass filter)
            z1_I = fI * dc_a + z1_I * dc_b;
            z1_Q = fQ * dc_a + z1_Q * dc_b;
            fI -= z1_I;
            fQ -= z1_Q;

            // Compute magnitude squared
            float magsq = fI * fI + fQ * fQ;

            // Clamp to [0, 1]
            if (magsq > 1.0f) magsq = 1.0f;

            // Scale to uint16_t range [0, 65535]
            float mag = sqrtf(magsq) * 65535.0f + 0.5f;

            write_ptr[k] = static_cast<uint16_t>(mag);
        }

        in.commit_read(to_process);
        mag_out->commit_write(to_process);

        return cler::Empty{};
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
    constexpr uint64_t ADSB_FREQ_HZ = 1'090'000'000;  // 1090 MHz
    constexpr uint32_t SAMPLE_RATE_HZ = 2'000'000;   // 2 MSPS
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
        if (!use_soapy) {
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

        IQToMagnitudeBlock iq2mag("IQ to Magnitude", SAMPLE_RATE_HZ);
        ADSBDecoderBlock decoder("ADSB Decoder", 0xFFFF); //all messages
        
        
        SinkNullBlock<uint16_t> null_sink("Null Sink");
        FanoutBlock<uint16_t> fanout("Fanout", 2);
        SinkFileBlock<uint16_t> file_sink("File Sink", "adsb_magnitudes.bin");

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

            cler::BlockRunner(&iq2mag, &fanout.in),
            
            cler::BlockRunner(&fanout, &file_sink.in, &decoder.in),
            cler::BlockRunner(&file_sink),
            
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
