#include "cler.hpp"
#include "cler_addons.hpp"
#include "blocks/sources/source_hackrf.hpp"
#include "blocks/plots/plot_cspectrum.hpp"
#include "blocks/plots/plot_cspectrogram.hpp"
#include "blocks/utils/fanout.hpp"
#include "blocks/gui/gui_manager.hpp"

int main() {
    if (hackrf_init() != HACKRF_SUCCESS) {
        throw std::runtime_error("Failed to initialize HackRF library");
    }

    const uint32_t samp_rate = 4'000'000; // 4 MHz
    const uint64_t freq_hz = 915e6;       // 915 MHz
    const size_t FFT_SIZE = 1024; // FFT size

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

    cler::FlowGraph flowgraph(
        cler::BlockRunner(&source_hackrf, &fanout.in),
        cler::BlockRunner(&fanout, &timeplot.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&spectrogram),
        cler::BlockRunner(&timeplot)
    );

    cler::GuiManager gui(800, 400, "Hackrf Receiver Example");
    timeplot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f);

    flowgraph.run(cler::FlowGraphConfig{
        .adaptive_sleep = true
    });


    while (gui.should_close() == false) {
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
