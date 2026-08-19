#pragma once

#include "cler.hpp"
#include "desktop_blocks/fm/fm_mpx_decoder.hpp"
#include "fm_radio_blocks.hpp"
#include "fm_radio_source.hpp"
#include "desktop_blocks/gui/cler_palette.hpp"
#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <vector>

// Tuner panel: frequency, seek/scan, gains, signal meters and RDS text. Runs
// on the GUI thread; talks to the other blocks through their thread-safe
// setters/getters only.
struct FmRadioPanel : public cler::BlockBase {
    static constexpr bool is_gui = true;
    static constexpr double BAND_LO = 87.5e6, BAND_HI = 108.0e6, STEP = 100e3;

    struct Station {
        double freq_hz;
        float snr_db;
        char ps[9];
    };

    FmRadioPanel(const char* name, RadioSource& source, FMMpxDecoderBlock& mpx,
                 VolumeBlock& volume, double if_offset_hz, double start_hz, double gain_db)
        : cler::BlockBase(name), _src(source), _mpx(mpx), _vol(volume),
          _if_offset(if_offset_hz), _freq(start_hz), _gain(static_cast<float>(gain_db)) {
#ifdef FM_RADIO_HAVE_HACKRF
        if (auto* h = source.hackrf()) {
            _lna = h->get_lna_gain();
            _vga = h->get_vga_gain();
            _amp = h->get_amp_enable();
        }
#endif
        _volume = volume.volume();
        _deemph_us = 50;
    }

    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Error::NotEnoughSamples; }

    double frequency() const { return _freq; }

    void render() {
        using namespace cler::palette;
        drive_seek_scan();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(400, 0), ImVec2(400, FLT_MAX));
        ImGui::Begin("FM Radio", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        char buf[48];

        // --- tuning ---
        ImGui::PushStyleColor(ImGuiCol_Text, accent_hi);
        ImGui::SetWindowFontScale(2.4f);
        ImGui::Text("%7.2f MHz", _freq / 1e6);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        const float w = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
        if (ImGui::Button("<< seek", ImVec2(w, 0))) start_seek(-1);
        ImGui::SameLine();
        if (ImGui::Button("- 0.1", ImVec2(w, 0))) tune(_freq - STEP);
        ImGui::SameLine();
        if (ImGui::Button("+ 0.1", ImVec2(w, 0))) tune(_freq + STEP);
        ImGui::SameLine();
        if (ImGui::Button("seek >>", ImVec2(w, 0))) start_seek(+1);
        float mhz = static_cast<float>(_freq / 1e6);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##dial", &mhz, 87.5f, 108.0f, "%.1f MHz")) {
            tune(std::round(mhz * 10.0f) / 10.0 * 1e6);
        }

        // --- signal ---
        const float snr = _mpx.pilot_snr_db();
        const bool stereo = _mpx.stereo_locked();
        auto st = _mpx.rds_station();
        ImGui::SeparatorText("Signal");
        lamp("STEREO", stereo);
        ImGui::SameLine();
        lamp("RDS", st.synced);
        ImGui::SameLine();
        lamp("TP", st.tp);
        ImGui::SameLine();
        lamp("TA", st.ta);
        ImGui::SameLine(0, 16);
        std::snprintf(buf, sizeof(buf), "pilot %.1f dB", snr);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, snr > SEEK_SNR_DB ? ok : warn);
        ImGui::ProgressBar(std::clamp(snr / 40.0f, 0.0f, 1.0f), ImVec2(-1, 0), buf);
        ImGui::PopStyleColor();
        bool stereo_on = _mpx.stereo();
        if (ImGui::Checkbox("stereo", &stereo_on)) _mpx.set_stereo(stereo_on);
        ImGui::SameLine(0, 16);
        ImGui::SetNextItemWidth(120);
        const char* deemph[] = {"50 us (EU)", "75 us (US)"};
        int di = _deemph_us == 75 ? 1 : 0;
        if (ImGui::Combo("de-emphasis", &di, deemph, 2)) {
            _deemph_us = di ? 75 : 50;
            _mpx.set_deemphasis_us(_deemph_us);
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##vol", &_volume, 0.0f, 2.0f, "volume %.2f")) _vol.set_volume(_volume);

        // --- RDS ---
        ImGui::SeparatorText("RDS");
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(st.ps[0] ? fg : faint, "%s", st.ps[0] ? st.ps : "--------");
        ImGui::SetWindowFontScale(1.0f);
        if (st.rt[0]) ImGui::TextWrapped("%s", st.rt);
        else ImGui::TextDisabled("%s", st.synced ? "no radiotext yet" : "no RDS");
        ImGui::TextDisabled("PI %04X   PTY %s   groups %u   block errors %.0f%%", st.pi, rds::pty_name(st.pty),
                            st.groups_ok, st.blocks_total ? 100.0 * st.blocks_bad / st.blocks_total : 0.0);

        // --- presets ---
        ImGui::SeparatorText("Stations");
        if (_mode == Mode::Scan) {
            std::snprintf(buf, sizeof(buf), "scanning %.1f MHz", _scan_freq / 1e6);
            if (ImGui::Button("stop")) stop_auto();
            ImGui::SameLine();
            ImGui::ProgressBar(static_cast<float>((_scan_freq - BAND_LO) / (BAND_HI - BAND_LO)), ImVec2(-1, 0), buf);
        } else if (ImGui::Button("scan band", ImVec2(-1, 0))) {
            start_scan();
        }
        const float rows = static_cast<float>(std::max<size_t>(1, std::min<size_t>(_stations.size(), 8)));
        if (ImGui::BeginListBox("##stations", ImVec2(-1, rows * ImGui::GetTextLineHeightWithSpacing() + 8))) {
            if (_stations.empty() && _mode != Mode::Scan) ImGui::TextDisabled("no stations yet - press 'scan band'");
            for (size_t i = 0; i < _stations.size(); ++i) {
                auto& s = _stations[i];
                if (s.ps[0] == 0 && std::fabs(s.freq_hz - _freq) < 1.0 && st.ps[0]) std::memcpy(s.ps, st.ps, sizeof(s.ps));
                std::snprintf(buf, sizeof(buf), "%6.1f MHz   %5.1f dB   %s", s.freq_hz / 1e6, s.snr_db, s.ps);
                if (ImGui::Selectable(buf, std::fabs(s.freq_hz - _freq) < 1.0)) tune(s.freq_hz);
            }
            ImGui::EndListBox();
        }

        // --- RF ---
        ImGui::SeparatorText(_src.kind_name());
#ifdef FM_RADIO_HAVE_HACKRF
        if (auto* h = _src.hackrf()) {
            if (ImGui::SliderInt("LNA", &_lna, 0, 40, "%d dB")) { _lna = (_lna / 8) * 8; h->set_lna_gain(_lna); }
            if (ImGui::SliderInt("VGA", &_vga, 0, 62, "%d dB")) { _vga = (_vga / 2) * 2; h->set_vga_gain(_vga); }
            if (ImGui::Checkbox("RF amp", &_amp)) h->set_amp_enable(_amp);
            ImGui::SameLine(0, 16);
            ImGui::TextDisabled("overflows %zu", _src.overflow_count());
        }
#endif
        if (_src.has_gain()) {
            if (ImGui::SliderFloat("gain", &_gain, 0.0f, 70.0f, "%.0f dB")) _src.set_gain(_gain);
        }
        ImGui::End();
    }

