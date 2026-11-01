// Band scanner: wideband spectrum + waterfall, double-click a signal to tune
// it, demodulated as WBFM / NBFM / AM / USB / LSB.
//   ./scanner [--freq <MHz>] [--rate <MS/s>] [--mode wbfm|nbfm|am|usb|lsb]
//             [--lna <dB>] [--vga <dB>] [--amp|--no-amp] [--screenshot <png>]
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_hackrf.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/resamplers/rational_resampler.hpp"
#include "desktop_blocks/demod/analog_demod.hpp"
#include "desktop_blocks/sigmf/recorder_sigmf.hpp"
#include "desktop_blocks/sinks/sink_audio.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "scanner_panel.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    double center_hz = 100.0e6, rate_hz = 2.4e6;
    AnalogDemodBlock::Mode mode = AnalogDemodBlock::Mode::WBFM;
    int lna = 32, vga = 20;
    bool amp = true;
    std::string screenshot;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--freq" && i + 1 < argc) center_hz = std::stod(argv[++i]) * 1e6;
        else if (a == "--rate" && i + 1 < argc) rate_hz = std::stod(argv[++i]) * 1e6;
        else if (a == "--mode" && i + 1 < argc) {
            std::string m = argv[++i];
            mode = m == "nbfm" ? AnalogDemodBlock::Mode::NBFM
                 : m == "am" ? AnalogDemodBlock::Mode::AM
                 : m == "usb" ? AnalogDemodBlock::Mode::USB
                 : m == "lsb" ? AnalogDemodBlock::Mode::LSB
                 : AnalogDemodBlock::Mode::WBFM;
        }
        else if (a == "--lna" && i + 1 < argc) lna = std::atoi(argv[++i]);
        else if (a == "--vga" && i + 1 < argc) vga = std::atoi(argv[++i]);
        else if (a == "--amp") amp = true;
        else if (a == "--no-amp") amp = false;
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else {
            std::cout << "Usage: " << argv[0] << " [--freq <MHz>] [--rate <MS/s>] [--mode wbfm|nbfm|am|usb|lsb]\n"
                         "          [--lna <dB>] [--vga <dB>] [--amp|--no-amp] [--screenshot <png>]\n";
            return a == "--help" ? 0 : 1;
        }
    }
    if (std::fabs(rate_hz - 2.4e6) > 1.0) {
        std::cerr << "scanner: only --rate 2.4 is wired (1/10 to the 240 kHz channel)\n";
        return 1;
    }

    // source -> fanout(spectrum, waterfall, tuner); tuner: shift -> 1/10 -> demod -> audio
    SourceHackRFBlock source("HackRF", static_cast<uint64_t>(center_hz),
                             static_cast<uint32_t>(rate_hz), lna, vga, amp, size_t{1} << 21);
    FanoutBlock<std::complex<float>> fanout("RF fanout", 4, 1 << 20);
    PlotCSpectrumBlock spectrum("Spectrum (double-click to tune)", {"RF"}, static_cast<size_t>(rate_hz), 4096);
    PlotCSpectrogramBlock waterfall("Waterfall", {"RF"}, static_cast<size_t>(rate_hz), 2048, 600);
    FrequencyShiftBlock shift("Tune shift", 0.0, rate_hz, 1 << 18);
    RationalResamplerBlock<1, 10, 160> channel("Channel", 60.0f, 1 << 18);
    AnalogDemodBlock demod("Demod", rate_hz / 10.0, mode, 1 << 16);
    SinkAudioBlock audio("Audio", 48000.0, paNoDevice, 4096, 1, 0.3);
    SigMFRecorderBlock recorder("Recorder", rate_hz, 1 << 20);
    ScannerPanel panel("Scanner", source, shift, demod, spectrum, recorder, center_hz, rate_hz);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &waterfall.in[0], &shift.in, &recorder.in),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&waterfall),
        cler::BlockRunner(&shift, &channel.in),
        cler::BlockRunner(&channel, &demod.in),
        cler::BlockRunner(&demod, &audio.in),
        cler::BlockRunner(&recorder),
        cler::BlockRunner(&audio),
        cler::BlockRunner(&panel));

    cler::GuiManager gui(1280, 800, "cler scanner");
    spectrum.set_initial_window(340.0f, 0.0f, 940.0f, 400.0f);
    waterfall.set_initial_window(340.0f, 400.0f, 940.0f, 400.0f);

    cler::FlowGraphConfig cfg; cfg.collect_detailed_stats = std::getenv("SCANNER_STATS") != nullptr;
    fg.run(cfg);
    const auto started = std::chrono::steady_clock::now();
    bool shot_taken = screenshot.empty();
    while (!gui.should_close()) {
        gui.render(fg);
        if (!shot_taken && std::chrono::steady_clock::now() - started > std::chrono::seconds(6)) {
            gui.request_screenshot(screenshot);
            shot_taken = true;
        }
    }
    fg.stop();
    print_flowgraph_execution_report(fg);
    return 0;
}
