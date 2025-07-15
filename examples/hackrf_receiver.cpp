#include "cler.hpp"
#include "blocks/sources/source_hackrf.hpp"
#include "blocks/plots/plot_cspectrum.hpp"

int main() {
    const uint32_t samp_rate = 4'000'000; // 4 MHz
    const uint64_t freq_hz = 915e6;       // 915 MHz

    SourceHackRFBlock source_hackrf(
        "SourceHackRF",
        freq_hz,
        samp_rate
    );

    PlotCSpectrumBlock plot_cspectrum(
        "Spectrum Plot",
        {"hackrf_signal"},
        samp_rate,
        256 // FFT size
    );

    cler::FlowGraph flowgraph(
        cler::BlockRunner(&source_hackrf, &plot_cspectrum.in[0]),
        cler::BlockRunner(&plot_cspectrum)
    );

    flowgraph.run();

    while (true) {
        // Keep the main thread alive
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
