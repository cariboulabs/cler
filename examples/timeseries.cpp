#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/plot_timeseries.hpp"
#include "blocks/math_complex2magphase.hpp"
#include "blocks/source_chirp.hpp"
#include <complex>

int main() {
    const size_t SPS = 100;

    cler::GuiManager gui(1000, 400 , "TimeSeries Plot Example");
    SourceChirpBlock<std::complex<float>> source("ChirpSource", 0.1f, 0.0, SPS/2, SPS, 1e5, 256);

    ComplexToMagPhaseBlock complex2magphase("complex2magphase", 512);
    const char* signal_labels[] = {"magnitude", "phase"};
    PlotTimeSeriesBlock timeseries_plot("time_series_plot", 2, signal_labels, SPS, 1024);

    cler::BlockRunner source_runner{&source, &complex2magphase.in};
    cler::BlockRunner complex2magphase_runner{&complex2magphase, &timeseries_plot.in[0], &timeseries_plot.in[1]};
    cler::BlockRunner timeseries_plot_runner{&timeseries_plot};

    cler::FlowGraph flowgraph(
        source_runner,
        complex2magphase_runner,
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
