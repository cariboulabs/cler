#pragma once

#include "cler.hpp"
#include "desktop_blocks/demod/analog_demod.hpp"
#include "desktop_blocks/gui/cler_palette.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/sigmf/recorder_sigmf.hpp"
#include "desktop_blocks/sources/source_hackrf.hpp"
#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>

// Tuner panel: hardware centre, click-tuned offset, mode, gains, bookmarks.
// The tuned frequency = centre + offset; a double-click on the spectrum sets
// the offset, retuning the hardware only when the click lands within the
// demodulator's channel of the band edge.
struct ScannerPanel : public cler::BlockBase {
    static constexpr bool is_gui = true;

    ScannerPanel(const char* name, SourceHackRFBlock& source, FrequencyShiftBlock& shift,
                 AnalogDemodBlock& demod, PlotCSpectrumBlock& spectrum,
                 SigMFRecorderBlock& recorder, double center_hz, double rate_hz)
        : cler::BlockBase(name), _src(source), _shift(shift), _demod(demod), _spectrum(spectrum),
          _recorder(recorder), _center(center_hz), _rate(rate_hz) {
        _lna = source.get_lna_gain();
        _vga = source.get_vga_gain();
        _amp = source.get_amp_enable();
    }

    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Error::NotEnoughSamples; }

    void render() {
        using namespace cler::palette;

        double click;
        if (_spectrum.take_click(click)) tune_offset(click);

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(320, 0), ImVec2(320, FLT_MAX));
        ImGui::Begin("Scanner", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        char buf[64];

        // --- tuned frequency ---
        ImGui::PushStyleColor(ImGuiCol_Text, accent_hi);
        ImGui::SetWindowFontScale(2.0f);
        ImGui::Text("%10.4f MHz", (_center + _offset) / 1e6);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("centre %.3f MHz  offset %+0.f kHz", _center / 1e6, _offset / 1e3);
        const float w = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
        if (ImGui::Button("-1M", ImVec2(w, 0))) retune(_center - 1e6);
        ImGui::SameLine();
        if (ImGui::Button("-0.1", ImVec2(w, 0))) retune(_center - 100e3);
        ImGui::SameLine();
        if (ImGui::Button("+0.1", ImVec2(w, 0))) retune(_center + 100e3);
        ImGui::SameLine();
        if (ImGui::Button("+1M", ImVec2(w, 0))) retune(_center + 1e6);
        float mhz = static_cast<float>(_center / 1e6);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##centre", &mhz, 24.0f, 1700.0f, "centre %.1f MHz", ImGuiSliderFlags_Logarithmic)) {
            retune(static_cast<double>(mhz) * 1e6);
        }

        // --- demod ---
        ImGui::SeparatorText("Demodulator");
        static const AnalogDemodBlock::Mode modes[] = {
            AnalogDemodBlock::Mode::WBFM, AnalogDemodBlock::Mode::NBFM,
            AnalogDemodBlock::Mode::AM, AnalogDemodBlock::Mode::USB, AnalogDemodBlock::Mode::LSB};
        const AnalogDemodBlock::Mode cur = _demod.mode();
        for (size_t i = 0; i < 5; ++i) {
            if (i) ImGui::SameLine();
            const bool on = modes[i] == cur;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, accent);
            if (ImGui::Button(AnalogDemodBlock::mode_name(modes[i]))) _demod.set_mode(modes[i]);
            if (on) ImGui::PopStyleColor();
        }

        // --- bookmarks ---
        ImGui::SeparatorText("Bookmarks");
        if (ImGui::Button("add current", ImVec2(-1, 0))) _bookmarks.push_back(_center + _offset);
        const float rows = static_cast<float>(std::max<size_t>(1, std::min<size_t>(_bookmarks.size(), 6)));
        if (ImGui::BeginListBox("##bookmarks", ImVec2(-1, rows * ImGui::GetTextLineHeightWithSpacing() + 8))) {
            if (_bookmarks.empty()) ImGui::TextDisabled("none yet - tune and 'add current'");
            for (size_t i = 0; i < _bookmarks.size(); ++i) {
                std::snprintf(buf, sizeof(buf), "%10.4f MHz##%zu", _bookmarks[i] / 1e6, i);
                if (ImGui::Selectable(buf, std::fabs(_bookmarks[i] - _center - _offset) < 1.0)) {
                    retune_to(_bookmarks[i]);
                }
            }
            ImGui::EndListBox();
        }

        // --- recording ---
        ImGui::SeparatorText("Record (SigMF, full band)");
        if (_recorder.recording()) {
            std::snprintf(buf, sizeof(buf), "stop  %.1f s  %s", _recorder.samples() / _rate, _recorder.base().c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, danger);
            if (ImGui::Button(buf, ImVec2(-1, 0))) _recorder.stop();
            ImGui::PopStyleColor();
        } else if (ImGui::Button("record", ImVec2(-1, 0))) {
            _recorder.start("scanner", _center);
        }

        // --- RF ---
        ImGui::SeparatorText("HackRF");
        if (ImGui::SliderInt("LNA", &_lna, 0, 40, "%d dB")) { _lna = (_lna / 8) * 8; _src.set_lna_gain(_lna); }
        if (ImGui::SliderInt("VGA", &_vga, 0, 62, "%d dB")) { _vga = (_vga / 2) * 2; _src.set_vga_gain(_vga); }
        if (ImGui::Checkbox("RF amp", &_amp)) _src.set_amp_enable(_amp);
        ImGui::SameLine(0, 16);
        ImGui::TextDisabled("overflows %zu", _src.get_overflow_count());
        ImGui::End();
    }

private:
    static constexpr double CHANNEL_BW = 240e3;

    // click at baseband `hz`: tune the demod there, recentring the hardware
    // only when the channel would clip the band edge
    void tune_offset(double hz) {
        if (std::fabs(hz) + CHANNEL_BW / 2.0 > _rate / 2.0) {
            retune_to(_center + hz);
            return;
        }
        _offset = hz;
        _shift.set_frequency_shift(-_offset);
    }

    void retune(double center_hz) {
        _center = std::clamp(center_hz, 1e6, 6e9);
        _src.set_frequency(static_cast<uint64_t>(_center + 0.5));
    }

    void retune_to(double tuned_hz) {
        retune(tuned_hz - _offset);
        const double back = tuned_hz - _center;
        if (std::fabs(back) + CHANNEL_BW / 2.0 <= _rate / 2.0) {
            _offset = back;
            _shift.set_frequency_shift(-_offset);
        }
    }

    SourceHackRFBlock& _src;
    FrequencyShiftBlock& _shift;

    AnalogDemodBlock& _demod;
    PlotCSpectrumBlock& _spectrum;
    SigMFRecorderBlock& _recorder;
    double _center, _rate, _offset = 0.0;
    int _lna = 0, _vga = 0;
    bool _amp = false;
    std::vector<double> _bookmarks;
};
