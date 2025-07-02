#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/plot_spectrum.hpp"
#include "blocks/cw_source.hpp"
#include <complex>

int main() {
    cler::GuiManager gui(1000, 400 , "Frequency Plot Example");
    CWSourceBlock<std::complex<float>> source("CwSource", 0.1, 50, 100, 512);

    const char* signal_labels[] = {"signal1"};
    PlotSpectrumBlock freqplot("FreqPlot", 1, signal_labels, 513, 100.0f);

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
