#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "blocks/gui/gui_manager.hpp"
#include "blocks/sources/source_cw.hpp"
#include "blocks/channelizers/polyphase_channelizer.hpp"
#include "blocks/math/add.hpp"
#include "blocks/plots/plot_cspectrum.hpp"
#include "blocks/noise/awgn.hpp"
#include "blocks/utils/fanout.hpp"
#include "blocks/utils/throughput.hpp"

float channel_freq(float channel_bw, uint8_t index, uint8_t num_channels) {
    float offset = static_cast<float>(index) - static_cast<float>(num_channels) / 2.0 + 0.5f;
    return offset * channel_bw;
}

struct CustomSourceBlock : public cler::BlockBase {
    CustomSourceBlock(std::string name,
                const float amplitude,
                const float noise_stddev,
                const float frequency_hz,
                const size_t sps)
        : BlockBase(std::move(name)),
        cw_source_block(std::string(this->name()) + "_CWSource",
                        amplitude, frequency_hz, sps),
        noise_block(std::string(this->name()) + "_AWGN",
                    noise_stddev / 100.0f),
        fanout_block(std::string(this->name()) + "_Fanout", 2)
    { }

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
    static constexpr size_t NUM_CHANNELS = 5;
    static constexpr size_t SPS = 2'000'000;
    static constexpr float channel_BW = static_cast<float>(SPS) / static_cast<float>(NUM_CHANNELS);

    float ch0_freq = channel_freq(channel_BW, 0, NUM_CHANNELS);
    float ch1_freq = channel_freq(channel_BW, 1, NUM_CHANNELS);
    float ch2_freq = channel_freq(channel_BW, 2, NUM_CHANNELS);
    float ch3_freq = channel_freq(channel_BW, 3, NUM_CHANNELS);
    float ch4_freq = channel_freq(channel_BW, 4, NUM_CHANNELS);

    CustomSourceBlock cw_source0("CW Source 0", 1*1.0f, 0.01f, ch0_freq, SPS);
    CustomSourceBlock cw_source1("CW Source 1", 1*10.0f, 0.01f, ch1_freq, SPS);
    CustomSourceBlock cw_source2("CW Source 2", 1*100.0f, 0.01f, ch2_freq, SPS);
    CustomSourceBlock cw_source3("CW Source 3", 1*1000.0f, 0.01f,  ch3_freq, SPS);
    CustomSourceBlock cw_source4("CW Source 4", 1*10000.0f, 0.01f,  ch4_freq, SPS);


    AddBlock<std::complex<float>> adder("Adder", NUM_CHANNELS);

    ThroughputBlock<std::complex<float>> throughput("Throughput");

    PolyphaseChannelizerBlock channelizer(
        "Polyphase Channelizer",
        NUM_CHANNELS, // number of channels
        80.0f, // kaiser attenuation
        3 // kaiser filter semilength
    );

    PlotCSpectrumBlock plot_polyphase_cspectrum(
        "Plot Channelizer Spectrum",
        {"pfch 0", "pfch 1", "pfch 2", "pfch 3", "pfch 4"},
        static_cast<size_t>(channel_BW),
        1024,
        SpectralWindow::BlackmanHarris
    );

    PlotCSpectrumBlock plot_input_cspectrum(
        "Plot Input Spectrum",
        {"source 0", "source 1", "source 2", "source 3", "source 4"},
        SPS,
        1024,
        SpectralWindow::BlackmanHarris
    );
 
    cler::FlowGraph flowgraph(
        cler::BlockRunner(&cw_source0, &adder.in[0], &plot_input_cspectrum.in[0]),
        cler::BlockRunner(&cw_source1, &adder.in[1], &plot_input_cspectrum.in[1]),
        cler::BlockRunner(&cw_source2, &adder.in[2], &plot_input_cspectrum.in[2]),
        cler::BlockRunner(&cw_source3, &adder.in[3], &plot_input_cspectrum.in[3]),
        cler::BlockRunner(&cw_source4, &adder.in[4], &plot_input_cspectrum.in[4]),
        cler::BlockRunner(&adder, &throughput.in),
        cler::BlockRunner(&throughput, &channelizer.in),
        cler::BlockRunner(&channelizer,
            &plot_polyphase_cspectrum.in[0],
            &plot_polyphase_cspectrum.in[1],
            &plot_polyphase_cspectrum.in[2],
            &plot_polyphase_cspectrum.in[3],
            &plot_polyphase_cspectrum.in[4]
        ),
        cler::BlockRunner(&plot_polyphase_cspectrum),
        cler::BlockRunner(&plot_input_cspectrum)
    );

    cler::FlowGraphConfig config;
    config.adaptive_sleep = true;
    flowgraph.run(config);
    
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

    flowgraph.stop();
    print_flowgraph_execution_report(flowgraph);
    throughput.report();

    return 0;
}