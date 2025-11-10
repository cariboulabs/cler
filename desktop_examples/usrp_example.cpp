// Unified USRP Example - demonstrates all UHD block features including TX
// Select mode via command line argument

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_uhd.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/sources/source_chirp.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/sinks/sink_uhd.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <map>

struct USRPArgs {
    std::string mode;
    double freq = 915e6;
    double rate = 2e6;
    double gain = 89.75;
    double cw_offset = 10e3; // for tx-cw
    double amp = 1;         // amplitude
    size_t fft = 1024;
    std::string device_address;
    double chirp_duration_s = 1; // for tx-chirp
};

USRPArgs parse_args(int argc, char** argv) {
    USRPArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto next_arg = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value after " << arg << "\n";
                exit(1);
            }
            return argv[++i];
        };

        try {
            if (arg == "--help" || arg == "--h") {
                std::cout << "Usage: " << argv[0]
                          << " --mode <rx|tx-chirp|tx-cw> [--freq <Hz>] [--rate <SPS>] "
                             "[--gain <dB>] [--amp <0-1>] [--cw_offset <Hz>] [--fft <size>] [--chirp_duration <s>] [--dev <addr>]\n";
                exit(0);
            } else if (arg == "--mode") {
                args.mode = next_arg();
            } else if (arg == "--freq") {
                args.freq = std::stod(next_arg());  // supports 918e6 or 918000000
            } else if (arg == "--rate") {
                args.rate = std::stod(next_arg());  // supports 2e6 or 2000000
            } else if (arg == "--gain") {
                args.gain = std::stod(next_arg());
            } else if (arg == "--amp") {
                args.amp = std::stod(next_arg());
            } else if (arg == "--cw_offset") {
                args.cw_offset = std::stod(next_arg());
            } else if (arg == "--fft") {
                args.fft = std::stoul(next_arg());
            } else if (arg == "--chirp_duration") {
                args.chirp_duration_s = std::stod(next_arg());
            } else if (arg == "--dev" || arg == "--device") {
                args.device_address = next_arg();
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                exit(1);
            }
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid numeric value for " << arg << ": " << e.what() << "\n";
            exit(1);
        } catch (const std::out_of_range& e) {
            std::cerr << "Value out of range for " << arg << ": " << e.what() << "\n";
            exit(1);
        }
    }

    if (args.mode.empty()) {
        std::cerr << "Error: --mode must be specified\n";
        exit(1);
    }

    return args;
}

template<typename T>
std::unique_ptr<T> init_usrp_async(const char* label,
                                   const std::string& device_address,
                                   const USRPConfig* config,
                                   std::atomic<bool>& ready_flag,
                                   std::atomic<bool>& fail_flag)
{
    std::unique_ptr<T> ptr;
    std::thread([&]() {
        try {
            ptr = std::make_unique<T>(label, device_address, 1, 0, "sc16", config);
            ready_flag = true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to initialize USRP: " << e.what() << std::endl;
            fail_flag = true;
        }
    }).detach();
    return ptr;
}


template<typename SourceBlockType, typename SinkBlockType>
void run_usrp_tx(cler::GuiManager& gui,
                 SourceBlockType& source_block,
                 PlotCSpectrumBlock& spectrum,
                 std::unique_ptr<SinkBlockType>& usrp_sink_ptr,
                 FanoutBlock<std::complex<float>>& fanout)
{
    auto& usrp_sink = *usrp_sink_ptr;

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_block, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp_sink.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp_sink)
    );

    flowgraph.run();

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Underflows: " << usrp_sink.get_underflow_count() << std::endl;
}

void print_usage(const char* prog) {
    std::cout << "\nUSRP Example - Unified demonstration of UHD block features\n" << std::endl;
    std::cout << "Usage: " << prog << " <mode> [options...]" << std::endl;
    std::cout << "\nAvailable modes:" << std::endl;
    std::cout << "  rx          - Simple RX with spectrum plot" << std::endl;
    std::cout << "  tx-chirp    - Transmit chirp signal with spectrum plot" << std::endl;
    std::cout << "  tx-cw       - Transmit continuous wave with spectrum plot" << std::endl;
    std::cout << "\nMode-specific options:" << std::endl;
    std::cout << "  rx:         [freq_hz] [rate_hz] [gain_db]" << std::endl;
    std::cout << "  tx-chirp:   [freq_hz] [rate_hz] [gain_db] [amplitude]" << std::endl;
    std::cout << "  tx-cw:      [freq_hz] [rate_hz] [gain_db] [cw_offset_hz] [amplitude]" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << prog << " rx 915e6 2e6 30" << std::endl;
    std::cout << "  " << prog << " tx-chirp 915e6 2e6 89 0.3" << std::endl;
    std::cout << "  " << prog << " tx-cw 915e6 2e6 89 100e3 0.5" << std::endl;
    std::cout << std::endl;
}

