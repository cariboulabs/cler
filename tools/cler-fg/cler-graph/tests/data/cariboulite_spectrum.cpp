#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_cariboulite.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

int main() {
    const size_t sps = 2'000'000;
    const float freq_hz = 902e6;

    SourceCaribouliteBlock<std::complex<float>> source_cariboulite(
        "SourceCaribouLite",
        CaribouLiteRadio::RadioType::S1G,
        static_cast<float>(sps),
        freq_hz,
        true
    );

    PlotCSpectrumBlock plot(
        "Spectrum Plot",
        {"caribou_signal"},
        sps,
        256);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_cariboulite, &plot.in[0]),
        cler::BlockRunner(&plot)
    );

    cler::GuiManager gui(800, 400, "Hackrf Receiver Example");
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f);

    flowgraph.run(cler::FlowGraphConfig{});
    while (!gui.should_close()) {
        gui.render(flowgraph);
    }
    flowgraph.stop();
    print_flowgraph_execution_report(flowgraph);

    return 0;
}