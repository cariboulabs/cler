// Unified USRP Example - demonstrates all UHD block features including TX
// Select mode via command line argument

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_uhd_zohar_full.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/sources/source_chirp.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/sinks/sink_uhd.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <map>

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

void mode_rx(int argc, char** argv) {
    double freq = argc > 2 ? std::stod(argv[2]) : 915e6;
    double rate = argc > 3 ? std::stod(argv[3]) : 2e6;
    double gain = argc > 4 ? std::stod(argv[4]) : 30.0;
    std::string device_address = argc > 5 ? argv[5] : "";

    std::cout << "RX Mode - Spectrum Plot" << std::endl;
    std::cout << "Device: " << (device_address.empty() ? "default" : device_address) << std::endl;
    std::cout << "Freq: " << freq/1e6 << " MHz, Rate: " << rate/1e6 << " MSPS, Gain: " << gain << " dB" << std::endl;

    cler::GuiManager gui(1200, 600, "USRP RX - Spectrum");
    SourceUHDBlock<std::complex<float>> usrp("USRP", freq, rate, device_address, gain, 1);
    PlotCSpectrumBlock spectrum("USRP Spectrum", {"I/Q"}, rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp, &spectrum.in[0]),
        cler::BlockRunner(&spectrum)
    );

    flowgraph.run();
    std::cout << "Flowgraph running... Close window to exit." << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Overflows: " << usrp.get_overflow_count() << std::endl;
}

void mode_tx_chirp(int argc, char** argv) {
    double freq = argc > 2 ? std::stod(argv[2]) : 915e6;
    double rate = argc > 3 ? std::stod(argv[3]) : 2e6;
    double gain = argc > 4 ? std::stod(argv[4]) : 89.75;
    float amplitude = argc > 5 ? std::stof(argv[5]) : 0.5f;
    float chirp_duration = argc > 6 ? std::stof(argv[6]) : 1.0f;
    std::string device_address = argc > 7 ? argv[7] : "";
    std::cout << "TX Chirp Mode" << std::endl;
    std::cout << "Device: " << (device_address.empty() ? "default" : device_address) << std::endl;
    std::cout << "Freq: " << freq/1e6 << " MHz, Rate: " << rate/1e6 << " MSPS, Gain: " << gain << " dB" << std::endl;
    std::cout << "Amplitude: " << amplitude << std::endl;
    std::cout << "Chirp: -500 kHz to +500 kHz over " << chirp_duration << " seconds" << std::endl;

    cler::GuiManager gui(1200, 600, "USRP TX - Chirp Signal");

    // Chirp source: -500 kHz to +500 kHz over 1 second
    SourceChirpBlock<std::complex<float>> chirp("Chirp", 
        amplitude,       // Amplitude
        -500e3f,         // Start frequency
        500e3f,          // End frequency
        rate,            // Sample rate
        chirp_duration); // Duration

    // Fanout to spectrum plot and USRP
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    // Spectrum plot
    PlotCSpectrumBlock spectrum("TX Spectrum", {"Chirp"}, rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    // USRP TX sink
    SinkUHDBlock<std::complex<float>> usrp("USRP_TX", freq, rate, device_address, gain, 1);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&chirp, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp)
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
    std::cout << "Underflows: " << usrp.get_underflow_count() << std::endl;
}

void mode_tx_cw(int argc, char** argv) {
    double freq = argc > 2 ? std::stod(argv[2]) : 915e6;
    double rate = argc > 3 ? std::stod(argv[3]) : 2e6;
    double gain = argc > 4 ? std::stod(argv[4]) : 89.75;
    double cw_offset = argc > 5 ? std::stod(argv[5]) : 1;
    float amplitude = argc > 6 ? std::stof(argv[6]) : 0.5f;
    std::string device_address = argc > 7 ? argv[7] : "";


    std::cout << "TX CW Mode - Continuous Wave" << std::endl;
    std::cout << "Device: " << (device_address.empty() ? "default" : device_address) << std::endl;
    std::cout << "Center Freq: " << freq/1e6 << " MHz" << std::endl;
    std::cout << "CW Offset: " << cw_offset/1e3 << " kHz" << std::endl;
    std::cout << "Actual TX: " << (freq + cw_offset)/1e6 << " MHz" << std::endl;
    std::cout << "Rate: " << rate/1e6 << " MSPS, Gain: " << gain << " dB" << std::endl;
    std::cout << "Amplitude: " << amplitude << std::endl;

    cler::GuiManager gui(1200, 600, "USRP TX - Continuous Wave");

    // CW source
    SourceCWBlock<std::complex<float>> cw("CW", 
        amplitude,      // Amplitude
        cw_offset,      // Frequency offset
        rate);          // Sample rate

    // Fanout to spectrum plot and USRP
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    // Spectrum plot
    PlotCSpectrumBlock spectrum("TX Spectrum", {"CW Tone"}, rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    // USRP TX sink
    SinkUHDBlock<std::complex<float>> usrp("USRP_TX", freq, rate, device_address, gain, 1);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&cw, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp)
    );

    flowgraph.run();
    std::cout << "Transmitting cw signal. Close window to stop." << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Underflows: " << usrp.get_underflow_count() << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];

    try {
        if (mode == "rx") {
            mode_rx(argc, argv);
        } else if (mode == "tx-chirp") {
            mode_tx_chirp(argc, argv);
        } else if (mode == "tx-cw") {
            mode_tx_cw(argc, argv);
        } else {
            std::cerr << "Unknown mode: " << mode << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}