#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/polyphase_channelizer.hpp"
#include "blocks/add.hpp"
#include "blocks/plot_cspectrum.hpp"

int main() {
    static constexpr size_t SPS = 500'000;
    static constexpr float dchannel_hz = 500e3f; // 500 kHz

    SourceCWBlock<std::complex<float>> cw_source1("CW Source1", 1.0f, -2.0 * dchannel_hz, SPS);
    SourceCWBlock<std::complex<float>> cw_source2("CW Source2", 1.0f, -1.0 * dchannel_hz, SPS);
    SourceCWBlock<std::complex<float>> cw_source3("CW Source3", 1.0f,  0.0 * dchannel_hz, SPS);
    SourceCWBlock<std::complex<float>> cw_source4("CW Source4", 1.0f,  1.0 * dchannel_hz, SPS);
    SourceCWBlock<std::complex<float>> cw_source5("CW Source5", 1.0f,  2.0 * dchannel_hz, SPS);

    AddBlock<std::complex<float>> adder("Adder", 5);

    PolyphaseChannelizerBlock channelizer(
        "Polyphase Channelizer",
        5, // number of channels
        80.0f, // kaiser attenuation
        3 // kaiser filter semilength
    );

    const char* signal_labels[] = {
        "pfch 1",
        "pfch 2",
        "pfch 3",
        "pfch 4",
        "pfch 5"
    };
    PlotCSpectrumBlock plot_cspectrum(
        "Plot Channelizer Spectrum",
        5, // number of channels
        signal_labels,
        SPS,
        256 // FFT size
    );
 
    cler::BlockRunner cw_source1_runner(&cw_source1, &adder.in[0]);
    cler::BlockRunner cw_source2_runner(&cw_source2, &adder.in[1]);
    cler::BlockRunner cw_source3_runner(&cw_source3, &adder.in[2]);
    cler::BlockRunner cw_source4_runner(&cw_source4, &adder.in[3]);
    cler::BlockRunner cw_source5_runner(&cw_source5, &adder.in[4]);
    cler::BlockRunner adder_runner(&adder, &channelizer.in);
    cler::BlockRunner channelizer_runner(&channelizer,
        &plot_cspectrum.in[0],
        &plot_cspectrum.in[1],
        &plot_cspectrum.in[2],
        &plot_cspectrum.in[3],
        &plot_cspectrum.in[4]
    );
    cler::BlockRunner plot_cspectrum_runner(&plot_cspectrum);

    cler::FlowGraph flow_graph(
        cw_source1_runner,
        cw_source2_runner,
        cw_source3_runner,
        cw_source4_runner,
        cw_source5_runner,
        adder_runner,
        channelizer_runner,
        plot_cspectrum_runner
    );

    flow_graph.run();
    
    cler::GuiManager gui_manager;
    while (!gui_manager.should_close()) {
        gui_manager.begin_frame();
        plot_cspectrum.render();
        gui_manager.end_frame();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}