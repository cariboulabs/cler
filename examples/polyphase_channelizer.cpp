#include "cler.hpp"
#include "gui/gui_manager.hpp"
#include "blocks/polyphase_channelizer.hpp"
#include "blocks/add.hpp"
#include "blocks/cw_source.hpp"
#include "blocks/plot_time_series.hpp"

int main() {
    CWSourceBlock<float> cw_source1("CW Source", 1, 100, 256);
    CWSourceBlock<float> cw_source2("CW Source", 2, 100, 256);
    // AddBlock<std::complex<float>> adder("Adder", 2, 512, 256);

    const char* signal_labels[] = {
        "Signal 1",
        "Signal 2"
    };
    PlotTimeSeriesBlock plot_times_series("Plot Time Series", 2, signal_labels, 256);


    // PolyphaseChannelizerBlock channelizer("Channelizer", 4, 80.0f, 3, 512, 256);

    cler::BlockRunner cw_source1_runner{&cw_source1, &plot_times_series.in[0]};
    cler::BlockRunner cw_source2_runner{&cw_source2, &plot_times_series.in[1]};
    cler::BlockRunner plot_times_series_runner{&plot_times_series};

    cler::GuiManager gui(1000, 400 , "Polyphase Channelizer Example");

    cler::FlowGraph flowgraph(
        cw_source1_runner,
        cw_source2_runner,
        plot_times_series_runner
    );

    flowgraph.run();

    while (!gui.should_close()) {
        gui.begin_frame();
        plot_times_series.render();
        gui.end_frame();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return 0;
}