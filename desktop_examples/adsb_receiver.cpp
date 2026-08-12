#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <optional>
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/adsb/adsb_decoder.hpp"
#include "desktop_blocks/adsb/adsb_aggregate.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include <cmath>

void on_aircraft_update(const ADSBState&, void*) {}

using SoapyTypeCS16 = SourceSoapySDRBlock<std::complex<int16_t>>;
using FileTypeCS16 = SourceFileBlock<std::complex<int16_t>>;
using SourceVariant = std::variant<SoapyTypeCS16, FileTypeCS16>;

inline auto make_source_variant(bool use_soapy, const std::string& device_args_or_filename,
                                uint64_t freq, uint32_t rate, double gain) {
    if (use_soapy) {
        return SourceVariant(std::in_place_type<SoapyTypeCS16>,
                                    "SoapySourceCS16", device_args_or_filename, freq, rate, gain);
    } else {
        return SourceVariant(std::in_place_type<FileTypeCS16>,
                                    "FileSourceCS16", device_args_or_filename.c_str(), false);
    }
}

// Selects a live SoapySDR device or a recorded IQ file at construction, same procedure() either way
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

    // 1 Hz high-pass DC offset removal filter state
    float z1_I = 0.0f;
    float z1_Q = 0.0f;
    float dc_a;
    float dc_b;

    IQToMagnitudeBlock(const char* name, uint32_t sample_rate = 2000000)
        : BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<int16_t>)) {
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
            float fI = read_ptr[k].real() / 32768.0f;
            float fQ = read_ptr[k].imag() / 32768.0f;

            z1_I = fI * dc_a + z1_I * dc_b;
            z1_Q = fQ * dc_a + z1_Q * dc_b;
            fI -= z1_I;
            fQ -= z1_Q;

            float magsq = fI * fI + fQ * fQ;
            if (magsq > 1.0f) magsq = 1.0f;

            float mag = sqrtf(magsq) * 65535.0f + 0.5f;
            write_ptr[k] = static_cast<uint16_t>(mag);
        }

        in.commit_read(to_process);
        mag_out->commit_write(to_process);

        return cler::Empty{};
    }
};

