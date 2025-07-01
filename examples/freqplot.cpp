#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include "blocks/plot_spectrum.hpp"
#include "blocks/cw_source.hpp"
#include <complex>

int main() {
    cler::GuiManager gui(1000, 400 , "Frequency Plot Example");
    CWSourceBlock<std::complex<float>> source("CwSource", 1, 100, 512);

    // const char* name, size_t num_inputs, const char** signal_labels, size_t work_size, float sample_rate) 
    PlotSpectrumBlock freqplot("FreqPlot", 1, "signal1", 512, 100.0f);

    cler::BlockRunner source_runners{&source, &freqplot.in};
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
