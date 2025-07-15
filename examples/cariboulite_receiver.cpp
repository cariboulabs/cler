#include "cler.hpp"
#include "blocks/sources/source_cariboulite.hpp"
#include "blocks/plots/plot_cspectrum.hpp"

int main() {
    const size_t samp_rate = 4'000'000; // 4 MHz
    const float freq_hz = 915e6; // 915 MHz

    SourceCaribouliteBlock source_cariboulite(
        "SourceCaribouLite",
        CaribouLiteRadio::RadioType::S1G,
        static_cast<float>(samp_rate),
        freq_hz,
        true
    );

    PlotCSpectrumBlock plot_cspectrum(
        "Spectrum Plot",
        {"caribou_signal"},
        samp_rate,
        256);

    cler::FlowGraph flowgraph(
        cler::BlockRunner(&source_cariboulite, &plot_cspectrum.in[0]),
        cler::BlockRunner(&plot_cspectrum)
    );

    flowgraph.run();

    while (true) {
        // Simulate some work in the main thread
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}