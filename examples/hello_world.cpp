#include "cler.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/throttle.hpp"
#include "blocks/add.hpp"
#include "blocks/plot_timeseries.hpp"
#include "gui_manager.hpp"

int main() {
    cler::GuiManager gui(800, 400, "Hello World Plot Example");

    constexpr const size_t SPS = 1000;
    SourceCWBlock<float> source1("CWSource", 1.0f, 10.0f, SPS); //amplitude, frequency
    SourceCWBlock<float> source2("CWSource2", 1.0f, 20.0f, SPS);
    ThrottleBlock<float> throttle("Throttle", SPS);
    AddBlock<float> adder("Adder", 2); // 2 inputs

    const char* signal_labels[] = {"Added Sources"};
    PlotTimeSeriesBlock plot(
        "Hello World Plot",
        1,
        signal_labels,
        SPS,
        10.0f // duration in seconds
    );
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f); //x,y, width, height

    cler::BlockRunner source1_runner{&source1, &adder.in[0]};
    cler::BlockRunner source2_runner{&source2, &adder.in[1]};
    cler::BlockRunner adder_runner{&adder, &throttle.in};
    cler::BlockRunner throttle_runner{&throttle, &plot.in[0]};
    cler::BlockRunner plot_runner{&plot};

    cler::FlowGraph flowgraph(
        source1_runner,
        source2_runner,
        adder_runner,
        throttle_runner,
        plot_runner
    );

    flowgraph.run();

    while (gui.should_close() == false) {
        gui.begin_frame();
        plot.render();
        gui.end_frame();
    }
    return 0;
}