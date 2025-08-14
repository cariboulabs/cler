#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/sources/source_chirp.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/math/complex_demux.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"

int main() {
    size_t SPS = 200;

    const size_t GW = 1500;
    const size_t GH = 800;
    cler::GuiManager gui(GW, GH , "Plots Example");
    
    SourceCWBlock<std::complex<float>> cw_source("CWSource", 1.0f, 2.0f, SPS);
    ThrottleBlock<std::complex<float>> cw_throttle("CWThrottle", SPS);
    FanoutBlock<std::complex<float>> cw_fanout("CWFanout", 3);
    ComplexToMagPhaseBlock cw_complex2realimag("CWComplex2RealImag", ComplexToMagPhaseBlock::Mode::RealImag);
    PlotTimeSeriesBlock cw_timeseries_plot(
        "CW-TimeSeriesPlot",
        {"Real", "Imaginary"},
        SPS,
        10.0f //duration in seconds
    );

    SourceChirpBlock<std::complex<float>> chirp_source("ChirpSource", 1.0f, 20.0f, 80.0f, SPS, 10.0f);
    ThrottleBlock<std::complex<float>> chirp_throttle("ChirpThrottle", SPS);
    FanoutBlock<std::complex<float>> chirp_fanout("ChirpFanout", 3);
    ComplexToMagPhaseBlock chirp_c2realimag("ChirpComplex2RealImag", ComplexToMagPhaseBlock::Mode::RealImag);
    PlotTimeSeriesBlock chirp_timeseries_plot(
        "Chirp-TimeSeriesPlot",
        {"Real", "Imaginary"},
        SPS,
        10.0f //duration in seconds
    );
    
    PlotCSpectrumBlock cspectrum_plot(
        "Chirp-CSpectrumPlot",
        {"CW", "Chirp"},
        SPS,
        256 // buffer size for FFT
    );

    PlotCSpectrogramBlock cspectrogram_plot(
        "CW-SpectrogramPlot",
        {"CW", "Chirp"},
        SPS,
        128, // buffer size for FFT
        100 // tall
    );

    cw_timeseries_plot.set_initial_window(0.0f, 0.0f, GW / 2.0f, GH / 2.0f);
    chirp_timeseries_plot.set_initial_window(GW /2.0, 0.0f, GW / 2.0f, GH / 2.0f);
    cspectrum_plot.set_initial_window(0.0, GH/2.0, GW / 2.0f, GH / 2.0f);
    cspectrogram_plot.set_initial_window(GW/2.0, GH/2.0, GW / 2.0f, GH / 2.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&cw_source, &cw_throttle.in),
        cler::BlockRunner(&cw_throttle, &cw_fanout.in),
        cler::BlockRunner(&cw_fanout, &cw_complex2realimag.in, &cspectrum_plot.in[0], &cspectrogram_plot.in[0]),
        cler::BlockRunner(&cw_complex2realimag, &cw_timeseries_plot.in[0], &cw_timeseries_plot.in[1]),
        cler::BlockRunner(&cw_timeseries_plot), 

        cler::BlockRunner(&chirp_source, &chirp_throttle.in),
        cler::BlockRunner(&chirp_throttle, &chirp_fanout.in),
        cler::BlockRunner(&chirp_fanout, &chirp_c2realimag.in, &cspectrum_plot.in[1], &cspectrogram_plot.in[1]),
        cler::BlockRunner(&chirp_c2realimag, &chirp_timeseries_plot.in[0], &chirp_timeseries_plot.in[1]),
        cler::BlockRunner(&chirp_timeseries_plot),

        cler::BlockRunner(&cspectrum_plot),
        cler::BlockRunner(&cspectrogram_plot)
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
