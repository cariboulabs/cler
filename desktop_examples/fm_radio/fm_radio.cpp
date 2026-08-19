// Broadcast FM radio: stereo, RDS, seek/scan, live band and MPX spectra.
//   ./fm_radio [--source hackrf|pluto|soapy] [--device <args>] [--freq <MHz>] [--gain <dB>]
//              [--rate <MS/s>] [--lna <dB>] [--vga <dB>] [--amp|--no-amp] [--screenshot <png>]
//   soapy: --device "driver=rtlsdr"; pluto: --device ip:192.168.2.1 (gain < 0 = AGC)
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/fm/fm_demod.hpp"
#include "desktop_blocks/fm/fm_mpx_decoder.hpp"
#include "desktop_blocks/sinks/sink_audio.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "fm_radio_blocks.hpp"
#include "fm_radio_panel.hpp"
#include "fm_radio_source.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    double freq_hz = 100.0e6, gain_db = 30.0, rf_rate = 2.4e6;
    int lna = 32, vga = 20;
    bool amp = true;
    std::string screenshot, source_name = "hackrf", device;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--freq" && i + 1 < argc) freq_hz = std::stod(argv[++i]) * 1e6;
        else if (arg == "--source" && i + 1 < argc) source_name = argv[++i];
        else if (arg == "--device" && i + 1 < argc) device = argv[++i];
        else if (arg == "--gain" && i + 1 < argc) gain_db = std::stod(argv[++i]);
        else if (arg == "--rate" && i + 1 < argc) rf_rate = std::stod(argv[++i]) * 1e6;
        else if (arg == "--lna" && i + 1 < argc) lna = std::atoi(argv[++i]);
        else if (arg == "--vga" && i + 1 < argc) vga = std::atoi(argv[++i]);
        else if (arg == "--amp") amp = true;
        else if (arg == "--no-amp") amp = false;
        else if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else {
            std::cout << "Usage: " << argv[0] << " [--source hackrf|pluto|soapy] [--device <args>] [--freq <MHz>] [--gain <dB>]\n"
                         "          [--rate <MS/s>: " << ChannelResampler::menu() << "] [--lna <dB>] [--vga <dB>] [--amp|--no-amp] [--screenshot <png>]\n";
            return arg == "--help" ? 0 : 1;
        }
    }

    // Tune the hardware 250 kHz above the station so the DC spur lands outside
    // the channel, then shift it back digitally.
    if (!ChannelResampler::supported(rf_rate)) {
        std::cerr << "fm_radio: --rate must be one of " << ChannelResampler::menu() << "\n";
        return 1;
    }
    const double RF_RATE = rf_rate;
    constexpr double IF_OFFSET = 250e3, MPX_RATE = ChannelResampler::MPX_RATE, AUDIO_RATE = MPX_RATE / 5;

    // Sink-paced pipeline: every buffer between source and sink sits full, so
    // the first channel after the source is the only slack against a blocking
    // audio write (HackRF also has its own 2M-sample ring).
    RadioSource source("Source", RadioSource::parse_kind(source_name), device,
                       freq_hz + IF_OFFSET, RF_RATE, gain_db, lna, vga, amp);
    FanoutBlock<std::complex<float>> rf_fanout("RF fanout", 2, 1 << 20);
    PlotCSpectrumBlock band_plot("Band (RF around the tuned station)", {"RF"}, static_cast<size_t>(RF_RATE), 2048);
    FrequencyShiftBlock shift("IF shift", +IF_OFFSET, RF_RATE, 1 << 18);
    ChannelResampler channel("Channel", RF_RATE, 60.0f, 1 << 18);
    FMDemodBlock demod("FM demod", MPX_RATE, 75e3, 1 << 16);
    FanoutBlock<float> mpx_fanout("MPX fanout", 2, 1 << 16);
    RealToComplexBlock mpx_cplx("MPX->C", 1 << 16);
    PlotCSpectrumBlock mpx_plot("MPX (demodulated: audio 0-15k | pilot 19k | stereo 23-53k | RDS 57k)", {"mpx"}, static_cast<size_t>(MPX_RATE), 2048);
    FMMpxDecoderBlock mpx("MPX decoder", MPX_RATE, 5, 50.0, 1 << 16);
    VolumeBlock volume("Volume", 1.0f, 1 << 14);
    SinkAudioBlock audio("Audio", AUDIO_RATE, paNoDevice, 4096, 2, 0.3);
    FmRadioPanel panel("FM Radio", source, mpx, volume, IF_OFFSET, freq_hz, gain_db);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &rf_fanout.in),
        cler::BlockRunner(&rf_fanout, &band_plot.in[0], &shift.in),
        cler::BlockRunner(&band_plot),
        cler::BlockRunner(&shift, &channel.in),
        cler::BlockRunner(&channel, &demod.in),
        cler::BlockRunner(&demod, &mpx_fanout.in),
        cler::BlockRunner(&mpx_fanout, &mpx_cplx.in, &mpx.in),
        cler::BlockRunner(&mpx_cplx, &mpx_plot.in[0]),
        cler::BlockRunner(&mpx_plot),
        cler::BlockRunner(&mpx, &volume.in),
        cler::BlockRunner(&volume, &audio.in),
        cler::BlockRunner(&audio),
        cler::BlockRunner(&panel));

    cler::GuiManager gui(1280, 720, "cler FM radio");
    band_plot.set_initial_window(420.0f, 10.0f, 850.0f, 340.0f);
    mpx_plot.set_initial_window(420.0f, 360.0f, 850.0f, 340.0f);

    cler::FlowGraphConfig cfg; cfg.collect_detailed_stats = std::getenv("FM_STATS") != nullptr;
    fg.run(cfg);
    const auto started = std::chrono::steady_clock::now();
    bool shot_taken = screenshot.empty();
    while (!gui.should_close()) {
        gui.render(fg);
        if (!shot_taken && std::chrono::steady_clock::now() - started > std::chrono::seconds(4)) {
            gui.request_screenshot(screenshot);
            shot_taken = true;
        }
    }
    fg.stop();
    print_flowgraph_execution_report(fg);
    return 0;
}
