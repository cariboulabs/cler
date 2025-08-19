#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

int main() {
    cler::GuiManager gui(800, 400, "Hello World Plot Example");

    const size_t SPS = 1000;
    SourceCWBlock<std::complex<float>> source("CWSource", 1.0f, 100.0f, SPS); //amplitude, frequency
    ThrottleBlock<std::complex<float>> throttle("Throttle", SPS);
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    FrequencyShiftBlock frequency_shift("FrequencyShift", 300.0f, SPS);

    PlotCSpectrumBlock plot(
        "Freq shift plot",
        {"original","shifted"},
        SPS,
        256
    );
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f); //x,y, width, height

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &throttle.in),
        cler::BlockRunner(&throttle, &fanout.in),
        cler::BlockRunner(&fanout, &plot.in[0], &frequency_shift.in),
        cler::BlockRunner(&frequency_shift, &plot.in[1]),
        cler::BlockRunner(&plot)
    );

    flowgraph.run();

    while (gui.should_close() == false) {
        gui.begin_frame();
        plot.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}