#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

// Desktop blocks
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/filters/kaiser_decim_lpf.hpp"
#include "desktop_blocks/fm/fm_demod.hpp"
#include "desktop_blocks/resamplers/multistage_resampler.hpp"
#include "desktop_blocks/sinks/sink_audio.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/math/gain.hpp"

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
              << "  --gain <dB>      RX gain in dB (default: 20.0)\n"
              << "  --device <args>  SoapySDR device arguments (default: auto-detect)\n"
              << "  --help           Print this message\n"
              << "\nExamples:\n"
              << "  Listen to 88.5 FM Israel with RTL-SDR:\n"
              << "    " << prog_name << " --device \"driver=rtlsdr\" --freq 88.5 \n";
}

int main(int argc, char* argv[]) {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line arguments
    double freq_mhz = 88.5;
    double gain_db = 0.0;
    std::string device_args = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--freq" && i + 1 < argc) {
            freq_mhz = std::stod(argv[++i]);
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
    const float wanted_freq_hz = static_cast<float>(freq_mhz * 1e6);
    const float sdr_sps = 1e6f;

    std::cout << "FM Receiver Configuration:\n"
              << "  Frequency: " << freq_mhz << " MHz\n"
              << "  Gain: " << gain_db << " dB\n"
              << "  FM Deviation: 75 kHz (broadcast standard)\n"
              << "  Sample Rate: " << sdr_sps / 1e6 << " MSPS\n"
              << "  Device: " << (device_args.empty() ? "auto-detect" : device_args) << "\n"
              << "\n";


    SourceSoapySDRBlock<std::complex<float>> source(
        "SoapySDR RX",
        device_args,
        wanted_freq_hz - 500e3,
        sdr_sps,
        gain_db,
        0  // channel 0
    );

    // Set HackRF individual gain stages (critical for weak FM signals!)
    // These values match working GNURadio example: IF=40, VGA=62
    source.set_gain_element("LNA", 40);   // Low Noise Amplifier (0-40 dB)
    source.set_gain_element("VGA", 62);   // Variable Gain Amplifier (0-62 dB)
    source.set_gain_element("AMP", 0);    // RF amplifier enable/disable (0 or 14 dB)

    FrequencyShiftBlock freq_shift(
        "Frequency Shift",
        500e3,
        sdr_sps
    );

    KaiserDecimLPFBlock<std::complex<float>> lpf(
        "Channel Filter",
        sdr_sps,   // Sample rate
        100e3,       // Cutoff
        25e3,       // Transition
        5,          // Decimation factor (1 MSPS -> 200 kSPS)
        60.0        // Attenuation
    );


    MultiStageResamplerBlock<std::complex<float>> resampler1(
        "Resampler1",
        12.0 / 5.0,
        60.0
    );


    FMDemodBlock fm_demod(
        "FM Demod",
        480e3,
        75e3 /*freq_deviation*/
    );

    MultiStageResamplerBlock<float> audio_decim(
        "Audio Decim",
        1.0 / 10.0,
        60.0
    );

    GainBlock<float> gain(
        "Audio Gain",
        0.3  // unity gain
    );

    SinkAudioBlock audio_out(
        "Audio Out",
        48e3f,
        paNoDevice,
        cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) * 30
    );

    std::cout << "Creating flowgraph...\n";

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &freq_shift.in),
        cler::BlockRunner(&freq_shift, &lpf.in),
        cler::BlockRunner(&lpf, &resampler1.in),
        cler::BlockRunner(&resampler1, &fm_demod.in),
        cler::BlockRunner(&fm_demod, &audio_decim.in),
        cler::BlockRunner(&audio_decim, &gain.in),
        cler::BlockRunner(&gain, &audio_out.in),
        cler::BlockRunner(&audio_out)
    );

    std::cout << "Flowgraph created. Starting execution...\n"
              << "Press Ctrl+C to stop.\n\n";

    cler::FlowGraphConfig config;
    // config.scheduler = cler::SchedulerType::FixedThreadPool;
    // flowgraph.run(config);

    while (!g_should_exit) {
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));

        source.procedure(&freq_shift.in);
        freq_shift.procedure(&lpf.in);
        lpf.procedure(&resampler1.in);
        resampler1.procedure(&fm_demod.in);
        fm_demod.procedure(&audio_decim.in);
        audio_decim.procedure(&gain.in);
        gain.procedure(&audio_out.in);
        audio_out.procedure();
    }

    flowgraph.stop();
    std::cout << "Flowgraph stopped. Cleanup complete.\n";
    return 0;
}
