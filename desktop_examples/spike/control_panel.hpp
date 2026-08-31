#pragma once

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/triggers/trigger_block.hpp"
#include "../power_detector.hpp"
#include "channelizer_panel.hpp"
#include "spike_source.hpp"
#include "spike_args.hpp"

using Trig = TriggerBlock<float>;
using PowerDet = PowerDetectorBlock<std::complex<float>>;

static void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Render-only: writes staged source config and mutex-guarded trigger config; owns neither.
struct ControlPanel : public cler::BlockBase {
    static constexpr bool is_gui = true;

    ControlPanel(const char* name, ISource* src, Trig* trig,
                 PlotCSpectrumBlock* spectrum,
                 PlotCSpectrogramBlock* sgram,
                 PowerDetectorBlock<std::complex<float>>* power,
                 ChannelizerPanelBlock* chan,
                 size_t sgram_rows, const SpikeArgs& a)
        : BlockBase(name),
          _src(src), _trig(trig), _spectrum(spectrum), _sgram(sgram), _power(power),
          _chan(chan),
          _kind(src->kind()),
          _freq_ui_min_mhz(a.limits.known ? static_cast<float>(a.limits.fmin / 1e6) : 0.0f),
          _freq_ui_max_mhz(a.limits.known ? static_cast<float>(a.limits.fmax / 1e6)
                                          : (src->kind() == SourceKind::HackRF ? 7250.0f : 6000.0f)),
          _rate_ui_min_msps(a.limits.known ? static_cast<float>(a.limits.rmin / 1e6)
                                           : (src->kind() == SourceKind::HackRF ? 2.0f : 0.1f)),
          _rate_ui_max_msps(a.limits.known ? static_cast<float>(a.limits.rmax / 1e6)
                                           : (src->kind() == SourceKind::HackRF ? 20.0f : 61.44f)),
          _freq_mhz(static_cast<float>(a.freq / 1e6)),
          _freq_anchor_mhz(_freq_mhz),
          _gain_db(static_cast<float>(a.gain)),
          _lna_db(a.lna),
          _vga_db(a.vga),
          _amp_enable(a.amp),
          _rate_hz(a.rate),
          _rate_msps(static_cast<float>(a.rate / 1e6)),
          _rate_from_cli(a.rate_from_cli),
          _n_fft(a.fft),
          _sgram_rows(sgram_rows),
          _zs_bw_mhz(static_cast<float>(a.rate / 1e6)),   // full rate = bypass
          _max_window_ms(trig->max_window_ms()) {
        _history_s = fpr_to_history(8);   // default depth; conf key history_s overrides
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Error::NotEnoughSamples;
    }

    // Slider/drag that becomes a typed input on double-click; [tmin,tmax] (default vmin/vmax) bounds the typed value.
    bool editable(const char* label, float* v, float vmin, float vmax,
                  const char* fmt, bool use_slider, float drag_speed = 1.0f,
                  float tmin = NAN, float tmax = NAN) {
        if (std::isnan(tmin)) tmin = vmin;
        if (std::isnan(tmax)) tmax = vmax;
        bool changed = false;
        if (_editing == label) {
            if (_editing_start) { ImGui::SetKeyboardFocusHere(); _editing_start = false; }
            changed = ImGui::InputFloat(label, v, 0.0f, 0.0f, fmt,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemDeactivated()) {        // Enter or clicked away: commit
                *v = std::min(std::max(*v, tmin), tmax);
                _editing = nullptr;
                changed = true;
            }
        } else {
            if (use_slider)
                changed = ImGui::SliderFloat(label, v, vmin, vmax, fmt, ImGuiSliderFlags_AlwaysClamp);
            else
                changed = ImGui::DragFloat(label, v, drag_speed, vmin, vmax, fmt, ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                _editing = label;        // string literals have stable addresses
                _editing_start = true;
            }
        }
        return changed;
    }

    // Read-only display; double-click opens a typed input clamped to [vmin, vmax].
    bool grid_field(const char* label, float* v, const char* fmt,
                    float vmin, float vmax) {
        bool changed = false;
        if (_editing == label) {
            if (_editing_start) { ImGui::SetKeyboardFocusHere(); _editing_start = false; }
            ImGui::SetNextItemWidth(120);
            ImGui::InputFloat(label, v, 0.0f, 0.0f, fmt,
                              ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemDeactivated()) {        // Enter or clicked away: commit
                *v = std::min(std::max(*v, vmin), vmax);
                _editing = nullptr;
                changed = true;
            }
        } else {
            // Button/InputFloat need DIFFERENT ids ("_btn" suffix) or the button's active state deactivates the input.
            char btn[96];
            int n = std::snprintf(btn, sizeof(btn), fmt, static_cast<double>(*v));
            if (n < 0) n = 0;
            if (n > static_cast<int>(sizeof(btn)) - 1) n = sizeof(btn) - 1;
            std::snprintf(btn + n, sizeof(btn) - static_cast<size_t>(n),
                          "###%s_btn", label);
            ImGui::Button(btn, ImVec2(120, 0));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                _editing = label;        // string literals have stable addresses
                _editing_start = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
        }
        return changed;
    }

