#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_hackrf.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

#include <cstdlib>
#include <string>
#include <iostream>

int main(int argc, char** argv) {
    if (hackrf_init() != HACKRF_SUCCESS) {
        throw std::runtime_error("Failed to initialize HackRF library");
    }

    // Default parameters
    uint64_t freq_hz = 915e6;          // 915 MHz
    uint32_t samp_rate = 4e6;    // 4 MHz
    size_t FFT_SIZE = 1024;            // FFT size

    // --- Parse command-line flags ---
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--freq" && i + 1 < argc) {
            freq_hz = static_cast<uint64_t>(std::stod(argv[++i]));
        } else if (arg == "--rate" && i + 1 < argc) {
            samp_rate = static_cast<uint32_t>(std::stod(argv[++i]));
        } else if (arg == "--fft" && i + 1 < argc) {
            FFT_SIZE = std::stoul(argv[++i]);
        } else if (arg == "--help" || arg == "--h") {
            std::cout << "Usage: " << argv[0] << " [--freq <Hz>] [--rate <SPS>] [--fft <size>]\n";
            return 0;
        }
    }

    std::cout << "HackRF Receiver Example:\n"
              << "Frequency: " << freq_hz << " Hz\n"
              << "Sample Rate: " << samp_rate << " S/s\n"
              << "FFT Size: " << FFT_SIZE << "\n";

    SourceHackRFBlock source_hackrf(
        "SourceHackRF",
        freq_hz,
        samp_rate
    );

    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    PlotCSpectrumBlock timeplot(
        "Spectrum Plot",
        {"hackrf_signal"},
        samp_rate,
        FFT_SIZE
    );

    PlotCSpectrogramBlock spectrogram(
        "Spectrogram",
        {"hackrf_signal"},
        samp_rate,
        FFT_SIZE,
        1000
    );

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_hackrf, &fanout.in),
        cler::BlockRunner(&fanout, &timeplot.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&spectrogram),
        cler::BlockRunner(&timeplot)
    );

    cler::GuiManager gui(800, 400, "HackRF Receiver Example");
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

    hackrf_exit();
    return 0;
}
