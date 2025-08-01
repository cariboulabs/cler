#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/math/add.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

int main() {
    cler::GuiManager gui(800, 400, "Hello World Plot Example");

    const size_t SPS = 1000;
    SourceCWBlock<float> source1("CWSource", 1.0f, 1.0f, SPS); //amplitude, frequency
    SourceCWBlock<float> source2("CWSource2", 1.0f, 20.0f, SPS);
    ThrottleBlock<float> throttle("Throttle", SPS);
    AddBlock<float> adder("Adder", 2); // 2 inputs

    PlotTimeSeriesBlock plot(
        "Hello World Plot",
        {"Added Sources"},
        SPS,
        3.0f // duration in seconds
    );
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f); //x,y, width, height

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source1, &adder.in[0]),
        cler::BlockRunner(&source2, &adder.in[1]),
        cler::BlockRunner(&adder, &throttle.in),
        cler::BlockRunner(&throttle, &plot.in[0]),
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