// APRS receiver: 144.800 MHz (Europe/IL) from a 2.4 MS/s capture, AFSK1200
// demodulated to a live station map.
//   ./aprs_receiver                       HackRF
//   ./aprs_receiver --sim                 synthetic stations around Haifa (no hardware)
//   ./aprs_receiver --file capture.cs8    replay an 8-bit IQ capture (hackrf_transfer -f 145050000 -s 2400000)
//   [--freq <MHz>] [--lna <dB>] [--vga <dB>] [--amp|--no-amp] [--lat <deg>] [--lon <deg>] [--screenshot <png>]
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/resamplers/rational_resampler.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/fm/fm_demod.hpp"
#include "desktop_blocks/aprs/afsk_demod.hpp"
#include "desktop_blocks/aprs/aprs_map.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "aprs_source.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    // the SDR is tuned IF_OFFSET above the channel and shifted back digitally,
    // keeping the DC spur out of the signal (same trick as fm_radio)
    constexpr double RF_RATE = 2.4e6, IF_OFFSET = 250e3, AUDIO_RATE = 48e3, DEVIATION = 3e3;
    double freq = 144.800e6;
    APRSSourceBlock::Kind kind = APRSSourceBlock::Kind::HackRF;
    bool amp = true, have_origin = false;
    int lna = 40, vga = 30;
    float lat = 32.82f, lon = 35.0f;
    std::string file, screenshot;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--sim") kind = APRSSourceBlock::Kind::Sim;
        else if (a == "--file" && i + 1 < argc) { kind = APRSSourceBlock::Kind::File; file = argv[++i]; }
        else if (a == "--freq" && i + 1 < argc) freq = std::stod(argv[++i]) * 1e6;
        else if (a == "--lna" && i + 1 < argc) lna = std::atoi(argv[++i]);
        else if (a == "--vga" && i + 1 < argc) vga = std::atoi(argv[++i]);
        else if (a == "--amp") amp = true;
        else if (a == "--no-amp") amp = false;
        else if (a == "--lat" && i + 1 < argc) { lat = std::stof(argv[++i]); have_origin = true; }
        else if (a == "--lon" && i + 1 < argc) { lon = std::stof(argv[++i]); have_origin = true; }
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else {
            std::cout << "Usage: " << argv[0] << " [--sim | --file <cs8>] [--freq <MHz>] [--lna <dB>] [--vga <dB>] [--amp|--no-amp] [--lat <deg>] [--lon <deg>] [--screenshot <png>]\n";
            return a == "--help" ? 0 : 1;
        }
    }

    // source -> fanout(band plot, channel); channel: shift -> 1/50 -> NBFM -> AFSK -> map
    APRSSourceBlock source("Source", kind, file, freq + IF_OFFSET, RF_RATE, lna, vga, amp,
                           "Sim stations", RF_RATE, -IF_OFFSET, 3e3, size_t{1} << 18);
    FanoutBlock<std::complex<float>> fanout("RF fanout", 2, 1 << 20);
    PlotCSpectrumBlock band("144.8 MHz band (APRS at -250 kHz)", {"RF"}, static_cast<size_t>(RF_RATE), 4096);
    FrequencyShiftBlock shift("IF shift", +IF_OFFSET, RF_RATE, 1 << 18);
    RationalResamplerBlock<1, 50, 160> decim("1/50", 60.0f, 1 << 18);
    FMDemodBlock fm("NBFM", AUDIO_RATE, DEVIATION, 1 << 16);
    AFSKDemodBlock afsk("AFSK1200", AUDIO_RATE, 1 << 14);
    APRSMapBlock map("APRS", lat, lon, have_origin);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &fanout.in),
        cler::BlockRunner(&fanout, &band.in[0], &shift.in),
        cler::BlockRunner(&band),
        cler::BlockRunner(&shift, &decim.in),
        cler::BlockRunner(&decim, &fm.in),
        cler::BlockRunner(&fm, &afsk.in),
        cler::BlockRunner(&afsk, &map.in),
        cler::BlockRunner(&map));

    cler::GuiManager gui(1280, 720, "cler APRS");
    map.set_initial_window(0.0f, 0.0f, 1280.0f, 470.0f);
    band.set_initial_window(0.0f, 470.0f, 1280.0f, 250.0f);

    fg.run();
    const auto started = std::chrono::steady_clock::now();
    bool shot_taken = screenshot.empty();
    while (!gui.should_close()) {
        gui.render(fg);
        if (!shot_taken && std::chrono::steady_clock::now() - started > std::chrono::seconds(30)) {
            gui.request_screenshot(screenshot);
            shot_taken = true;
        }
    }
    fg.stop();
    std::cout << source.kind_name() << " overflows " << source.overflow_count() << "\n"
              << afsk.name() << ": frames ok " << afsk.frames_ok() << " bad crc " << afsk.frames_bad_crc()
              << " packets " << afsk.packets() << "\n"
              << "stations: " << map.station_count() << "\n";
    return 0;
}
