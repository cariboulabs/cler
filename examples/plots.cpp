#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/plot_timeseries.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/source_chirp.hpp"
#include "blocks/throttle.hpp"
#include "blocks/math_complex_demux.hpp"
#include "blocks/plot_cspectrum.hpp"
#include "blocks/fanout.hpp"
#include "blocks/sink_terminal.hpp"
#include <complex>

int main() {
    size_t SPS = 100;

    cler::GuiManager gui(1000, 400 , "TimeSeries Plot Example");
    
    SourceCWBlock<std::complex<float>> cw_source("CWSource", 1.0f, 2.0f, SPS);
    ThrottleBlock<std::complex<float>> cw_throttle("CWThrottle", SPS);
    ComplexToMagPhaseBlock cw_complex2realimag("CWComplex2RealImag", ComplexToMagPhaseBlock::Mode::RealImag);
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
    FanoutBlock<std::complex<float>> chirp_fanout("ChirpFanout", 2);
    ComplexToMagPhaseBlock chirp_c2realimag("ChirpComplex2RealImag", ComplexToMagPhaseBlock::Mode::RealImag);
    PlotTimeSeriesBlock chirp_timeseries_plot(
        "Chirp-TimeSeriesPlot",
        2, // number of inputs
        signal_labels,
        SPS,
        10.0f //duration in seconds
    );
    PlotCSpectrumBlock chirp_cspectrum_plot(
        "Chirp-CSpectrumPlot",
        2, // number of inputs
        signal_labels,
        SPS,
        256 // buffer size for FFT
    );

    SinkTerminalBlock<std::complex<float>> sink_terminal("SinkTerminal");
    cler::BlockRunner sink_terminal_runner(&sink_terminal);

    cler::BlockRunner cw_source_runner(&cw_source, &cw_throttle.in);
    cler::BlockRunner cw_throttle_runner(&cw_throttle, &cw_complex2realimag.in);
    cler::BlockRunner cw_complex2realimag_runner(&cw_complex2realimag, &cw_timeseries_plot.in[0], &cw_timeseries_plot.in[1]);
    cler::BlockRunner cw_timeseries_plot_runner(&cw_timeseries_plot);

    cler::BlockRunner chirp_source_runner(&chirp_source, &chirp_throttle.in);
    cler::BlockRunner chirp_throttle_runner(&chirp_throttle, &chirp_fanout.in);
    cler::BlockRunner chirp_fanout_runner(&chirp_fanout, &chirp_c2realimag.in, &sink_terminal.in); //end of branch1
    cler::BlockRunner chirp_complex2realimag(&chirp_c2realimag, &chirp_timeseries_plot.in[0], &chirp_timeseries_plot.in[1]);
    cler::BlockRunner chirp_timeseries_plot_runner(&chirp_timeseries_plot);
    cler::BlockRunner chirp_cspectrum_plot_runner(&chirp_cspectrum_plot);

    cler::FlowGraph flowgraph(
        cw_source_runner,
        cw_throttle_runner,
        cw_complex2realimag_runner,
        cw_timeseries_plot_runner,

        chirp_source_runner,
        chirp_throttle_runner,
        chirp_fanout_runner,
        chirp_complex2realimag,
        chirp_timeseries_plot_runner,
        // chirp_cspectrum_plot_runner

        sink_terminal_runner // Uncomment to see the terminal output
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
