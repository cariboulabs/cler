#include "cler.hpp"
#include "blocks/sources/source_cariboulite.hpp"
#include "blocks/plots/plot_cspectrum.hpp"

int main() {
    SourceCaribouliteBlock source_cariboulite(
        "SourceCaribouLite",
        CaribouLiteRadio::RadioType::S1G,
        915e6,
        true
    );

    PlotCSpectrumBlock plot_cspectrum(
        "Spectrum Plot",
        {"caribou_signal"},
        4'000'000,
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