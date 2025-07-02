#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/plot_spectrum.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/source_chirp.hpp"
#include <complex>

int main() {
    const size_t SPS = 100;

    cler::GuiManager gui(1000, 400 , "Frequency Plot Example");
    // SourceCWBlock<std::complex<float>> source("CwSource", 0.1f, 5, SPS, 512);
    SourceChirpBlock<std::complex<float>> source("ChirpSource", 0.1f, 0.0, SPS/2, SPS, 1e5, 512);

    const char* signal_labels[] = {"signal1"};
    PlotSpectrumBlock freqplot("FreqPlot", 1, signal_labels, SPS, 256);

    cler::BlockRunner source_runners{&source, &freqplot.in[0]};
    cler::BlockRunner freqplot_runners{&freqplot};

    cler::FlowGraph flowgraph(
        source_runners,
        freqplot_runners
    );

    flowgraph.run();

    //rendering has to happen in the MAIN THREAD
    while (!gui.should_close()) {
        gui.begin_frame();
        freqplot.render();
        gui.end_frame();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
