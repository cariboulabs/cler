#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

// Desktop blocks
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/fm/fm_demod.hpp"
#include "desktop_blocks/resamplers/multistage_resampler.hpp"
#include "desktop_blocks/sinks/sink_audio.hpp"
#include "desktop_blocks/utils/throttle.hpp"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

// Global flag for signal handling
static std::atomic<bool> g_should_exit{false};

void signal_handler(int signum) {
    std::cout << "\n\nInterrupt signal (" << signum << ") received. Shutting down...\n";
    g_should_exit = true;
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS]\n"
              << "  FM receiver with SoapySDR source and audio output (75 kHz deviation, broadcast standard)\n"
              << "\nOptions:\n"
              << "  --freq <MHz>     Center frequency in MHz (default: 88.5)\n"
              << "  --rate <MSPS>    Sample rate in MSPS (minimum: 0.4, recommended: 2.0-4.0)\n"
              << "  --gain <dB>      RX gain in dB (default: 20.0)\n"
              << "  --device <args>  SoapySDR device arguments (default: auto-detect)\n"
              << "  --help           Print this message\n"
              << "\nPost-Processing (add blocks as needed):\n"
              << "  1. Resampler: down to 48 kHz audio rate (if sample rate > 48 kHz)\n"
              << "  2. LPF: low-pass filter around 15 kHz to smooth audio\n"
              << "  3. De-emphasis: frequency correction (75µs or 50µs, broadcast standard)\n"
              << "\nSample Rate Guidance:\n"
              << "  Rule: sample_rate >= 10 × frequency_deviation (150 kHz minimum)\n"
              << "  Practical: 1-4 MSPS (1 MSPS=safe, 2 MSPS=recommended, 4 MSPS=best quality)\n"
              << "\nExamples:\n"
              << "  Listen to 88.5 FM Israel with RTL-SDR:\n"
              << "    " << prog_name << " --device \"driver=rtlsdr\" --freq 88.5 --rate 2.0\n"
              << "\n  Listen to 100 MHz with HackRF (high quality):\n"
              << "    " << prog_name << " --device \"driver=hackrf\" --freq 100.0 --rate 4.0\n";
}

int main(int argc, char* argv[]) {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line arguments
    double freq_mhz = 88.5;
    double rate_msps = 2.0;
    double gain_db = 20.0;
    std::string device_args = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--freq" && i + 1 < argc) {
            freq_mhz = std::stod(argv[++i]);
        } else if (arg == "--rate" && i + 1 < argc) {
            rate_msps = std::stod(argv[++i]);
        } else if (arg == "--gain" && i + 1 < argc) {
            gain_db = std::stod(argv[++i]);
        } else if (arg == "--device" && i + 1 < argc) {
            device_args = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Convert to Hz
    double freq_hz = freq_mhz * 1e6;
    double rate_hz = rate_msps * 1e6;

    std::cout << "FM Receiver Configuration:\n"
              << "  Frequency: " << freq_mhz << " MHz\n"
              << "  Sample Rate: " << rate_msps << " MSPS\n"
              << "  Gain: " << gain_db << " dB\n"
              << "  FM Deviation: 75 kHz (broadcast standard)\n"
              << "  Device: " << (device_args.empty() ? "auto-detect" : device_args) << "\n"
              << "\n";

    std::cout << "Creating blocks...\n";

    SourceSoapySDRBlock<std::complex<float>> source(
        "SoapySDR RX",
        device_args,
        freq_hz,
        rate_hz,
        gain_db,
        0  // channel 0
    );

    FMDemodBlock fm_demod(
        "FM Demod",
        rate_hz
        // Uses default 75 kHz deviation (broadcast standard)
    );

    // Resampler: downsample from SDR rate to 48 kHz audio rate
    float resample_ratio = 48000.0f / rate_hz;
    MultiStageResamplerBlock<float> resampler(
        "Resampler",
        resample_ratio,
        60.0f  // 60 dB attenuation for filter stopband
    );

    // Throttle: rate-limit decoded audio to match playback speed (48kHz)
    ThrottleBlock<float> throttle(
        "Throttle",
        48000  // 48 kHz audio rate
    );

    SinkAudioBlock audio_out(
        "Audio Out",
        48000.0  // 48 kHz audio output
    );

    std::cout << "Creating flowgraph...\n";

    // Create flowgraph: SDR → FM Demod → Resampler → Throttle → Audio
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fm_demod.in),
        cler::BlockRunner(&fm_demod, &resampler.in),
        cler::BlockRunner(&resampler, &throttle.in),
        cler::BlockRunner(&throttle, &audio_out.in)
    );

    std::cout << "Flowgraph created. Starting execution...\n"
              << "Press Ctrl+C to stop.\n\n";

    // Configure and run flowgraph
    cler::FlowGraphConfig config;
    config.scheduler = cler::SchedulerType::ThreadPerBlock;
    flowgraph.run(config);

    std::cout << "Flowgraph running. Tuned to " << freq_mhz << " MHz.\n"
              << "Chain: SDR (" << rate_msps << " MSPS) → FM Demod → Resampler (48 kHz) → Audio\n";

    // Keep main thread alive and check signal flag
    while (!g_should_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    flowgraph.stop();
    std::cout << "Flowgraph stopped. Cleanup complete.\n";

    return 0;
}
