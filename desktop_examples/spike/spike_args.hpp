#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

#include "spike_source.hpp"

struct SpikeArgs {
    SourceKind source = SourceKind::UHD;
    double freq = 915e6;
    double rate = 1e6;
    double gain = 30.0;
    int    lna  = 40;
    int    vga  = 16;
    bool   amp  = false;
    size_t fft  = 2048;
    double history_s = 0.0;
    bool   rate_from_cli = false;
    std::string device_address;

    std::string capture_dir;
    int    capture_frames    = 1;
    double capture_timeout_s = 30.0;
    bool   capture_exit      = false;
    bool   capture_force     = false;
    bool   capture_no_dat    = false;
    bool   capture_mode() const { return !capture_dir.empty(); }
};

// The AD9361 floor is 2.083 MS/s and the HackRF wants >= 2 MS/s; only the USRP takes 1.
static double default_rate_for(SourceKind kind) {
    switch (kind) {
        case SourceKind::Pluto:  return 2.4e6;
        case SourceKind::HackRF: return 2.4e6;
        case SourceKind::UHD:    break;
    }
    return 1e6;
}

static void print_usage(const char* prog) {
    std::cout << "\nSlim Spike-like analyzer for USRP / HackRF / Pluto\n"
              << "Usage: " << prog << " [OPTIONS]\n"
              << "  -s, --source DEV  Device source: uhd|hackrf|pluto (default uhd)\n"
              << "  -f, --freq FREQ   Center frequency Hz (default 915e6; clamped to what the\n"
              << "                    device tunes, which is reported when it happens)\n"
              << "  -r, --rate RATE   Initial sample rate S/s (default 1e6 for uhd, 2.4e6 for\n"
              << "                    hackrf/pluto, whose hardware floors are higher;\n"
              << "                    live-tunable in the GUI; if given, overrides the\n"
              << "                    saved rate and is clamped to what the device takes)\n"
              << "  -g, --gain GAIN   [uhd/pluto] Gain dB (default 30; pluto applies it\n"
              << "                    at start only, <0 selects AGC)\n"
              << "      --lna  DB     [hackrf] LNA gain dB, 0-40 step 8 (default 40)\n"
              << "      --vga  DB     [hackrf] VGA gain dB, 0-62 step 2 (default 16)\n"
              << "      --amp  0|1    [hackrf] RX amp on/off (default 0)\n"
              << "  -F, --fft  SIZE   FFT size for spectrum view (default 2048)\n"
              << "  -H, --history SEC Minimum waterfall time-depth in seconds (sizes the\n"
              << "                    spectrogram ring; default ~4.1s at 1MS/s. Lower this\n"
              << "                    to reach a shorter history, e.g. --history 1)\n"
              << "  -d, --dev  ADDR   [uhd] USRP device address (default auto)\n"
              << "                    [pluto] IIO uri (default ip:192.168.2.1)\n"
              << "                    [hackrf] serial (default first device found)\n"
              << "  -h, --help\n"
              << "\nUnattended capture (for scripts / agents; needs a display):\n"
              << "      --capture DIR      Snapshot automatically into DIR (created if\n"
              << "                         needed): <base>.png screenshot of the whole\n"
              << "                         window + <base>.dat plot data, same as the\n"
              << "                         Snapshot button. Prints each path on stdout.\n"
              << "                         Forces the trigger scope visible and does NOT\n"
              << "                         save settings back to the conf file.\n"
              << "      --capture-on-trigger N  Snapshot on each of the next N trigger\n"
              << "                         frames (default 1)\n"
              << "      --capture-timeout S     Give up waiting for a trigger frame after\n"
              << "                         S seconds; snapshots the current view anyway\n"
              << "                         and exits 2 (default 30, 0 = wait forever)\n"
              << "      --capture-force    Force the trigger for each snapshot, so a frame\n"
              << "                         appears with no signal over the threshold\n"
              << "      --capture-no-dat   Screenshot only, skip the .dat (which carries the\n"
              << "                         spectrogram blob and can reach tens of MB)\n"
              << "      --capture-exit     Quit once the last snapshot is written\n"
              << "\n  e.g. " << prog << " -s hackrf --capture /tmp/shots \\\n"
              << "            --capture-on-trigger 1 --capture-timeout 20 --capture-exit\n"
              << std::endl;
}

static SpikeArgs parse_args(int argc, char** argv) {
    SpikeArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "Error: " << arg << " needs a value\n"; exit(1); }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") { print_usage(argv[0]); exit(0); }
        else if (arg == "-s" || arg == "--source") {
            std::string v = next();
            if (v == "uhd" || v == "usrp") a.source = SourceKind::UHD;
            else if (v == "hackrf")        a.source = SourceKind::HackRF;
            else if (v == "pluto")         a.source = SourceKind::Pluto;
            else { std::cerr << "Unknown source: " << v << " (want uhd|hackrf|pluto)\n"; exit(1); }
        }
        else if (arg == "-f" || arg == "--freq") a.freq = std::stod(next());
        else if (arg == "-r" || arg == "--rate") { a.rate = std::stod(next()); a.rate_from_cli = true; }
        else if (arg == "-g" || arg == "--gain") a.gain = std::stod(next());
        else if (arg == "--lna") a.lna = std::stoi(next());
        else if (arg == "--vga") a.vga = std::stoi(next());
        else if (arg == "--amp") a.amp = (std::stoi(next()) != 0);
        else if (arg == "-F" || arg == "--fft")  a.fft  = std::stoul(next());
        else if (arg == "-H" || arg == "--history") a.history_s = std::stod(next());
        else if (arg == "-d" || arg == "--dev" || arg == "--device") a.device_address = next();
        else if (arg == "--capture") a.capture_dir = next();
        else if (arg == "--capture-on-trigger") a.capture_frames = std::stoi(next());
        else if (arg == "--capture-timeout") a.capture_timeout_s = std::stod(next());
        else if (arg == "--capture-force")  a.capture_force  = true;
        else if (arg == "--capture-no-dat") a.capture_no_dat = true;
        else if (arg == "--capture-exit")  a.capture_exit  = true;
        else { std::cerr << "Unknown option: " << arg << "\n"; print_usage(argv[0]); exit(1); }
    }
    if (!a.rate_from_cli) a.rate = default_rate_for(a.source);
    if (a.capture_frames < 1) {
        std::cerr << "Error: --capture-on-trigger needs N >= 1\n"; exit(1);
    }
    if (a.capture_timeout_s < 0.0) {
        std::cerr << "Error: --capture-timeout must be >= 0\n"; exit(1);
    }
    if (!a.capture_mode() &&
        (a.capture_exit || a.capture_force || a.capture_no_dat ||
         a.capture_frames != 1)) {
        std::cerr << "Error: --capture-* options need --capture DIR\n"; exit(1);
    }
    return a;
}
