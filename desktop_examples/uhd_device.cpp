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

void print_usage(const char* prog) {
    std::cout << "\nUSRP Example - Unified demonstration of UHD block features\n" << std::endl;
    std::cout << "Usage: " << prog << " [OPTIONS]" << std::endl;
    std::cout << "\nAvailable modes:" << std::endl;
    std::cout << "  rx          - Simple RX with spectrum plot" << std::endl;
    std::cout << "  tx-chirp    - Transmit chirp signal with spectrum plot" << std::endl;
    std::cout << "  tx-cw       - Transmit continuous wave with spectrum plot" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -m, --mode MODE          Operating mode: rx, tx-chirp, or tx-cw (required)" << std::endl;
    std::cout << "  -f, --freq FREQ          Center frequency in Hz (default: 915e6)" << std::endl;
    std::cout << "  -r, --rate RATE          Sample rate in samples/sec (default: 2e6)" << std::endl;
    std::cout << "  -g, --gain GAIN          Gain in dB (default: depends on mode)" << std::endl;
    std::cout << "  -a, --amp AMP            Amplitude 0.0-1.0 (default: depends on mode)" << std::endl;
    std::cout << "  -o, --cw_offset OFFSET   CW tone offset from center in Hz (default: 0)" << std::endl;
    std::cout << "  -F, --fft SIZE           FFT size for spectrum analysis (default: 2048)" << std::endl;
    std::cout << "  -c, --chirp_duration DUR Chirp duration in seconds (default: 0.001)" << std::endl;
    std::cout << "  -d, --dev ADDRESS        USRP device address (default: auto)" << std::endl;
    std::cout << "  -h, --help               Show this help message" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << prog << " -m rx -f 915e6 -r 2e6 -g 30" << std::endl;
    std::cout << "  " << prog << " --mode tx-chirp --freq 915e6 --rate 2e6 --gain 89 --amp 0.3" << std::endl;
    std::cout << "  " << prog << " -m tx-cw -f 915e6 -r 2e6 -g 89 -o 100e3 -a 0.5" << std::endl;
    std::cout << "  " << prog << " -m rx -d \"addr=192.168.10.2\" -f 2.4e9" << std::endl;
    std::cout << std::endl;
}

USRPArgs parse_args(int argc, char** argv) {
    USRPArgs args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto next_arg = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                exit(1);
            }
            return argv[++i];
        };

        try {
            if (arg == "-h" || arg == "--help") {
                print_usage(argv[0]);
                exit(0);
            }
            else if (arg == "-m" || arg == "--mode") {
                args.mode = next_arg();
            }
            else if (arg == "-f" || arg == "--freq") {
                args.freq = std::stod(next_arg());  // supports 918e6 or 918000000
            }
            else if (arg == "-r" || arg == "--rate") {
                args.rate = std::stod(next_arg());  // supports 2e6 or 2000000
            }
            else if (arg == "-g" || arg == "--gain") {
                args.gain = std::stod(next_arg());
            }
            else if (arg == "-a" || arg == "--amp") {
                args.amp = std::stod(next_arg());
            }
            else if (arg == "-o" || arg == "--cw_offset") {
                args.cw_offset = std::stod(next_arg());
            }
            else if (arg == "-F" || arg == "--fft") {
                args.fft = std::stoul(next_arg());
            }
            else if (arg == "-c" || arg == "--chirp_duration") {
                args.chirp_duration_s = std::stod(next_arg());
            }
            else if (arg == "-d" || arg == "--dev" || arg == "--device") {
                args.device_address = next_arg();
            }
            else {
                std::cerr << "Error: Unknown option '" << arg << "'" << std::endl;
                std::cerr << "Use -h or --help for usage information" << std::endl;
                exit(1);
            }
        } catch (const std::invalid_argument& e) {
            std::cerr << "Error: Invalid numeric value for " << arg << ": " << e.what() << std::endl;
            exit(1);
        } catch (const std::out_of_range& e) {
            std::cerr << "Error: Value out of range for " << arg << ": " << e.what() << std::endl;
            exit(1);
        }
    }
    
    return args;
}

void mode_rx(const USRPArgs& args) {
    
    try {
        //try to initialize USRP RX
        SourceUHDBlock<std::complex<float>> usrp_source("USRP", args.freq,
            args.rate, args.device_address, args.gain, 1 /*num channels*/);
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize USRP" << e.what() << std::endl;
        return;
    }
    //Initalization was successful, lets re-init for real
    SourceUHDBlock<std::complex<float>> usrp_source("USRP", args.freq,
        args.rate, args.device_address, args.gain, 1 /*num channels*/);

    cler::GuiManager gui(1000, 800, "USRP Receiver Example");
    PlotCSpectrumBlock spectrum("USRP Spectrum", {"I/Q"}, args.rate, args.fft);
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"usrp_signal"}, args.rate, args.fft, 1000);
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp_source, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram)
    );

    flowgraph.run();
    std::cout << "Flowgraph running... Close window to exit." << std::endl;

    spectrum.set_initial_window(1000.0f, 0.0f, 400.0f, 400.0f);
    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        spectrogram.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Overflows: " << usrp_source.get_overflow_count() << std::endl;
}

void mode_tx_chirp(const USRPArgs& args) {
    UHDConfig config{args.freq, args.rate, args.gain};
    
    try {
        // Try to initialize USRP TX
        SinkUHDBlock<std::complex<float>> usrp_sink("USRP_TX", 
            args.device_address, 1 /*num channels*/, 0 /*mboard*/, "sc16", &config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize USRP: " << e.what() << std::endl;
        return;
    }
    // Initialization was successful, lets re-init for real
    SinkUHDBlock<std::complex<float>> usrp_sink("USRP_TX", 
        args.device_address, 1 /*num channels*/, 0 /*mboard*/, "sc16", &config);

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

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&chirp, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp_sink.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp_sink)
    );

    flowgraph.run();
    std::cout << "Transmitting chirp signal. Close window to stop." << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Underflows: " << usrp_sink.get_underflow_count() << std::endl;
}

void mode_tx_cw(const USRPArgs& args) {
    UHDConfig config{args.freq, args.rate, args.gain};
    
    try {
        // Try to initialize USRP TX
        SinkUHDBlock<std::complex<float>> usrp_sink("USRP_TX", 
            args.device_address, 1 /*num channels*/, 0 /*mboard*/, "sc16", &config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize USRP: " << e.what() << std::endl;
        return;
    }
    // Initialization was successful, lets re-init for real
    SinkUHDBlock<std::complex<float>> usrp_sink("USRP_TX", 
        args.device_address, 1 /*num channels*/, 0 /*mboard*/, "sc16", &config);

    cler::GuiManager gui(1200, 600, "USRP TX - Continuous Wave");
    SourceCWBlock<std::complex<float>> cw("CW", args.amp, args.cw_offset, args.rate);
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    PlotCSpectrumBlock spectrum("TX Spectrum", {"CW Tone"}, args.rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&cw, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp_sink.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp_sink)
    );

    flowgraph.run();
    std::cout << "Transmitting CW tone. Close window to stop." << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Underflows: " << usrp_sink.get_underflow_count() << std::endl;
}

int main(int argc, char** argv) {
    USRPArgs args = parse_args(argc, argv);
    std::cout<<"inside main"<<std::endl;

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
