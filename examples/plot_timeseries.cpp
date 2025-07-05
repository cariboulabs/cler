#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/plot_timeseries.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/throttle.hpp"
#include "blocks/math_complex_demux.hpp"
// #include "blocks/source_chirp.hpp"
#include <complex>

int main() {
    size_t SPS = 100;

    cler::GuiManager gui(1000, 400 , "TimeSeries Plot Example");
    SourceCWBlock<std::complex<float>> source("Source", 1.0f, 2.0f, SPS);
    ThrottleBlock<std::complex<float>> throttle("Throttle", SPS);

    ComplexToMagPhaseBlock demux("Demux", ComplexToMagPhaseBlock::Mode::RealImag, 1024);

    const char* signal_labels[] = {"Real", "Imaginary"};
    PlotTimeSeriesBlock timeseries_plot(
        "TimeSeriesPlot",
        2, // number of inputs
        signal_labels,
        SPS,
        10.0f // duration in seconds
    );

    cler::BlockRunner source_runner(&source, &throttle.in);
    cler::BlockRunner throttle_runner(&throttle, &demux.in);
    cler::BlockRunner demux_runner(&demux, &timeseries_plot.in[0], &timeseries_plot.in[1]);
    cler::BlockRunner timeseries_plot_runner(&timeseries_plot);

    cler::FlowGraph flowgraph(
        source_runner,
        throttle_runner,
        demux_runner,
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