private:
    enum class Mode { Idle, Seek, Scan };

    static void lamp(const char* label, bool on) {
        using namespace cler::palette;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ok : bg2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ok : bg2);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? ok : bg2);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? fg : faint);
        ImGui::Button(label);
        ImGui::PopStyleColor(4);
    }

    void tune(double hz) {
        hz = std::clamp(hz, BAND_LO, BAND_HI);
        if (hz == _freq) return;
        _freq = hz;
        _src.set_frequency(_freq + _if_offset);
        _mpx.rds_reset();
        _tuned_at = std::chrono::steady_clock::now();
    }

    void start_seek(int dir) { _mode = Mode::Seek; _dir = dir; tune(step(_freq, dir)); }
    void start_scan() { _mode = Mode::Scan; _stations.clear(); _scan_freq = BAND_LO; _scan_snr.clear(); tune(BAND_LO); }
    void stop_auto() { _mode = Mode::Idle; }

    static double step(double hz, int dir) {
        hz += dir * STEP;
        if (hz > BAND_HI + 1.0) hz = BAND_LO;
        if (hz < BAND_LO - 1.0) hz = BAND_HI;
        return hz;
    }

    void drive_seek_scan() {
        if (_mode == Mode::Idle) return;
        const auto dwell = std::chrono::milliseconds(_mode == Mode::Seek ? 250 : 200);
        if (std::chrono::steady_clock::now() - _tuned_at < dwell) return;
        const float snr = _mpx.pilot_snr_db();
        if (_mode == Mode::Seek) {
            if (snr > SEEK_SNR_DB || std::fabs(_freq - _seek_origin) < 1.0) { _mode = Mode::Idle; return; }
            tune(step(_freq, _dir));
            return;
        }
        _scan_snr.push_back(snr);
        if (_scan_freq + STEP > BAND_HI + 1.0) {
            finish_scan();
            return;
        }
        _scan_freq += STEP;
        tune(_scan_freq);
    }

    void finish_scan() {
        _mode = Mode::Idle;
        // local maxima above threshold, one per station
        for (size_t i = 0; i < _scan_snr.size(); ++i) {
            const float s = _scan_snr[i];
            if (s < SEEK_SNR_DB) continue;
            const float l = i ? _scan_snr[i - 1] : -99.0f;
            const float r = i + 1 < _scan_snr.size() ? _scan_snr[i + 1] : -99.0f;
            if (s >= l && s > r) {
                Station st{BAND_LO + i * STEP, s, {}};
                _stations.push_back(st);
            }
        }
        std::sort(_stations.begin(), _stations.end(), [](const Station& a, const Station& b) { return a.snr_db > b.snr_db; });
        if (!_stations.empty()) tune(_stations.front().freq_hz);
    }

    static constexpr float SEEK_SNR_DB = 12.0f;

    RadioSource& _src;
    FMMpxDecoderBlock& _mpx;
    VolumeBlock& _vol;
    double _if_offset;
    double _freq;
    int _lna = 0, _vga = 0;
    bool _amp = false;
    float _gain = 30.0f;
    float _volume = 1.0f;
    int _deemph_us = 50;
    Mode _mode = Mode::Idle;
    int _dir = 1;
    double _seek_origin = 0.0;
    double _scan_freq = BAND_LO;
    std::vector<float> _scan_snr;
    std::vector<Station> _stations;
    std::chrono::steady_clock::time_point _tuned_at = std::chrono::steady_clock::now();
};
