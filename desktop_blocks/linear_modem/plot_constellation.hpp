#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <atomic>
#include <complex>
#include <cstdio>
#include <mutex>
#include <vector>

// Scatter plot of the last N recovered constellation points. Same shape as the
// other plot blocks: procedure() rings the points into a Channel, render()
// snapshots it into preallocated x/y arrays under a try_lock and draws; nothing
// is allocated per frame and the point count is bounded by the ring.
struct PlotConstellationBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    cler::Channel<std::complex<float>> in;

    PlotConstellationBlock(const char* name, size_t num_points = 2048, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _capacity(num_points), _ring(num_points) {
        if (num_points < 64) {
            cler::panic("PlotConstellationBlock needs at least 64 points");
        }
        _tmp.resize(std::min<size_t>(4096, num_points));
        _snapshot_x.resize(num_points);
        _snapshot_y.resize(num_points);
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

    // GUI-THREAD-ONLY: overlay text drawn on the plot.
    void set_metrics(float evm_percent, float snr_db, bool locked) {
        _evm = evm_percent;
        _snr = snr_db;
        _locked = locked;
        _has_metrics = true;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        const size_t work = std::min(in.size(), _tmp.size());
        if (work == 0) {
            return cler::Error::NotEnoughSamples;
        }
        const size_t drop = (_ring.size() + work > _capacity) ? (_ring.size() + work - _capacity) : 0;
        in.readN(_tmp.data(), work);
        _ring.commit_read(drop);
        _ring.writeN(_tmp.data(), work);
        return cler::Empty{};
    }

    void render() {
        // Skip the snapshot while paused so the display freezes even though
        // procedure() keeps draining input underneath.
        if (!_gui_pause.load(std::memory_order_acquire) && _snapshot_mutex.try_lock()) {
            const std::complex<float>* ptr1; const std::complex<float>* ptr2;
            size_t size1, size2;
            const size_t available = _ring.peek_read(ptr1, size1, ptr2, size2);
            if (available > 0) {
                for (size_t i = 0; i < size1; ++i) {
                    _snapshot_x[i] = ptr1[i].real();
                    _snapshot_y[i] = ptr1[i].imag();
                }
                for (size_t i = 0; i < size2; ++i) {
                    _snapshot_x[size1 + i] = ptr2[i].real();
                    _snapshot_y[size1 + i] = ptr2[i].imag();
                }
                _snapshot_ready_size = available;
            }
            _snapshot_mutex.unlock();
        }

        if (_snapshot_ready_size == 0) {
            return;
        }

        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin(name());

        if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
            _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
        }
        if (_has_metrics) {
            ImGui::SameLine(0, 16);
            ImGui::Text("EVM %.1f %%   SNR %.1f dB   %s", _evm, _snr, _locked ? "LOCK" : "no lock");
        }

        if (ImPlot::BeginPlot(name(), ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("I", "Q");
            ImPlot::SetupAxesLimits(-1.5, 1.5, -1.5, 1.5, ImPlotCond_Always);
            ImPlot::PlotScatter("points", _snapshot_x.data(), _snapshot_y.data(),
                                static_cast<int>(_snapshot_ready_size),
                                ImPlotSpec(ImPlotProp_Marker, ImPlotMarker_Circle,
                                           ImPlotProp_MarkerSize, 1.6f));
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    size_t _capacity;
    cler::Channel<std::complex<float>> _ring;
    std::vector<std::complex<float>> _tmp;
    std::vector<float> _snapshot_x, _snapshot_y;
    size_t _snapshot_ready_size = 0;
    std::mutex _snapshot_mutex;
    std::atomic<bool> _gui_pause = false;

    ImVec2 _initial_window_position{0.0f, 0.0f};
    ImVec2 _initial_window_size{600.0f, 600.0f};

    float _evm = 0.0f, _snr = 0.0f;
    bool _locked = false, _has_metrics = false;
};