    void render() {
        // Polled every frame: the source snaps the requested rate async, so re-sync everything once it lands.
        {
            const double actual = _src->actual_sample_rate();
            if (std::abs(actual - _rate_hz) > 0.5) on_rate_changed(actual);
        }

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        // Tiling code below reads the ACTUAL rect (window_pos/window_size), not a fixed strip.
        ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 0.0f),
                                            ImVec2(420.0f, FLT_MAX));
        ImGui::Begin("Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::TextUnformatted("View");
        ImGui::Separator();
        ImGui::Checkbox("Zero-span scope", &show_scope);
        ImGui::SameLine();
        ImGui::Checkbox("Spectrum", &show_spectrum);
        ImGui::Checkbox("Spectrogram (waterfall)", &show_spectrogram);
        ImGui::SameLine();
        ImGui::Checkbox("Channelizer", &show_channelizer);
        help("Which windows to display. All views run continuously; these only "
             "toggle visibility, so nothing needs restarting.");

        if (ImGui::Button("Arrange windows")) arrange_requested = true;
        help("Re-tile the visible plot windows into equal-height rows next to "
             "the control panel. Also happens automatically when the View "
             "checkboxes change or the main window is resized. You can still "
             "move/resize windows afterward.");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Snapshot");
        ImGui::Separator();
        if (ImGui::Button("Snapshot")) {
            if (_snapshot_dir.empty()) {
                const char* home = std::getenv("HOME");
                std::snprintf(_snapdir_edit, sizeof(_snapdir_edit), "%s",
                              home ? home : ".");
                ImGui::OpenPopup("Snapshot directory");
            } else {
                snapshot_requested = true;
            }
        }
        help("Save a screenshot of the whole window (.png) plus the data behind "
             "the currently visible plots (.dat, self-describing text with a "
             "binary spectrogram blob) into the snapshot directory. The "
             "directory is asked for once and remembered in the conf file.");
        if (ImGui::BeginPopupModal("Snapshot directory", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Directory to save snapshots into:");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("##snapdir", _snapdir_edit, sizeof(_snapdir_edit));
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                _snapshot_dir = trimmed(_snapdir_edit);
                if (!_snapshot_dir.empty()) snapshot_requested = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (!_status.empty() &&
            std::chrono::steady_clock::now() < _status_until) {
            ImGui::TextWrapped("%s", _status.c_str());
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Radio");
        ImGui::Separator();

        // Rate is only REQUESTED here; the poll above re-syncs this widget once
        // it lands. Both sources support live rate change (HackRF via a brief
        // stream stop/restart); the range differs per device.
        ImGui::SetNextItemWidth(180);
        editable("Rate (MS/s)", &_rate_msps, _rate_ui_min_msps, _rate_ui_max_msps,
                 "%.3f", /*slider=*/false, /*drag_speed=*/0.01f);
        if (ImGui::IsItemDeactivatedAfterEdit())
            push_rate_config(static_cast<double>(_rate_msps) * 1e6);
        help("Sample rate = displayed span, applied live. The hardware may "
             "snap the request to what its clocking supports; the shown value "
             "follows the ACTUAL device rate. On HackRF the RX stream is briefly "
             "stopped and restarted. A capture straddling the switch may be "
             "garbled once, and the waterfall restarts.");

        // Apply only on edit-end (avoid spamming retunes); anchor recenters on commit only, else mid-drag moves the mapping under the cursor.
        const float freq_lo = std::max(_freq_ui_min_mhz, _freq_anchor_mhz - 25.0f);
        const float freq_hi = std::min(_freq_ui_max_mhz, _freq_anchor_mhz + 25.0f);
        ImGui::SetNextItemWidth(180);
        editable("Center (MHz)", &_freq_mhz, freq_lo, freq_hi, "%.3f",
                 /*slider=*/true, 0.1f, /*tmin=*/_freq_ui_min_mhz, /*tmax=*/_freq_ui_max_mhz);
        bool freq_done = ImGui::IsItemDeactivatedAfterEdit();
        if (freq_done) {
            _freq_anchor_mhz = _freq_mhz;   // recenter for next time
            _chan->set_center_freq(static_cast<double>(_freq_mhz) * 1e6);
        }

        // Gain: UHD is a single dB knob; HackRF splits into LNA/VGA/amp stages.
        bool gain_done = false;
        if (_kind == SourceKind::UHD) {
            ImGui::SetNextItemWidth(180);
            editable("Gain (dB)", &_gain_db, 0.0f, 76.0f, "%.1f", /*slider=*/true);
            gain_done = ImGui::IsItemDeactivatedAfterEdit();
        } else {
            ImGui::SetNextItemWidth(180);
            ImGui::SliderInt("LNA (dB)", &_lna_db, 0, 40, "%d");
            gain_done |= ImGui::IsItemDeactivatedAfterEdit();
            help("HackRF front-end LNA gain. Applied in 8 dB steps (0-40).");
            ImGui::SetNextItemWidth(180);
            ImGui::SliderInt("VGA (dB)", &_vga_db, 0, 62, "%d");
            gain_done |= ImGui::IsItemDeactivatedAfterEdit();
            help("HackRF baseband VGA gain. Applied in 2 dB steps (0-62).");
            if (ImGui::Checkbox("RX amp (+14 dB)", &_amp_enable)) gain_done = true;
            help("HackRF external RX amplifier: adds ~14 dB but can overload on "
                 "strong signals.");
        }
        if (freq_done || gain_done) push_radio_config();

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Trigger");
        ImGui::Separator();

        bool changed = false;
        changed |= editable("Level (dB)", &_threshold_db, -120.0f, 0.0f, "%.1f", true);
        help("Power level the signal must cross to fire a trigger.");
        changed |= editable("Hysteresis (dB)", &_hysteresis_db, 0.0f, 30.0f, "%.1f", true);
        help("Dead-band. After a rising trigger fires, the signal must drop below "
             "(Level - Hysteresis) before it can fire again. Stops chatter when the "
             "signal hovers right at the Level.");
        ImGui::SetNextItemWidth(150);
        changed |= editable("Window (ms)", &_window_ms, 1.0f, _max_window_ms, "%.1f", true);
        ImGui::SameLine();
        // Capture memory is fixed at startup, so this shrinks at higher rates (refreshed by on_rate_changed()).
        ImGui::TextDisabled("max %.0f", _max_window_ms);
        help("Total time span captured and displayed per trigger (pre + post). "
             "This is the scope timebase. To see a whole repeating burst locked in "
             "place, set Window a bit LESS than the burst period and >= the burst "
             "span. 'max' is the largest window at the CURRENT sample rate: the "
             "capture buffer is sized once at startup, so raising the rate "
             "shrinks the reachable window (in ms).");
        changed |= editable("Pre-trigger (%)", &_pretrigger_pct, 0.0f, 90.0f, "%.0f", true);
        help("How much of the Window is shown BEFORE the trigger instant (t=0). "
             "e.g. 10% puts the trigger 10% in from the left.");
        changed |= editable("Holdoff (ms)", &_holdoff_ms, 0.0f, 5000.0f, "%.0f", true);
        help("Minimum time AFTER a capture finishes before another trigger can fire "
             "(added on top of the Window time, not overlapping). To lock a repeating "
             "burst, set Window < burst period, then keep Holdoff small.");
        changed |= editable("Auto timeout (ms)", &_auto_ms, 10.0f, 2000.0f, "%.0f", true);
        help("Auto mode only: if no edge arrives within this time, fire anyway so the "
             "display keeps refreshing. Ignored in Normal/Single.");

        const char* edges[] = {"Rising", "Falling"};
        changed |= ImGui::Combo("Edge", &_edge_idx, edges, 2);
        help("Rising: fire when power crosses Level upward. Falling: downward.");
        const char* modes[] = {"Normal", "Single", "Auto"};
        changed |= ImGui::Combo("Mode", &_mode_idx, modes, 3);
        help("Normal: capture on every trigger. Single: capture one then stop "
             "(press Arm to go again). Auto: like Normal but free-runs if idle.");

        if (changed) push_trigger_config();

        // Push decimator config before trigger: trigger runs at the DETECTION rate (decimated when filter on, device rate off).
        const bool filter_toggled = ImGui::Checkbox("Filter", &_zs_filter_on);
        help("Enable the zero-span channel-selection filter. Off (default): "
             "the power trace sees the raw full-rate I/Q stream, exactly as "
             "without a filter. On: the stream is decimated to the bandwidth "
             "below before the power is computed.");
        ImGui::BeginDisabled(!_zs_filter_on);
        ImGui::SetNextItemWidth(180);
        editable("Zero-span BW (MHz)", &_zs_bw_mhz, min_zs_bw_mhz(),
                 static_cast<float>(_rate_hz / 1e6), "%.4f",
                 /*slider=*/false, /*drag_speed=*/0.01f);
        const bool bw_committed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (!_zs_filter_on) {
            ImGui::TextDisabled("(off)");
        } else {
            const size_t R = PowerDet::decimation_for(zs_bw_hz(), _rate_hz);
            if (R <= 1) {
                ImGui::TextDisabled("(bypass)");
            } else {
                const double eff = _rate_hz / static_cast<double>(R);
                if (eff < 1e6) ImGui::TextDisabled("eff. %.1f kHz", eff / 1e3);
                else           ImGui::TextDisabled("eff. %.3f MHz", eff / 1e6);
            }
        }
        ImGui::EndDisabled();
        if (filter_toggled || bw_committed) {
            clamp_zs_bw();
            push_power_config();
            refresh_trigger_window();
            push_trigger_config();
        }
        help("Channel (detection) bandwidth of the zero-span power trace: the "
             "I/Q stream is decimated by an integer factor R with an 80 dB "
             "anti-alias lowpass, so exactly this two-sided width around the "
             "center survives and off-channel bursts are rejected before the "
             "power is computed. The achieved value snaps to sample_rate/R -- "
             "'eff.' shows what is actually applied. The trigger then runs at "
             "the decimated rate, so narrower bandwidths allow LONGER capture "
             "windows. A capture straddling a toggle or bandwidth change may "
             "be garbled once.");

        ImGui::Dummy(ImVec2(0, 6));
        if (ImGui::Button("Arm / Re-arm")) _trig->rearm();
        ImGui::SameLine();
        if (ImGui::Button("Force"))        _trig->force_trigger();

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Text("State: %s", state_str(_trig->state()));

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Channelizer");
        ImGui::Separator();

        // Snaps to N = rate/width integer channels; effective width/N mirrored beside the control.
        ImGui::SetNextItemWidth(180);
        editable("Channel width (MHz)", &_chan_width_mhz, min_chan_width_mhz(),
                 static_cast<float>(_rate_hz / 2e6), "%.3f",
                 /*slider=*/false, /*drag_speed=*/0.01f);
        const bool chan_width_committed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        {
            const size_t n = ChannelizerPanelBlock::channels_for(
                chan_width_hz(), _rate_hz);
            const double eff = ChannelizerPanelBlock::effective_width(
                chan_width_hz(), _rate_hz);
            ImGui::TextDisabled("N=%zu eff. %.3f MHz", n, eff / 1e6);
        }
        help("Width of each channelizer strip. The device rate is split into "
             "N = rate/width equal channels (N snaps to an integer in [2, 64]; "
             "'eff.' shows the width actually applied). Each strip is a "
             "scrolling peak-power-vs-time trace of one channel; strips are "
             "labeled with their absolute center frequency. A width or rate "
             "change restarts the strip history.");
        ImGui::SetNextItemWidth(180);
        editable("Span (s)", &_chan_span_s, 1.0f, 600.0f, "%.1f",
                 /*slider=*/false, /*drag_speed=*/0.1f);
        const bool chan_span_committed = ImGui::IsItemDeactivatedAfterEdit();
        help("Time depth of the channelizer strips (shared X axis). Longer "
             "spans peak-hold more samples into each on-screen point.");
        if (chan_width_committed || chan_span_committed) {
            clamp_chan_width();
            push_chan_config();
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextUnformatted("Spectrogram grid");
        ImGui::Separator();
        // Grid on/off toggle lives in the Spectrogram window; here only sets line spacing.
        if (grid_field("Grid time (ms)", &_grid_time_ms, "%.1f",
                       0.1f, 100000.0f))
            _sgram->set_grid_time_ms(_grid_time_ms);
        help("Spacing between horizontal (time) grid lines on the waterfall, in "
             "milliseconds. Double-click the field to type a value. Turn the "
             "grid on/off with the 'Grid' checkbox in the Spectrogram window.");
        if (grid_field("Grid freq (MHz)", &_grid_freq_mhz, "%.4f",
                       0.0001f, 100000.0f))
            _sgram->set_grid_freq_mhz(_grid_freq_mhz);
        help("Spacing between vertical (frequency) grid lines on the waterfall, "
             "in MHz, anchored at the band center (DC). Double-click to type a "
             "value.");

        // window_pos/size feed the main loop's tiling (auto-sized, so not constant).
        window_pos  = ImGui::GetWindowPos();
        window_size = ImGui::GetWindowSize();

        ImGui::End();

        // Hidden plots keep draining input (fanout never stalls) but skip FFT/copy work, so they can't jitter the trigger path.
        _sgram->set_active(show_spectrogram);
        _spectrum->set_active(show_spectrum);
        _chan->set_active(show_channelizer);
        _trig->set_visible(show_scope);
        _spectrum->set_visible(show_spectrum);
        _sgram->set_visible(show_spectrogram);
        _chan->set_visible(show_channelizer);

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (vp->WorkSize.x != _pending_work_size.x ||
            vp->WorkSize.y != _pending_work_size.y) {
            _pending_work_size = vp->WorkSize;
            _work_size_stable  = 0;
        } else if (_work_size_stable < kResizeSettleFrames) {
            ++_work_size_stable;
        }
        const bool resize_retile =
            _work_size_stable >= kResizeSettleFrames &&
            (vp->WorkSize.x != _tiled_work_size.x ||
             vp->WorkSize.y != _tiled_work_size.y);

        // Read visibility AFTER render() (toggle applies this frame) but stage rects BEFORE the view renders below consume them.
        bool retile = _first_layout || arrange_requested || resize_retile ||
                      show_scope       != _prev_scope ||
                      show_spectrum    != _prev_spectrum ||
                      show_spectrogram != _prev_spectrogram ||
                      show_channelizer != _prev_channelizer;
        _first_layout = false;
        arrange_requested = false;
        _prev_scope       = show_scope;
        _prev_spectrum    = show_spectrum;
        _prev_spectrogram = show_spectrogram;
        _prev_channelizer = show_channelizer;

        if (retile) {
            // Plot area = viewport minus the panel's rect and margins, split into N equal rows.
            _tiled_work_size = vp->WorkSize;
            const float gap = 8.0f;
            const float x = window_pos.x + window_size.x + 10.0f;
            const float w = vp->WorkPos.x + vp->WorkSize.x - 10.0f - x;
            const float y0 = vp->WorkPos.y + 10.0f;
            const float total_h = vp->WorkSize.y - 20.0f;
            const int n = (show_scope ? 1 : 0) +
                          (show_spectrum ? 1 : 0) +
                          (show_spectrogram ? 1 : 0) +
                          (show_channelizer ? 1 : 0);
            if (n > 0 && w > 50.0f && total_h > 50.0f) {
                const float row_h = (total_h - gap * static_cast<float>(n - 1))
                                    / static_cast<float>(n);
                int row = 0;
                auto row_y = [&](int r) { return y0 + static_cast<float>(r) * (row_h + gap); };
                if (show_scope)       _trig->apply_window_rect(x, row_y(row++), w, row_h);
                if (show_spectrum)    _spectrum->apply_window_rect(x, row_y(row++), w, row_h);
                if (show_spectrogram) _sgram->apply_window_rect(x, row_y(row++), w, row_h);
                if (show_channelizer) _chan->apply_window_rect(x, row_y(row++), w, row_h);
            }
        }
    }

    // Transient status line under the Snapshot button (auto-hides).
    void set_status(const std::string& s) {
        _status = s;
        _status_until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    }

    const std::string& snapshot_dir() const { return _snapshot_dir; }
    // Override the loaded/asked-for directory (--capture DIR). Call after
    // load(); capture runs skip save(), so this never lands in the conf file.
    void set_snapshot_dir(const std::string& dir) { _snapshot_dir = dir; }
    float  freq_mhz() const { return _freq_mhz; }
    double rate_hz()  const { return _rate_hz; }

    // Rate the TRIGGER's power stream runs at (device rate / decimation when the filter is on); all trigger ms<->samples math must use this.
    size_t detection_rate() const {
        const size_t R = _zs_filter_on
                       ? PowerDet::decimation_for(zs_bw_hz(), _rate_hz)
                       : 1;
        double det = std::round(_rate_hz / static_cast<double>(R));
        if (det < 1.0) det = 1.0;
        return static_cast<size_t>(det);
    }

    // Push loaded/initial settings to radio, trigger, and spectrogram (startup).
    void apply_all() {
        push_radio_config();
        // Decimator must land before trigger config: trigger's time base is the DECIMATED rate.
        clamp_zs_bw();
        push_power_config();
        refresh_trigger_window();
        push_trigger_config();
        clamp_chan_width();
        push_chan_config();
        _sgram->set_frames_per_row(history_to_fpr(_history_s));
        _sgram->set_show_grid(_show_grid);
        _sgram->set_grid_time_ms(_grid_time_ms);
        _sgram->set_grid_freq_mhz(_grid_freq_mhz);
        // Saved rate staged LAST via the live path so on_rate_changed() fires once it lands; explicit CLI -r wins over it.
        if (!_rate_from_cli && _loaded_rate_hz > 0.0 &&
            std::abs(_loaded_rate_hz - _rate_hz) > 0.5) {
            push_rate_config(_loaded_rate_hz);
            // Seed above used the CONSTRUCTION rate; mark stale so on_rate_changed() redoes it.
            _history_seed_pending = true;
        }
    }

    // Which windows to draw; public so main()'s render loop can gate on them.
    bool show_scope       = true;
    bool show_spectrum    = true;
    bool show_spectrogram = false;
    bool show_channelizer = false;

    bool arrange_requested = false;   // one-shot from "Arrange windows"; consumed by main()'s tiling code
    bool snapshot_requested = false;  // one-shot from "Snapshot"; consumed by main() (has block/GuiManager scope)

    // Actual panel rect from the last render() (auto-sized width); main() tiles plots right of it, using defaults until then.
    ImVec2 window_pos  = ImVec2(10.0f, 10.0f);
    ImVec2 window_size = ImVec2(360.0f, 520.0f);

    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string key;
        while (f >> key) {
            if      (key == "freq_mhz")       f >> _freq_mhz;
            else if (key == "gain_db")        f >> _gain_db;
            else if (key == "lna_gain_db")    f >> _lna_db;
            else if (key == "vga_gain_db")    f >> _vga_db;
            else if (key == "amp_enable")     f >> _amp_enable;
            else if (key == "threshold_db")   f >> _threshold_db;
            else if (key == "hysteresis_db")  f >> _hysteresis_db;
            else if (key == "window_ms")      f >> _window_ms;
            else if (key == "pretrigger_pct") f >> _pretrigger_pct;
            else if (key == "holdoff_ms")     f >> _holdoff_ms;
            else if (key == "auto_ms")        f >> _auto_ms;
            else if (key == "edge")           f >> _edge_idx;
            else if (key == "mode")           f >> _mode_idx;
            else if (key == "show_scope")     f >> show_scope;
            else if (key == "show_spectrum")  f >> show_spectrum;
            else if (key == "show_spectrogram") f >> show_spectrogram;
            else if (key == "show_channelizer") f >> show_channelizer;
            else if (key == "chan_width_mhz")   f >> _chan_width_mhz;
            else if (key == "chan_span_s")      f >> _chan_span_s;
            else if (key == "history_s")      f >> _history_s;
            else if (key == "grid_on")        f >> _show_grid;
            else if (key == "grid_time_ms")   f >> _grid_time_ms;
            else if (key == "grid_freq_mhz")  f >> _grid_freq_mhz;
            else if (key == "zs_bw_hz") {
                // Stored in Hz (widget shows MHz); key alone doesn't enable filtering (zs_filter_on does).
                float bw_hz = 0.0f;
                f >> bw_hz;
                _zs_bw_mhz = bw_hz / 1e6f;
            }
            else if (key == "zs_filter_on")   f >> _zs_filter_on;
            else if (key == "rate_hz")        f >> _loaded_rate_hz;
            else if (key == "snapshot_dir") {
                // Rest-of-line value (may contain spaces), unlike other whitespace-delimited keys.
                std::string rest;
                std::getline(f, rest);
                _snapshot_dir = trimmed(rest.c_str());
            }
            else { std::string skip; std::getline(f, skip); }
        }
        // Window is NOT clamped here: its limit depends on the DECIMATED rate, only known once bandwidth applies (clamped in apply_all()/on_rate_changed()).
        clamp_zs_bw();
        _chan_span_s = std::min(std::max(_chan_span_s, 1.0f), 600.0f);
        // set_frequency() ignores an out-of-range write, which would leave the
        // panel showing a frequency the radio is not on
        _freq_mhz = std::clamp(_freq_mhz, _freq_ui_min_mhz, _freq_ui_max_mhz);
        if (_loaded_rate_hz > 0.0) {
            _loaded_rate_hz = std::clamp(_loaded_rate_hz,
                                         static_cast<double>(_rate_ui_min_msps) * 1e6,
                                         static_cast<double>(_rate_ui_max_msps) * 1e6);
        }
        _freq_anchor_mhz = _freq_mhz;   // center the freq slider on the saved value
        return true;
    }

    void save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return;
        f << "freq_mhz "       << _freq_mhz       << "\n"
          << "gain_db "        << _gain_db        << "\n"
          << "lna_gain_db "    << _lna_db         << "\n"
          << "vga_gain_db "    << _vga_db         << "\n"
          << "amp_enable "     << _amp_enable     << "\n"
          << "threshold_db "   << _threshold_db   << "\n"
          << "hysteresis_db "  << _hysteresis_db  << "\n"
          << "window_ms "      << _window_ms      << "\n"
          << "pretrigger_pct " << _pretrigger_pct << "\n"
          << "holdoff_ms "     << _holdoff_ms     << "\n"
          << "auto_ms "        << _auto_ms        << "\n"
          << "edge "           << _edge_idx       << "\n"
          << "mode "           << _mode_idx       << "\n"
          << "show_scope "       << show_scope       << "\n"
          << "show_spectrum "    << show_spectrum    << "\n"
          << "show_spectrogram " << show_spectrogram << "\n"
          << "history_s "        << fpr_to_history(_sgram->frames_per_row()) << "\n"
          << "grid_on "          << _sgram->show_grid()  << "\n"
          << "grid_time_ms "     << _grid_time_ms        << "\n"
          << "grid_freq_mhz "    << _grid_freq_mhz       << "\n"
          << "zs_bw_hz "         << static_cast<double>(_zs_bw_mhz) * 1e6 << "\n"
          << "zs_filter_on "     << _zs_filter_on    << "\n"
          << "show_channelizer " << show_channelizer << "\n"
          << "chan_width_mhz "   << _chan_width_mhz  << "\n"
          << "chan_span_s "      << _chan_span_s     << "\n"
          << "rate_hz "          << _rate_hz         << "\n";
        if (!_snapshot_dir.empty())
            f << "snapshot_dir " << _snapshot_dir << "\n";
    }

private:
    static std::string trimmed(const char* s) {
        std::string v(s);
        size_t b = v.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        size_t e = v.find_last_not_of(" \t\r\n");
        return v.substr(b, e - b + 1);
    }

    static const char* state_str(Trig::State s) {
        switch (s) {
            case Trig::State::Idle:      return "IDLE (single done - re-arm)";
            case Trig::State::Armed:     return "ARMED";
            case Trig::State::Capturing: return "CAPTURING";
        }
        return "?";
    }

    // Fill a SourceConfig from the current UI state (both gain models); the
    // adapter uses only the fields its device understands.
    SourceConfig make_config(double rate_hz) const {
        SourceConfig cfg;
        cfg.center_freq_Hz = static_cast<double>(_freq_mhz) * 1e6;
        cfg.sample_rate_Hz = rate_hz;
        cfg.bandwidth_Hz   = rate_hz;
        cfg.gain_db        = static_cast<double>(_gain_db);
        cfg.lna_gain_db    = _lna_db;
        cfg.vga_gain_db    = _vga_db;
        cfg.amp_enable     = _amp_enable;
        return cfg;
    }

    void push_radio_config() {
        // current rate: UHD skips this no-op, HackRF ignores rate entirely
        _src->request_configure(make_config(_rate_hz));
    }

    // Stages a new rate onto the source's streaming thread; render()'s poll picks up the actual result. (UHD only)
    void push_rate_config(double rate_hz) {
        _src->request_configure(make_config(rate_hz));
    }

    // Single choke point when the ACTUAL device rate changes; re-derives all rate-dependent state.
    void on_rate_changed(double actual_hz) {
        const double old_rate_hz = _rate_hz;   // for seconds-preserving rescales
        _rate_hz   = actual_hz;
        _rate_msps = static_cast<float>(actual_hz / 1e6);   // widget display

        // Decimator first: trigger's time base below is the re-derived DECIMATED rate.
        clamp_zs_bw();
        push_power_config();

        // Capture memory is fixed size, so max window (ms) shrinks as detection rate rises; push one config so counts land together.
        refresh_trigger_window();
        push_trigger_config();

        clamp_chan_width();
        push_chan_config();

        // Spectrogram also clears its ring here -- old rows would be mislabeled at the new rate.
        const size_t sps = static_cast<size_t>(actual_hz + 0.5);
        _spectrum->set_sample_rate(sps);
        _sgram->set_sample_rate(sps);

        // Preserve on-screen seconds across rate changes: redo the pending startup seed at the real rate, else rescale by new/old rate.
        if (_history_seed_pending) {
            _sgram->set_frames_per_row(history_to_fpr(_history_s));
            _history_seed_pending = false;
        } else if (old_rate_hz > 0.0) {
            double fpr = std::round(static_cast<double>(_sgram->frames_per_row())
                                    * actual_hz / old_rate_hz);
            if (fpr < 1.0)   fpr = 1.0;
            if (fpr > 256.0) fpr = 256.0;
            _sgram->set_frames_per_row(static_cast<size_t>(fpr));
        }
    }

    // history_s (conf key) <-> frames/row, translated only at load/save (live control is the History slider); one row spans fpr*n_fft/sps seconds.
    size_t history_to_fpr(float history_s) const {
        double fpr = std::round(static_cast<double>(history_s) * _rate_hz
                                / (static_cast<double>(_n_fft)
                                   * static_cast<double>(_sgram_rows)));
        if (fpr < 1.0)   fpr = 1.0;
        if (fpr > 256.0) fpr = 256.0;
        return static_cast<size_t>(fpr);
    }
    float fpr_to_history(size_t fpr) const {
        return static_cast<float>(static_cast<double>(fpr)
                                  * static_cast<double>(_n_fft)
                                  * static_cast<double>(_sgram_rows) / _rate_hz);
    }

    // Requested zero-span bandwidth in Hz (the widget shows MHz).
    double zs_bw_hz() const { return static_cast<double>(_zs_bw_mhz) * 1e6; }

    // Decimation is capped (rate/Rmax); below this floor the request would snap far from typed, so clamp the widget instead.
    float min_zs_bw_mhz() const {
        const double floor_hz = std::max(PowerDet::MIN_OUTPUT_RATE_HZ,
                                         _rate_hz / static_cast<double>(PowerDet::MAX_DECIMATION));
        return static_cast<float>(std::min(floor_hz, _rate_hz) / 1e6);
    }

    void clamp_zs_bw() {
        const float hi = static_cast<float>(_rate_hz / 1e6);
        _zs_bw_mhz = std::min(std::max(_zs_bw_mhz, min_zs_bw_mhz()), hi);
    }

    // Re-derive the reachable window (fixed sample capacity in ms at the DETECTION rate) and re-clamp.
    void refresh_trigger_window() {
        _max_window_ms = 1000.0f * static_cast<float>(_trig->max_window_samples())
                       / static_cast<float>(detection_rate());
        if (_window_ms > _max_window_ms) _window_ms = _max_window_ms;
    }

    void push_power_config() {
        // Filter off requests the full rate; decimation_for() snaps that to R=1, a true bypass with no decimator object.
        const double bw = _zs_filter_on ? zs_bw_hz() : _rate_hz;
        _power->set_channel_bandwidth(bw, _rate_hz);
    }

    // Requested channelizer channel width in Hz (the widget shows MHz).
    double chan_width_hz() const { return static_cast<double>(_chan_width_mhz) * 1e6; }

    // N is capped at N_MAX; a narrower request would snap far from typed, so clamp (mirrors min_zs_bw_mhz).
    float min_chan_width_mhz() const {
        return static_cast<float>(
            _rate_hz / static_cast<double>(ChannelizerPanelBlock::N_MAX) / 1e6);
    }

    void clamp_chan_width() {
        const float hi = static_cast<float>(_rate_hz / 2e6);   // N >= 2
        _chan_width_mhz = std::min(std::max(_chan_width_mhz, min_chan_width_mhz()), hi);
    }

    void push_chan_config() {
        _chan->set_config(chan_width_hz(), _rate_hz,
                          static_cast<double>(_chan_span_s),
                          static_cast<double>(_freq_mhz) * 1e6);
    }

    void push_trigger_config() {
        _trig->set_config(_threshold_db, _window_ms, _pretrigger_pct,
                          _holdoff_ms,
                          _edge_idx == 0 ? Trig::Edge::Rising : Trig::Edge::Falling,
                          _mode_idx == 0 ? Trig::Mode::Normal
                                         : (_mode_idx == 1 ? Trig::Mode::Single
                                                           : Trig::Mode::Auto),
                          _hysteresis_db, _auto_ms,
                          detection_rate());
    }

    ISource* _src;
    Trig* _trig;
    PlotCSpectrumBlock* _spectrum;
    PlotCSpectrogramBlock* _sgram;
    PowerDetectorBlock<std::complex<float>>* _power;
    ChannelizerPanelBlock* _chan;

    SourceKind _kind;               // selects gain UI + rate-live behavior
    float  _freq_ui_min_mhz;        // freq slider/typed lower bound (device floor when probed)
float  _freq_ui_max_mhz;        // freq slider/typed upper bound (6000 UHD, 7250 HackRF)
    float  _rate_ui_min_msps;       // rate widget lower bound (0.1 UHD, 2 HackRF)
    float  _rate_ui_max_msps;       // rate widget upper bound (61.44 UHD, 20 HackRF)
    float  _freq_mhz;
    float  _freq_anchor_mhz;        // center of the +/-25MHz slider window; moved only on commit (render())
    float  _gain_db;                // UHD single-knob gain
    int    _lna_db;                 // HackRF LNA (0-40, step 8)
    int    _vga_db;                 // HackRF VGA (0-62, step 2)
    bool   _amp_enable;             // HackRF RX amp
    double _rate_hz;                // the ACTUAL device rate last synced to
    float  _rate_msps;              // rate widget value (MS/s), follows _rate_hz
    double _loaded_rate_hz = 0.0;   // conf key rate_hz (0 = not present)
    bool   _rate_from_cli;          // -r given explicitly: overrides saved rate_hz
    size_t _n_fft;
    size_t _sgram_rows;
    float  _history_s = 0.0f;               // waterfall depth (s); seeds spectrogram in apply_all()
    bool   _history_seed_pending = false;   // seed used construction rate; on_rate_changed() must redo it
    // Waterfall grid (conf: grid_on/grid_time_ms/grid_freq_mhz); on/off flag lives in the spectrogram window, panel owns/pushes the densities.
    bool   _show_grid      = false;
    float  _grid_time_ms   = 100.0f;   // horizontal (time) line spacing, ms
    float  _grid_freq_mhz  = 1.0f;     // vertical (frequency) line spacing, MHz
    float  _zs_bw_mhz;          // zero-span channel bandwidth (MHz); rate = bypass
    bool   _zs_filter_on = false;   // engages filter only when checked (old confs = off)
    float  _chan_width_mhz = 1.0f;  // requested width (conf key chan_width_mhz); snapped to rate/N live
    float  _chan_span_s    = 10.0f; // channelizer strip time depth (chan_span_s)

    // Trigger UI state (defaults mirror the TriggerBlock constructor below).
    float _threshold_db   = -40.0f;
    float _hysteresis_db  = 3.0f;
    float _window_ms      = 20.0f;
    float _pretrigger_pct = 10.0f;
    float _holdoff_ms     = 100.0f;
    float _auto_ms        = 200.0f;
    float _max_window_ms  = 200.0f;
    int   _edge_idx       = 0;  // Rising
    int   _mode_idx       = 2;  // Auto

    // Double-click-to-type state: which field (by label pointer) is being typed.
    const char* _editing  = nullptr;
    bool        _editing_start = false;

    // Tile visible plots as equal-height rows right of the panel: on first frame, view-set change, "Arrange windows", or a debounced resize.
    bool _prev_scope = false, _prev_spectrum = false, _prev_spectrogram = false;
    bool _prev_channelizer = false;
    bool _first_layout = true;
    // Retile only once WorkSize is STABLE for kResizeSettleFrames (no fighting a drag) and differs from the size last tiled.
    static constexpr int kResizeSettleFrames = 5;
    ImVec2 _tiled_work_size{0.0f, 0.0f};     // WorkSize used by the last tiling
    ImVec2 _pending_work_size{0.0f, 0.0f};   // last observed WorkSize
    int    _work_size_stable = 0;            // frames it has been unchanged

    // Snapshot: dest dir (conf key snapshot_dir, asked once via modal), edit buffer, transient status.
    std::string _snapshot_dir;
    char        _snapdir_edit[512] = {0};
    std::string _status;
    std::chrono::steady_clock::time_point _status_until{};
};
