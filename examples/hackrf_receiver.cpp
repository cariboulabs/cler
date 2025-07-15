#include "cler.hpp"
#include "cler_addons.hpp"
#include "blocks/sources/source_hackrf.hpp"
#include "blocks/plots/plot_cspectrum.hpp"
#include "gui_manager.hpp"

int main() {
    const uint32_t samp_rate = 4'000'000; // 4 MHz
    const uint64_t freq_hz = 915e6;       // 915 MHz

    SourceHackRFBlock source_hackrf(
        "SourceHackRF",
        freq_hz,
        samp_rate
    );

    PlotCSpectrumBlock plot(
        "Spectrum Plot",
        {"hackrf_signal"},
        samp_rate,
        256 // FFT size
    );

    cler::FlowGraph flowgraph(
        cler::BlockRunner(&source_hackrf, &plot.in[0]),
        cler::BlockRunner(&plot)
    );

    cler::GuiManager gui(800, 400, "Hackrf Receiver Example");
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f);

    flowgraph.run(cler::FlowGraphConfig{
        .adaptive_sleep = true,
    });


    while (gui.should_close() == false) {
        gui.begin_frame();
        plot.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    flowgraph.stop();
    print_flowgraph_execution_report(flowgraph);

    return 0;
}
