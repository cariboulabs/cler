#pragma once

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "control_panel.hpp"

static std::string config_path(const char* leaf) {
    const char* home = std::getenv("HOME");
    std::string dir = home ? std::string(home) : std::string(".");
    return dir + "/" + leaf;
}

// Snapshot: <base>.png (screenshot, GuiManager) + <base>.dat (plot data); runs on the GUI thread.

static bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// dir/spike_YYYYmmdd_HHMMSS, suffixed _1,_2... until both files are free (shared suffix); empty if none free.
static std::string snapshot_base_path(const std::string& dir) {
    char ts[32];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmv);
    const std::string stem = dir + "/spike_" + ts;
    std::string base = stem;
    for (int i = 1; file_exists(base + ".png") || file_exists(base + ".dat"); ++i) {
        if (i > 999) return std::string();
        base = stem + "_" + std::to_string(i);
    }
    return base;
}

// .dat format: line-oriented text with at most one trailing binary blob. One
// section per VISIBLE plot with data:
//   [trigger]     n/pre_ms/post_ms/frame, then CSV time_ms,power_db (t=0 at trigger)
//   [spectrum]    n_fft, then CSV freq_hz,mag_db (baseband)
//   [spectrogram] rows/cols/row_seconds/freq_min_hz/freq_max_hz, then
//                 "BINARY <nbytes>" + raw little-endian float32 dB, row-major, newest row first
static bool write_snapshot_dat(const std::string& path,
                               bool want_trig, bool want_spec, bool want_sgram,
                               double rate_hz, double detection_rate_hz,
                               double freq_mhz,
                               Trig& trig, PlotCSpectrumBlock& spec,
                               PlotCSpectrogramBlock& sgram,
                               std::string& err) {
    // Copy out of the blocks first (cheap GUI-thread copy) so no lock is held while writing.
    std::vector<float> trig_y;
    float pre_ms = 0.0f, post_ms = 0.0f;
    size_t trig_idx = 0, trig_rate = 0;
    unsigned long frame = 0;
    bool have_trig = want_trig &&
                     trig.export_frame(trig_y, pre_ms, post_ms, trig_idx, frame,
                                       trig_rate);

    std::vector<float> sp_freq, sp_mag;
    bool have_spec = want_spec && spec.export_spectrum(0, sp_freq, sp_mag);

    std::vector<float> sg;
    size_t sg_rows = 0, sg_cols = 0, sg_fpr = 0, sg_sps = 0;
    bool have_sgram = want_sgram &&
                      sgram.export_display(0, sg, sg_rows, sg_cols, sg_fpr, sg_sps);

    if (!have_trig && !have_spec && !have_sgram) {
        err = "no visible plot has data yet";
        return false;
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open " + path;
        return false;
    }

    char ts[64];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    f << "# spike snapshot " << ts << "\n"
      << "# format: text sections for the plots visible at snapshot time;\n"
      << "#   [trigger]     n/pre_ms/post_ms/frame, then CSV time_ms,power_db (trigger at t=0)\n"
      << "#   [spectrum]    n_fft, then CSV freq_hz,mag_db (baseband)\n"
      << "#   [spectrogram] rows/cols/row_seconds/freq range, then 'BINARY <nbytes>'\n"
      << "#                 + raw little-endian float32 dB, row-major, newest row first,\n"
      << "#                 at the very end of the file\n";
    char line[96];
    std::snprintf(line, sizeof(line), "# sample_rate_hz %.0f\n", rate_hz);
    f << line;
    // Zero-span stream rate (device/decimation); the [trigger] section's own axis uses the capture-time rate.
    std::snprintf(line, sizeof(line), "# detection_rate_hz %.0f\n", detection_rate_hz);
    f << line;
    std::snprintf(line, sizeof(line), "# center_freq_mhz %.6f\n", freq_mhz);
    f << line;

    if (have_trig) {
        // Use the CAPTURED rate (trigger snapshot metadata) -- current device rate may already differ.
        const double dt_ms = 1000.0 / static_cast<double>(trig_rate);
        f << "[trigger]\n"
          << "n " << trig_y.size() << "\n";
        std::snprintf(line, sizeof(line), "pre_ms %.6f\npost_ms %.6f\n",
                      static_cast<double>(pre_ms), static_cast<double>(post_ms));
        f << line;
        f << "frame " << frame << "\n"
          << "# columns: time_ms,power_db\n";
        for (size_t i = 0; i < trig_y.size(); ++i) {
            double tm = (static_cast<double>(i) - static_cast<double>(trig_idx)) * dt_ms;
            std::snprintf(line, sizeof(line), "%.4f,%.3f\n",
                          tm, static_cast<double>(trig_y[i]));
            f << line;
        }
    }

    if (have_spec) {
        f << "[spectrum]\n"
          << "n_fft " << sp_mag.size() << "\n"
          << "# columns: freq_hz,mag_db\n";
        for (size_t i = 0; i < sp_mag.size(); ++i) {
            std::snprintf(line, sizeof(line), "%.1f,%.3f\n",
                          static_cast<double>(sp_freq[i]),
                          static_cast<double>(sp_mag[i]));
            f << line;
        }
    }

    if (have_sgram) {
        const double row_seconds = (sg_sps > 0)
            ? static_cast<double>(sg_fpr) * static_cast<double>(sg_cols)
                  / static_cast<double>(sg_sps)
            : 0.0;
        f << "[spectrogram]\n"
          << "rows " << sg_rows << "\n"
          << "cols " << sg_cols << "\n";
        std::snprintf(line, sizeof(line), "row_seconds %.9f\n", row_seconds);
        f << line;
        std::snprintf(line, sizeof(line), "freq_min_hz %.1f\nfreq_max_hz %.1f\n",
                      -static_cast<double>(sg_sps) / 2.0,
                      static_cast<double>(sg_sps) / 2.0);
        f << line;
        f << "order newest_row_first\n"
          << "encoding binary_float32_le\n"
          << "BINARY " << sg.size() * sizeof(float) << "\n";
        f.write(reinterpret_cast<const char*>(sg.data()),
                static_cast<std::streamsize>(sg.size() * sizeof(float)));
    }

    f.flush();
    if (!f) {
        err = "write to " + path + " failed";
        return false;
    }
    return true;
}

// Builds the source and the flowgraph around it and runs the GUI until the