int main(int argc, char** argv) {
    std::string source_arg;
    float initial_lat = 32.0f;   // Default: Israel
    float initial_lon = 34.0f;
    float sample_rate_mhz = -1.0f;  // No default - must be specified

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--source" && i + 1 < argc) {
            source_arg = argv[++i];
        } else if (arg == "--lat" && i + 1 < argc) {
            initial_lat = std::atof(argv[++i]);
        } else if (arg == "--lon" && i + 1 < argc) {
            initial_lon = std::atof(argv[++i]);
        } else if (arg == "--rate" && i + 1 < argc) {
            sample_rate_mhz = std::atof(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  --source <src>  - \"soapy\" for SoapySDR device, or path to IQ file (required)\n";
            std::cout << "  --rate <rate>   - Sample rate in MHz: 2 or 2.4 (required)\n";
            std::cout << "  --lat <lat>     - Map center latitude (default: 32.0)\n";
            std::cout << "  --lon <lon>     - Map center longitude (default: 34.0)\n";
            std::cout << "\nExamples:\n";
            std::cout << "  " << argv[0] << " --source soapy --rate 2\n";
            std::cout << "  " << argv[0] << " --source adsb_recording.bin --rate 2.4\n";
            std::cout << "  " << argv[0] << " --source soapy --rate 2.4 --lat 37.7 --lon -122.4\n";
            return 0;
        }
    }

    if (source_arg.empty()) {
        std::cerr << "Error: --source argument is required\n";
        std::cerr << "Run with --help for usage information\n";
        return 1;
    }

    if (sample_rate_mhz < 0.0f) {
        std::cerr << "Error: --rate argument is required (2 or 2.4)\n";
        std::cerr << "Run with --help for usage information\n";
        return 1;
    }

    if (sample_rate_mhz != 2.0f && sample_rate_mhz != 2.4f) {
        std::cerr << "Error: --rate must be either 2 or 2.4\n";
        return 1;
    }

    std::cout << "=== ADSB Receiver ===" << std::endl;
    std::cout << "Map center: " << std::fabs(initial_lat)
              << "°" << (initial_lat >= 0.0f ? 'N' : 'S') << ", "
              << std::fabs(initial_lon)
              << "°" << (initial_lon >= 0.0f ? 'E' : 'W') << std::endl;
    std::cout << std::endl;

    // ADS-B frequency and settings
    constexpr uint64_t ADSB_FREQ_HZ = 1'090'000'000;  // 1090 MHz
    uint32_t SAMPLE_RATE_HZ = static_cast<uint32_t>(sample_rate_mhz * 1'000'000);
    constexpr double GAIN_DB = 30.0;

    bool use_soapy = (source_arg == "soapy");

    if (use_soapy) {
        std::cout << "Source: SoapySDR (auto-detected)" << std::endl;
        std::cout << "  Frequency: " << ADSB_FREQ_HZ / 1e6 << " MHz" << std::endl;
        std::cout << "  Sample Rate: " << sample_rate_mhz << " MHz" << std::endl;
        std::cout << "  Gain: " << GAIN_DB << " dB" << std::endl;
    } else {
        std::cout << "Source: File playback" << std::endl;
        std::cout << "  File: " << source_arg << std::endl;
        std::cout << "  Sample Rate: " << sample_rate_mhz << " MHz" << std::endl;
    }
    std::cout << std::endl;

    cler::GuiManager gui(1400, 800, "ADSB Aircraft Tracker");

    std::string device_args_or_filename = "";
    if (!use_soapy) {
        device_args_or_filename = source_arg;
    }

    // SoapySDR reports device failures via exceptions by design
    std::optional<SelectableSourceBlock> source;
    try {
        source.emplace(
            "Source",
            use_soapy,
            use_soapy ? "" : source_arg,  // Empty string for auto-detect, or filename
            ADSB_FREQ_HZ,
            SAMPLE_RATE_HZ,
            GAIN_DB
        );
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        if (use_soapy) {
            std::cerr << "Make sure:" << std::endl;
            std::cerr << "  1. SoapySDR device is connected" << std::endl;
            std::cerr << "  2. SoapySDR drivers are installed for your device" << std::endl;
            std::cerr << "  3. You have permissions to access USB devices" << std::endl;
        }
        return 1;
    }

    IQToMagnitudeBlock iq2mag("IQ to Magnitude", SAMPLE_RATE_HZ);

    ADSBDecoderBlock::SampleRateMode decoder_mode = (sample_rate_mhz == 2.4f)
        ? ADSBDecoderBlock::SampleRateMode::RATE_2_4MHZ
        : ADSBDecoderBlock::SampleRateMode::RATE_2MHZ;
    ADSBDecoderBlock decoder("ADSB Decoder", decoder_mode);

    SinkNullBlock<uint16_t> null_sink("Null Sink");

    ADSBAggregateBlock aggregator(
        "ADSB Map",
        initial_lat, initial_lon,
        on_aircraft_update,
        nullptr
        // Coastline path defaults to "adsb_coastlines/ne_110m_coastline.shp"
    );

    aggregator.set_initial_window(0.0f, 0.0f, 1400.0f, 800.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&*source, &iq2mag.in),
        cler::BlockRunner(&iq2mag, &decoder.in),
        cler::BlockRunner(&decoder, &aggregator.in),
        cler::BlockRunner(&aggregator)
    );

    std::cout << "Starting receiver..." << std::endl;
    flowgraph.run();

    std::cout << "Tracking aircraft. Close window to exit." << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  - Mouse wheel: zoom in/out" << std::endl;
    std::cout << "  - Right-click drag: pan map" << std::endl;
    std::cout << std::endl;

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }

    std::cout << "Shutting down..." << std::endl;
    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);

    std::cout << "Total aircraft tracked: " << aggregator.aircraft_count() << std::endl;
    std::cerr << "[DONE] Receiver completed successfully" << std::endl;

    return 0;
}
