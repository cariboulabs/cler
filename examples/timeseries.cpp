#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/plot_timeseries.hpp"
#include "blocks/source_cw.hpp"
// #include "blocks/math_complex2magphase.hpp"
// #include "blocks/source_chirp.hpp"
#include <complex>

int main() {
    size_t SPS = 100;

    cler::GuiManager gui(1000, 400 , "TimeSeries Plot Example");
    SourceCWBlock<float> source("Source", 1.0f, 0.1f, SPS);
    
    const char* signal_labels[] = {"CW"};
    PlotTimeSeriesBlock timeseries_plot(
        "TimeSeriesPlot",
        1, // number of inputs
        signal_labels,
        SPS,
        10.0f // duration in seconds
    );

    cler::BlockRunner source_runner(&source, &timeseries_plot.in[0]);
    cler::BlockRunner timeseries_plot_runner(&timeseries_plot);

    cler::FlowGraph flowgraph(
        source_runner,
        timeseries_plot_runner
    );

    flowgraph.run();

    //rendering has to happen in the MAIN THREAD
    while (!gui.should_close()) {
        gui.begin_frame();
        timeseries_plot.render();
        gui.end_frame();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
