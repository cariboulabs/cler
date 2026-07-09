#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_pluto.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

#include <cstdlib>
#include <string>
#include <iostream>

int main(int argc, char** argv) {
    const char* uri = "ip:192.168.2.1";
    long long freq_hz = 100e6;
    long long samp_rate = 2.5e6;
    double gain_db = -1.0;             // negative = AGC
    size_t FFT_SIZE = 1024;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--uri" && i + 1 < argc) {
            uri = argv[++i];
        } else if (arg == "--freq" && i + 1 < argc) {
            freq_hz = static_cast<long long>(std::stod(argv[++i]));
        } else if (arg == "--rate" && i + 1 < argc) {
            samp_rate = static_cast<long long>(std::stod(argv[++i]));
        } else if (arg == "--gain" && i + 1 < argc) {
            gain_db = std::stod(argv[++i]);
        } else if (arg == "--fft" && i + 1 < argc) {
            FFT_SIZE = std::stoul(argv[++i]);
        } else if (arg == "--help" || arg == "--h") {
            std::cout << "Usage: " << argv[0]
                      << " [--uri <iio-uri>] [--freq <Hz>] [--rate <SPS>]"
                      << " [--gain <dB, negative=AGC>] [--fft <size>]\n"
                      << "URIs: ip:192.168.2.1 | usb: | local: (on the Pluto itself)\n";
            return 0;
        }
    }

    std::cout << "PlutoSDR Receiver Example:\n"
              << "URI: " << uri << "\n"
              << "Frequency: " << freq_hz << " Hz\n"
              << "Sample Rate: " << samp_rate << " S/s\n"
              << "FFT Size: " << FFT_SIZE << "\n";

    SourcePlutoBlock source_pluto(
        "SourcePluto",
        uri,
        freq_hz,
        samp_rate,
        gain_db
    );

    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    PlotCSpectrumBlock timeplot(
        "Spectrum Plot",
        {"pluto_signal"},
        samp_rate,
        FFT_SIZE
    );

    PlotCSpectrogramBlock spectrogram(
        "Spectrogram",
        {"pluto_signal"},
        samp_rate,
        FFT_SIZE,
        1000
    );

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_pluto, &fanout.in),
        cler::BlockRunner(&fanout, &timeplot.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&spectrogram),
        cler::BlockRunner(&timeplot)
    );

    cler::GuiManager gui(800, 400, "PlutoSDR Receiver Example");
    timeplot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f);

    flowgraph.run();

    while (!gui.should_close()) {
        gui.begin_frame();
        timeplot.render();
        spectrogram.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    flowgraph.stop();
    print_flowgraph_execution_report(flowgraph);

    return 0;
}
