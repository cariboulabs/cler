#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "cler_desktop_utils.hpp"
#include "blocks/sources/source_cariboulite.hpp"
#include "blocks/plots/plot_cspectrum.hpp"
#include "gui_manager.hpp"

int main() {
    const size_t samp_rate = 4'000'000; // 4 MHz
    const float freq_hz = 915e6; // 915 MHz

    SourceCaribouliteBlock source_cariboulite(
        "SourceCaribouLite",
        CaribouLiteRadio::RadioType::S1G,
        static_cast<float>(samp_rate),
        freq_hz,
        true
    );

    PlotCSpectrumBlock plot(
        "Spectrum Plot",
        {"caribou_signal"},
        samp_rate,
        256);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_cariboulite, &plot.in[0]),
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