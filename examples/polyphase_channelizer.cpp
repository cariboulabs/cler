#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/source_cw.hpp"
#include "blocks/polyphase_channelizer.hpp"
#include "blocks/add.hpp"
#include "blocks/plot_cspectrum.hpp"
#include "blocks/noise_awgn.hpp"
#include "blocks/fanout.hpp"
#include <algorithm>

float channel_freq(float channel_bw, uint8_t index, uint8_t num_channels) {
    float offset = static_cast<float>(index) - static_cast<float>(num_channels) / 2.0f;
    return offset * channel_bw;
}

struct CustomSourceBlock : public cler::BlockBase {
    CustomSourceBlock(const char* name,
                const float amplitude,
                const float noise_stddev,
                const float frequency_hz,
                const size_t sps)
        : BlockBase(name), 
        cw_source_block(name, amplitude, frequency_hz, sps),
        noise_block("AWGN Noise", noise_stddev / 100.0f),
        fanout_block("Fanout", 2) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out1, 
                                                      cler::ChannelBase<std::complex<float>>* out2) {
    
        size_t transferable = std::min({
            out1->space(),
            out2->space(),
            noise_block.in.space(),
            fanout_block.in.space()
        });
        if (transferable == 0) {
            return cler::Error::NotEnoughSpace;
        }
    
        // Generate a continuous wave signal
        auto result = cw_source_block.procedure(&noise_block.in);
        if (result.is_err()) {
            return result.unwrap_err();
        }

        // Add noise to the signal
        result = noise_block.procedure(&fanout_block.in);
        if (result.is_err()) {
            return result.unwrap_err();
        }

        // Fanout the signal to multiple outputs
        return fanout_block.procedure(out1, out2);
    }    

    private:
        SourceCWBlock<std::complex<float>> cw_source_block;
        NoiseAWGNBlock<std::complex<float>> noise_block;
        FanoutBlock<std::complex<float>> fanout_block;

};

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

    CustomSourceBlock cw_source1("CW Source 1", 1.0f, 0.01f, ch0_freq, SPS);
    CustomSourceBlock cw_source2("CW Source 2", 10.0f, 0.01f, ch1_freq, SPS);
    CustomSourceBlock cw_source3("CW Source 3", 100.0f, 0.01f, ch2_freq, SPS);
    CustomSourceBlock cw_source4("CW Source 4", 1000.0f, 0.01f,  ch3_freq, SPS);

    AddBlock<std::complex<float>> adder("Adder", 4);

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
        "source 1",
        "source 2",
        "source 3",
        "source 4"
    };
    PlotCSpectrumBlock plot_input_cspectrum(
        "Plot Input Spectrum",
        4, 
        input_signal_labels,
        SPS,
        256 // FFT size
    );
 
    cler::BlockRunner cw_source1_runner(&cw_source1, &adder.in[0], &plot_input_cspectrum.in[0]);
    cler::BlockRunner cw_source2_runner(&cw_source2, &adder.in[1], &plot_input_cspectrum.in[1]);
    cler::BlockRunner cw_source3_runner(&cw_source3, &adder.in[2], &plot_input_cspectrum.in[2]);
    cler::BlockRunner cw_source4_runner(&cw_source4, &adder.in[3], &plot_input_cspectrum.in[3]);
    cler::BlockRunner adder_runner(&adder, &channelizer.in);
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