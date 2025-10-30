// #include "cler.hpp"
// #include "cler_desktop_utils.hpp"
// // #include "desktop_blocks/sources/source_uhd.hpp"
// #include "desktop_blocks/sources/source_uhd_zohar.hpp"
// // #include "desktop_blocks/sources/source_hackrf.hpp"
// #include "desktop_blocks/plots/plot_cspectrum.hpp"
// #include "desktop_blocks/plots/plot_cspectrogram.hpp"
// #include "desktop_blocks/utils/fanout.hpp"
// #include "desktop_blocks/gui/gui_manager.hpp"


// int main() {
//     // if (hackrf_init() != HACKRF_SUCCESS) {
//     //     throw std::runtime_error("Failed to initialize HackRF library");
//     // }
//     const uint32_t samp_rate = 4'000'000; // 4 MHz
//     const uint64_t freq_hz = 915e6;       // 915 MHz
//     const size_t FFT_SIZE = 1024; // FFT size
//     const double gain = 30.0; // RX Gain in dB
//     std::string device_args = ""; // Default device
//     SourceUHDZoharBlock<std::complex<float>> usrp("USRP", device_args, freq_hz, samp_rate, gain);

//     FanoutBlock<std::complex<float>> fanout("Fanout", 2);

//     PlotCSpectrumBlock timeplot(
//         "Spectrum Plot",
//         {"uhd_zohar_signal"},
//         samp_rate,
//         FFT_SIZE
//     );

//     PlotCSpectrogramBlock spectrogram(
//         "Spectrogram",
//         {"uhd_zohar_signal"},
//         samp_rate,
//         FFT_SIZE,
//         1000
//     );

//     auto flowgraph = cler::make_desktop_flowgraph(
//         cler::BlockRunner(&usrp, &fanout.in),
//         cler::BlockRunner(&fanout, &timeplot.in[0], &spectrogram.in[0]),
//         cler::BlockRunner(&spectrogram),
//         cler::BlockRunner(&timeplot)
//     );

//     cler::GuiManager gui(800, 400, "uhd_zohar Receiver Example");
//     timeplot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f);

//     flowgraph.run();

//     while (gui.should_close() == false) {
//         gui.begin_frame();
//         timeplot.render();
//         spectrogram.render();
//         gui.end_frame();
//         std::this_thread::sleep_for(std::chrono::milliseconds(20));
//     }
//     flowgraph.stop();
//     print_flowgraph_execution_report(flowgraph);

//     // uhd_zohar_exit();
//     return 0;
// }

//chatgpt suggestions
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

    flowgraph.run();

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        spectrogram.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    print_flowgraph_execution_report(flowgraph);

    return 0;
}

