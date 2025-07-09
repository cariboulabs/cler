#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/polyphase_channelizer.hpp"
#include "blocks/add.hpp"
#include "blocks/plot_cspectrum.hpp"
#include "blocks/noise_awgn.hpp"
#include "blocks/fanout.hpp"

float channel_freq(float channel_bw, uint8_t index, uint8_t num_channels) {
    float offset = static_cast<float>(index) - static_cast<float>(num_channels) / 2.0f + 0.5;
    return offset * channel_bw;
}

int main() {
    static constexpr size_t SPS = 2'000'000;
    static constexpr float channel_BW = static_cast<float>(SPS) / 4.0f; // 500kHz channel spacing

    float ch0_freq = channel_freq(channel_BW, 0, 4);
    float ch1_freq = channel_freq(channel_BW, 1, 4);
    float ch2_freq = channel_freq(channel_BW, 2, 4);
    float ch3_freq = channel_freq(channel_BW, 3, 4);

    printf("Channel frequencies:\n"
           "  Channel 0: %.2f Hz\n"
           "  Channel 1: %.2f Hz\n"
           "  Channel 2: %.2f Hz\n"
           "  Channel 3: %.2f Hz\n",
           ch0_freq, ch1_freq, ch2_freq, ch3_freq);
    printf("Please let the spectrum a few seconds windows to stabilize.\n");

    SourceCWBlock<std::complex<float>> cw_source1("CW Source1", 1.0f, ch0_freq, SPS);
    SourceCWBlock<std::complex<float>> cw_source2("CW Source2", 1.0f, ch1_freq, SPS);
    SourceCWBlock<std::complex<float>> cw_source3("CW Source3", 1.0f, ch2_freq, SPS);
    SourceCWBlock<std::complex<float>> cw_source4("CW Source4", 1.0f, ch3_freq, SPS);

    AddBlock<std::complex<float>> adder("Adder", 4);
    NoiseAWGNBlock<std::complex<float>> noise_block("AWGN Noise", 0.01f);
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    PolyphaseChannelizerBlock channelizer(
        "Polyphase Channelizer",
        4, // number of channels
        80.0f, // kaiser attenuation
        3 // kaiser filter semilength
    );

    const char* pfch_signal_labels[] = {
        "pfch 1",
        "pfch 2",
        "pfch 3",
        "pfch 4",
    };
    PlotCSpectrumBlock plot_polyphase_cspectrum(
        "Plot Channelizer Spectrum",
        4, // number of channels
        pfch_signal_labels,
        static_cast<size_t>(channel_BW),
        256 // FFT size
    );

    const char* input_signal_labels[] = {
        "Input",
    };
    PlotCSpectrumBlock plot_input_cspectrum(
        "Plot Input Spectrum",
        1, 
        input_signal_labels,
        SPS,
        256 // FFT size
    );
 
    cler::BlockRunner cw_source1_runner(&cw_source1, &adder.in[0]);
    cler::BlockRunner cw_source2_runner(&cw_source2, &adder.in[1]);
    cler::BlockRunner cw_source3_runner(&cw_source3, &adder.in[2]);
    cler::BlockRunner cw_source4_runner(&cw_source4, &adder.in[3]);
    cler::BlockRunner adder_runner(&adder, &noise_block.in);
    cler::BlockRunner noise_block_runner(&noise_block, &fanout.in);
    cler::BlockRunner fanout_runner(&fanout, &channelizer.in, &plot_input_cspectrum.in[0]);
    cler::BlockRunner channelizer_runner(&channelizer,
        &plot_polyphase_cspectrum.in[0],
        &plot_polyphase_cspectrum.in[1],
        &plot_polyphase_cspectrum.in[2],
        &plot_polyphase_cspectrum.in[3]
    );
    cler::BlockRunner plot_polyphase_cspectrum_runner(&plot_polyphase_cspectrum);
    cler::BlockRunner plot_input_cspectrum_runner(&plot_input_cspectrum);

    cler::FlowGraph flow_graph(
        cw_source1_runner,
        cw_source2_runner,
        cw_source3_runner,
        cw_source4_runner,
        adder_runner,
        noise_block_runner,
        fanout_runner,
        channelizer_runner,
        plot_polyphase_cspectrum_runner,
        plot_input_cspectrum_runner
    );

    flow_graph.run();
    
    const float GW = 1800.0f;
    const float GH = 1000.0f;
    cler::GuiManager gui_manager(GW, GH, "Polyphase Channelizer Example");
    plot_input_cspectrum.set_initial_window(0.0, 0.0, GW, GH/2.0f);
    plot_polyphase_cspectrum.set_initial_window(0.0, GH/2.0f, GW, GH/2.0f);
    while (!gui_manager.should_close()) {
        gui_manager.begin_frame();
        plot_polyphase_cspectrum.render();
        plot_input_cspectrum.render();
        gui_manager.end_frame();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}