#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

// Desktop blocks
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/filters/kaiser_lpf.hpp"
#include "desktop_blocks/fm/fm_demod.hpp"
#include "desktop_blocks/resamplers/multistage_resampler.hpp"
#include "desktop_blocks/sinks/sink_audio.hpp"

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
              << "  --rate <MSPS>    Sample rate in MSPS (minimum: 0.4, recommended: 2.0-4.0, default: 2.0)\n"
              << "  --gain <dB>      RX gain in dB (default: 20.0)\n"
              << "  --device <args>  SoapySDR device arguments (default: auto-detect)\n"
              << "  --help           Print this message\n"
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

    // Channel selection filter: isolate the desired FM station from adjacent channels
    // FM broadcast spacing: 200 kHz, deviation: ±75 kHz
    // Filter cutoff: 100 kHz (captures ±75 kHz deviation + audio bandwidth)
    KaiserLPFBlock<std::complex<float>> channel_filter(
        "Channel Filter",
        rate_hz,        // Sample rate (e.g., 2 MSPS)
        100e3,          // Cutoff: 100 kHz (selects single FM station)
        20e3,           // Transition: 20 kHz (sharp rolloff to reject adjacent channels)
        60.0            // Attenuation: 60 dB (excellent adjacent channel rejection)
    );

    FMDemodBlock fm_demod(
        "FM Demod",
        rate_hz,
        75e3 /*freq_deviation*/
    );

    static constexpr float kAudioSampleRate = 48000.0f;

    // Resampler: downsample from SDR rate to 48 kHz audio rate
    // (includes built-in anti-aliasing filter with 60 dB stopband attenuation)
    float resample_ratio = kAudioSampleRate / rate_hz;
    MultiStageResamplerBlock<float> resampler(
        "Resampler",
        resample_ratio,
        60.0f  // 60 dB attenuation for filter stopband
    );

    SinkAudioBlock audio_out(
        "Audio Out",
        kAudioSampleRate
    );

    std::cout << "Creating flowgraph...\n";

    // Signal chain: SDR → Channel Filter → FM Demod → Resampler → Audio Out
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &channel_filter.in),
        cler::BlockRunner(&channel_filter, &fm_demod.in),
        cler::BlockRunner(&fm_demod, &resampler.in),
        cler::BlockRunner(&resampler, &audio_out.in),
        cler::BlockRunner(&audio_out)
    );

    std::cout << "Flowgraph created. Starting execution...\n"
              << "Press Ctrl+C to stop.\n\n";

    cler::FlowGraphConfig config;
    config.collect_detailed_stats = true;
    flowgraph.run(config);

    while (!g_should_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    flowgraph.stop();
    std::cout << "Flowgraph stopped. Cleanup complete.\n";

    // Print stats:
    for (const auto& s : flowgraph.stats()) {
        printf("%s: %zu successful, %zu failed, %.1f%% CPU\n",
                s.name.c_str(), s.successful_procedures, s.failed_procedures,
                s.get_cpu_utilization_percent());
    }
    return 0;
}
