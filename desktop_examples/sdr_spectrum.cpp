// Spectrum + spectrogram for any SDR cler knows: pass --source, or run with
// none to get the first device found (simulator when nothing is plugged in).
//   ./sdr_spectrum [--source hackrf[:serial]|pluto[:uri]|uhd[:addr]|cariboulite:s1g|soapy:<kwargs>|sim]
//                  [--freq Hz] [--rate Hz] [--fft N]
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_mux.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    std::string source;
    double freq = 100e6, rate = 2.4e6;
    size_t fft = 1024;
    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* flag) -> const char* { return (!std::strcmp(argv[i], flag) && i + 1 < argc) ? argv[++i] : nullptr; };
        if (const char* v = next("--source")) source = v;
        else if (const char* v = next("--freq")) freq = std::atof(v);
        else if (const char* v = next("--rate")) rate = std::atof(v);
        else if (const char* v = next("--fft")) fft = std::atoi(v);
        else {
            std::printf("Usage: %s [--source KIND[:ID]] [--freq Hz] [--rate Hz] [--fft N]\n", argv[0]);
            return std::strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }

    SourceMux src("Source");
    if (source.empty()) {
        const auto devs = src.enumerate();
        if (!src.select(devs.front().kind, devs.front().id, freq, rate)) cler::panic("sdr_spectrum: cannot open the first device");
        std::printf("using %s\n", devs.front().label.c_str());
    } else {
        SourceMux::Kind kind = SourceMux::Kind::None;
        std::string id;
        const size_t c = source.find(':');
        const std::string k = source.substr(0, c);
        id = c == std::string::npos ? "" : source.substr(c + 1);
        for (auto kk : {SourceMux::Kind::HackRF, SourceMux::Kind::Pluto, SourceMux::Kind::UHD,
                        SourceMux::Kind::Cariboulite, SourceMux::Kind::Soapy, SourceMux::Kind::Sim}) {
            if (k == SourceMux::kind_name(kk)) kind = kk;
        }
        if (kind == SourceMux::Kind::None || !src.select(kind, id, freq, rate)) cler::panic("sdr_spectrum: cannot open --source");
    }
    rate = src.rate();

    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    PlotCSpectrumBlock spectrum("Spectrum", {"sdr"}, static_cast<size_t>(rate), fft);
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"sdr"}, static_cast<size_t>(rate), fft, 1000);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&src, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram));

    cler::GuiManager gui(1000, 700, "SDR Spectrum");
    spectrum.set_initial_window(0.0f, 0.0f, 1000.0f, 350.0f);
    spectrogram.set_initial_window(0.0f, 350.0f, 1000.0f, 350.0f);

    flowgraph.run();
    while (!gui.should_close()) {
        gui.render(flowgraph);
    }
    flowgraph.stop();
    return 0;
}
