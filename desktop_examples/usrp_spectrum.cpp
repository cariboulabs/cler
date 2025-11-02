#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_uhd_zohar.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"


int main() {
    const double samp_rate = 2e6; // 2 MSPS
    const double freq_hz = 915e6;
    const size_t FFT_SIZE = 1024;

    SourceUHDBlock<std::complex<float>> source_usrp(
        "SourceUSRP",
        "",        // empty device args, default device
        freq_hz,
        samp_rate
    );

    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    PlotCSpectrumBlock spectrum("Spectrum", {"usrp_signal"}, samp_rate, FFT_SIZE);
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"usrp_signal"}, samp_rate, FFT_SIZE, 1000);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_usrp, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram)
    );

    cler::GuiManager gui(800, 400, "USRP Receiver Example");
    spectrum.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f);

    cler::FlowGraphConfig config;
    config.collect_detailed_stats = true;
    flowgraph.run(config);

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        spectrogram.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    for (const auto& s : flowgraph.stats()) {
        printf("%s: %zu successful, %zu failed, %.1f%% CPU\n",
                s.name.c_str(), s.successful_procedures, s.failed_procedures,
                s.get_cpu_utilization_percent());
    }

    print_flowgraph_execution_report(flowgraph);
    return 0;
}