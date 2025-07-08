#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/plot_timeseries.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/source_chirp.hpp"
#include "blocks/throttle.hpp"
#include "blocks/math_complex_demux.hpp"
#include "blocks/plot_cspectrum.hpp"
#include "blocks/fanout.hpp"
#include "blocks/plot_cspectogram.hpp"

int main() {
    size_t SPS = 100;

    constexpr const size_t GW = 1500;
    constexpr const size_t GH = 800;
    cler::GuiManager gui(GW, GH , "Plots Example");
    
    SourceCWBlock<std::complex<float>> cw_source("CWSource", 1.0f, 2.0f, SPS);
    ThrottleBlock<std::complex<float>> cw_throttle("CWThrottle", SPS);
    FanoutBlock<std::complex<float>> cw_fanout("CWFanout", 3);
    ComplexToMagPhaseBlock cw_complex2realimag("CWComplex2RealImag", ComplexToMagPhaseBlock::Mode::RealImag);
    const char* signal_labels[] = {"Real", "Imaginary"};
    PlotTimeSeriesBlock cw_timeseries_plot(
        "CW-TimeSeriesPlot",
        2, // number of inputs
        signal_labels,
        SPS,
        10.0f //duration in seconds
    );

    SourceChirpBlock<std::complex<float>> chirp_source("ChirpSource", 1.0f, 5.0f, 95.0f, SPS, 10.0f);
    ThrottleBlock<std::complex<float>> chirp_throttle("ChirpThrottle", SPS);
    FanoutBlock<std::complex<float>> chirp_fanout("ChirpFanout", 3);
    ComplexToMagPhaseBlock chirp_c2realimag("ChirpComplex2RealImag", ComplexToMagPhaseBlock::Mode::RealImag);
    PlotTimeSeriesBlock chirp_timeseries_plot(
        "Chirp-TimeSeriesPlot",
        2, // number of inputs
        signal_labels,
        SPS,
        10.0f //duration in seconds
    );
    
    const char* cspectrum_labels[] = {"CW","Chirp"};
    PlotCSpectrumBlock cspectrum_plot(
        "Chirp-CSpectrumPlot",
        2, // number of inputs
        cspectrum_labels,
        SPS,
        256 // buffer size for FFT
    );

    PlotCSpectrogramBlock cspectrogram_plot(
        "CW-SpectrogramPlot",
        2, // number of inputs
        cspectrum_labels,
        SPS,
        256, // buffer size for FFT
        100 // tall
    );

    cw_timeseries_plot.set_initial_window(0.0f, 0.0f, GW / 2.0f, GH / 2.0f);
    chirp_timeseries_plot.set_initial_window(GW /2.0, 0.0f, GW / 2.0f, GH / 2.0f);
    cspectrum_plot.set_initial_window(0.0, GH/2.0, GW / 2.0f, GH / 2.0f);
    cspectrogram_plot.set_initial_window(GW/2.0, GH/2.0, GW / 2.0f, GH / 2.0f);
    
    cler::BlockRunner cw_source_runner(&cw_source, &cw_throttle.in);
    cler::BlockRunner cw_throttle_runner(&cw_throttle, &cw_fanout.in);
    cler::BlockRunner cw_fanout_runner(&cw_fanout, &cw_complex2realimag.in, &cspectrum_plot.in[0], &cspectrogram_plot.in[0]);
    cler::BlockRunner cw_complex2realimag_runner(&cw_complex2realimag, &cw_timeseries_plot.in[0], &cw_timeseries_plot.in[1]);
    cler::BlockRunner cw_timeseries_plot_runner(&cw_timeseries_plot);

    cler::BlockRunner chirp_source_runner(&chirp_source, &chirp_throttle.in);
    cler::BlockRunner chirp_throttle_runner(&chirp_throttle, &chirp_fanout.in);
    cler::BlockRunner chirp_fanout_runner(&chirp_fanout, &chirp_c2realimag.in, &cspectrum_plot.in[1], &cspectrogram_plot.in[1]);
    cler::BlockRunner chirp_complex2realimag_runner(&chirp_c2realimag, &chirp_timeseries_plot.in[0], &chirp_timeseries_plot.in[1]);
    cler::BlockRunner chirp_timeseries_plot_runner(&chirp_timeseries_plot);
    
    cler::BlockRunner cspectrum_plot_runner(&cspectrum_plot);
    cler::BlockRunner cspectrogram_plot_runner(&cspectrogram_plot);

    cler::FlowGraph flowgraph(
        cw_source_runner,
        cw_throttle_runner,
        cw_fanout_runner,
        cw_complex2realimag_runner,
        cw_timeseries_plot_runner,

        chirp_source_runner,
        chirp_throttle_runner,
        chirp_fanout_runner,
        chirp_complex2realimag_runner,
        chirp_timeseries_plot_runner,

        cspectrum_plot_runner,
        cspectrogram_plot_runner
    );

    flowgraph.run();

    //rendering has to happen in the MAIN THREAD
    while (!gui.should_close()) {
        gui.begin_frame();
        cw_timeseries_plot.render();
        chirp_timeseries_plot.render();
        cspectrum_plot.render();
        cspectrogram_plot.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
