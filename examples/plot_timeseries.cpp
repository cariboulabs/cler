#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/plot_timeseries.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/source_chirp.hpp"
#include "blocks/throttle.hpp"
#include "blocks/math_complex_demux.hpp"
#include <complex>

int main() {
    size_t SPS = 100;

    cler::GuiManager gui(1000, 400 , "TimeSeries Plot Example");
    
    SourceCWBlock<std::complex<float>> cw_source("CWSource", 1.0f, 2.0f, SPS);
    ThrottleBlock<std::complex<float>> cw_throttle("CWThrottle", SPS);
    ComplexToMagPhaseBlock cw_demux("CWDemux", ComplexToMagPhaseBlock::Mode::RealImag, 1024);
    const char* signal_labels[] = {"Real", "Imaginary"};
    PlotTimeSeriesBlock cw_timeseries_plot(
        "CW-TimeSeriesPlot",
        2, // number of inputs
        signal_labels,
        SPS,
        10.0f //duration in seconds
    );

    SourceChirpBlock<std::complex<float>> chirp_source("ChirpSource", 1.0f, 1.0f, 10.0f, SPS, 10.0f);
    ThrottleBlock<std::complex<float>> chirp_throttle("ChirpThrottle", SPS);
    ComplexToMagPhaseBlock chirp_demux("ChirpDemux", ComplexToMagPhaseBlock::Mode::RealImag, 1024);
    PlotTimeSeriesBlock chirp_timeseries_plot(
        "Chirp-TimeSeriesPlot",
        2, // number of inputs
        signal_labels,
        SPS,
        10.0f //duration in seconds
    );

    cler::BlockRunner cw_source_runner(&cw_source, &cw_throttle.in);
    cler::BlockRunner cw_throttle_runner(&cw_throttle, &cw_demux.in);
    cler::BlockRunner cw_demux_runner(&cw_demux, &cw_timeseries_plot.in[0], &cw_timeseries_plot.in[1]);
    cler::BlockRunner cw_timeseries_plot_runner(&cw_timeseries_plot);

    cler::BlockRunner chirp_source_runner(&chirp_source, &chirp_throttle.in);
    cler::BlockRunner chirp_throttle_runner(&chirp_throttle, &chirp_demux.in);
    cler::BlockRunner chirp_demux_runner(&chirp_demux, &chirp_timeseries_plot.in[0], &chirp_timeseries_plot.in[1]);
    cler::BlockRunner chirp_timeseries_plot_runner(&chirp_timeseries_plot);


    cler::FlowGraph flowgraph(
        cw_source_runner,
        cw_throttle_runner,
        cw_demux_runner,
        cw_timeseries_plot_runner,

        chirp_source_runner,
        chirp_throttle_runner,
        chirp_demux_runner,
        chirp_timeseries_plot_runner
    );

    flowgraph.run();

    //rendering has to happen in the MAIN THREAD
    while (!gui.should_close()) {
        gui.begin_frame();
        chirp_timeseries_plot.render();
        cw_timeseries_plot.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