void mode_rx(const USRPArgs& args) {
    const size_t FFT_SIZE = args.fft;
    std::cout << "RX Mode - Spectrum Plot" << std::endl;
    std::cout << "Device: " << (args.device_address.empty() ? "default" : args.device_address) << std::endl;
    std::cout << "Freq: " << args.freq/1e6 << " MHz, Rate: " << args.rate/1e6
              << " MSPS, Gain: " << args.gain << " dB" << std::endl;

    PlotCSpectrumBlock spectrum("USRP Spectrum", {"I/Q"}, args.rate, 2048);
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"usrp_signal"}, args.rate, FFT_SIZE, 1000);
    cler::GuiManager gui(1000, 800, "USRP Receiver Example");
    spectrum.set_initial_window(1000.0f, 0.0f, 400.0f, 400.0f);

    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    std::atomic<bool> usrp_ready{false};
    std::atomic<bool> usrp_failed{false};
    std::unique_ptr<SourceUHDBlock<std::complex<float>>> usrp_source_ptr;

    std::thread init_thread([&]() {
        try {
            usrp_source_ptr = std::make_unique<SourceUHDBlock<std::complex<float>>>(
                "USRP", args.freq, args.rate, args.device_address, args.gain, 1
            );
            usrp_ready = true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to initialize USRP: " << e.what() << std::endl;
            usrp_failed = true;
        }
    });
    init_thread.detach();

    while (!usrp_ready && !usrp_failed && !gui.should_close()) {
        gui.begin_frame();
        ImGui::Text("Loading FPGA image, this may take a while...\nOnly for first use after USRP reboot.");
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    if (usrp_failed) return;

    auto& usrp_source = *usrp_source_ptr;

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp_source, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram)
    );

    flowgraph.run();
    std::cout << "Flowgraph running... Close window to exit." << std::endl;


    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        spectrogram.render();
        gui.end_frame();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_hop);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Overflows: " << usrp_source.get_overflow_count() << std::endl;
}

void mode_tx_chirp(const USRPArgs& args) {
    USRPConfig config{args.freq, args.rate, args.gain};
    cler::GuiManager gui(1200, 600, "USRP TX - Chirp Signal");

    SourceChirpBlock<std::complex<float>> chirp("Chirp",
                                                 args.amp,
                                                 -500e3f,
                                                 500e3f,
                                                 args.rate,
                                                 args.chirp_duration_s);

    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    PlotCSpectrumBlock spectrum("TX Spectrum", {"Chirp"}, args.rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    std::atomic<bool> usrp_ready{false}, usrp_failed{false};
    auto usrp_sink_ptr = init_usrp_async<SinkUHDBlock<std::complex<float>>>(
        "USRP_TX",
        args.device_address,
        &config,
        usrp_ready,
        usrp_failed
    );

    while (!usrp_ready && !usrp_failed && !gui.should_close()) {
        gui.begin_frame();
        ImGui::Text("Loading FPGA image, this may take a while...\nOnly for first use after USRP reboot.");
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    if (usrp_failed) return;

    run_usrp_tx(gui, chirp, spectrum, usrp_sink_ptr, fanout);
}

void mode_tx_cw(const USRPArgs& args) {
    USRPConfig config{args.freq, args.rate, args.gain};
    cler::GuiManager gui(1200, 600, "USRP TX - Continuous Wave");

    SourceCWBlock<std::complex<float>> cw("CW", args.amp, args.cw_offset, args.rate);

    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    PlotCSpectrumBlock spectrum("TX Spectrum", {"CW Tone"}, args.rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    std::atomic<bool> usrp_ready{false}, usrp_failed{false};
    auto usrp_sink_ptr = init_usrp_async<SinkUHDBlock<std::complex<float>>>(
        "USRP_TX",
        args.device_address,
        &config,
        usrp_ready,
        usrp_failed
    );

    while (!usrp_ready && !usrp_failed && !gui.should_close()) {
        gui.begin_frame();
        ImGui::Text("Loading FPGA image, this may take a while...\nOnly for first use after USRP reboot.");
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    if (usrp_failed) return;

    run_usrp_tx(gui, cw, spectrum, usrp_sink_ptr, fanout);
}

int main(int argc, char** argv) {
    USRPArgs args = parse_args(argc, argv);

    std::cout << "Mode: " << args.mode << "\n"
              << "Freq: " << args.freq << " Hz\n"
              << "Rate: " << args.rate << " S/s\n"
              << "Gain: " << args.gain << " dB\n"
              << "Amplitude: " << args.amp << "\n"
              << "CW Offset: " << args.cw_offset << " Hz\n"
              << "FFT: " << args.fft << "\n"
              << "Device: " << (args.device_address.empty() ? "default" : args.device_address)
              << std::endl;

    if (args.mode == "rx") {
        mode_rx(args);
    } else if (args.mode == "tx-chirp") {
        mode_tx_chirp(args);
    } else if (args.mode == "tx-cw") {
        mode_tx_cw(args);
    } else {
        std::cerr << "Unknown mode: " << args.mode << "\n";
        return 1;
    }

    return 0;
}
