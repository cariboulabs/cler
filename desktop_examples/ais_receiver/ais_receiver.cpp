// AIS receiver: both channels (161.975 / 162.025 MHz) from one 2.4 MS/s
// capture centred on 162.0 MHz, decoded to a live ship map.
//   ./ais_receiver                       HackRF
//   ./ais_receiver --sim                 synthetic ships around Haifa bay (no hardware)
//   ./ais_receiver --file capture.cs8    replay an 8-bit IQ capture (hackrf_transfer -f 162000000 -s 2400000)
//   [--lna <dB>] [--vga <dB>] [--amp|--no-amp] [--lat <deg>] [--lon <deg>] [--screenshot <png>]
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/resamplers/rational_resampler.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/ais/ais_decoder.hpp"
#include "desktop_blocks/ais/ais_map.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "ais_source.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    constexpr double RF_RATE = 2.4e6, CENTER = 162.0e6, CH_OFFSET = 25e3, CH_RATE = 48e3;
#ifdef __EMSCRIPTEN__
    AISSourceBlock::Kind kind = AISSourceBlock::Kind::Sim;  // no radio in a browser
#else
    AISSourceBlock::Kind kind = AISSourceBlock::Kind::HackRF;
#endif
    bool amp = true;
    int lna = 40, vga = 30;
    float lat = 32.83f, lon = 35.0f;
    std::string file, screenshot;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--sim") kind = AISSourceBlock::Kind::Sim;
        else if (a == "--file" && i + 1 < argc) { kind = AISSourceBlock::Kind::File; file = argv[++i]; }
        else if (a == "--lna" && i + 1 < argc) lna = std::atoi(argv[++i]);
        else if (a == "--vga" && i + 1 < argc) vga = std::atoi(argv[++i]);
        else if (a == "--amp") amp = true;
        else if (a == "--no-amp") amp = false;
        else if (a == "--lat" && i + 1 < argc) lat = std::stof(argv[++i]);
        else if (a == "--lon" && i + 1 < argc) lon = std::stof(argv[++i]);
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else {
            std::cout << "Usage: " << argv[0] << " [--sim | --file <cs8>] [--lna <dB>] [--vga <dB>] [--amp|--no-amp] [--lat <deg>] [--lon <deg>] [--screenshot <png>]\n";
            return a == "--help" ? 0 : 1;
        }
    }

    // source -> fanout(band plot, ch A, ch B); each channel: shift -> 1/50 -> decoder -> map
    AISSourceBlock source("Source", kind, file, CENTER, RF_RATE, lna, vga, amp,
                          "Sim ships", RF_RATE, size_t{1} << 18);
    FanoutBlock<std::complex<float>> fanout("RF fanout", 3, 1 << 20);
    PlotCSpectrumBlock band("162 MHz band (AIS channels at -25 / +25 kHz)", {"RF"}, static_cast<size_t>(RF_RATE), 4096);
    FrequencyShiftBlock shift_a("A shift", +CH_OFFSET, RF_RATE, 1 << 18);
    FrequencyShiftBlock shift_b("B shift", -CH_OFFSET, RF_RATE, 1 << 18);
    RationalResamplerBlock<1, 50, 160> decim_a("A 1/50", 60.0f, 1 << 18);
    RationalResamplerBlock<1, 50, 160> decim_b("B 1/50", 60.0f, 1 << 18);
    AISDecoderBlock dec_a("161.975 MHz", CH_RATE, 1 << 16);
    AISDecoderBlock dec_b("162.025 MHz", CH_RATE, 1 << 16);
    AISMapBlock map("AIS", 2, lat, lon);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &band.in[0], &shift_a.in, &shift_b.in),
        cler::BlockRunner(&band),
        cler::BlockRunner(&shift_a, &decim_a.in),
        cler::BlockRunner(&shift_b, &decim_b.in),
        cler::BlockRunner(&decim_a, &dec_a.in),
        cler::BlockRunner(&decim_b, &dec_b.in),
        cler::BlockRunner(&dec_a, &map.in[0]),
        cler::BlockRunner(&dec_b, &map.in[1]),
        cler::BlockRunner(&map));

    cler::GuiManager gui(1280, 720, "cler AIS");
    map.set_initial_window(0.0f, 0.0f, 1280.0f, 470.0f);
    band.set_initial_window(0.0f, 470.0f, 1280.0f, 250.0f);

    fg.run();
    const auto started = std::chrono::steady_clock::now();
    bool shot_taken = screenshot.empty();
    while (!gui.should_close()) {
        gui.render(fg);
        if (!shot_taken && std::chrono::steady_clock::now() - started > std::chrono::seconds(12)) {
            gui.request_screenshot(screenshot);
            shot_taken = true;
        }
    }
    fg.stop();
    std::cout << source.kind_name() << " overflows " << source.overflow_count() << "\n"
              << dec_a.name() << ": bursts " << dec_a.bursts() << " frames ok " << dec_a.frames_ok() << " bad crc " << dec_a.frames_bad_crc() << "\n"
              << dec_b.name() << ": bursts " << dec_b.bursts() << " frames ok " << dec_b.frames_ok() << " bad crc " << dec_b.frames_bad_crc() << "\n"
              << "vessels: " << map.vessel_count() << "\n";
    return 0;
}
