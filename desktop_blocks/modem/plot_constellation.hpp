#pragma once

#include "cler.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <atomic>
#include <complex>
#include <cstdio>
#include <mutex>
#include <vector>

// Scatter plot of the last N recovered constellation points. procedure() rings
// the points under a mutex; render() copies the ring on the GUI thread, the same
// snapshot pattern the spectrum plots use.
struct PlotConstellationBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    cler::Channel<std::complex<float>> in;

    PlotConstellationBlock(const char* name, size_t num_points = 2048, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _ring(num_points) {
        _scratch.resize(std::min<size_t>(buffer_size, 4096));
        _xs.resize(num_points);
        _ys.resize(num_points);
    }

    void set_initial_window(float x, float y, float w, float h) {
        _pos = ImVec2(x, y);
        _size = ImVec2(w, h);
    }

    // GUI-thread only: overlay text drawn on the plot.
    void set_metrics(float evm_percent, float snr_db, bool locked) {
        _evm = evm_percent;
        _snr = snr_db;
        _locked = locked;
        _has_metrics = true;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        const size_t n = std::min(in.size(), _scratch.size());
        if (n == 0) {
            return cler::Error::NotEnoughSamples;
        }
        in.readN(_scratch.data(), n);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (size_t i = 0; i < n; ++i) {
                _ring[_head] = _scratch[i];
                if (++_head == _ring.size()) _head = 0;
                if (_filled < _ring.size()) ++_filled;
            }
        }
        return cler::Empty{};
    }

    void render() {
        size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            count = _filled;
            const size_t start = (_head + _ring.size() - _filled) % _ring.size();
            for (size_t i = 0; i < count; ++i) {
                const std::complex<float>& p = _ring[(start + i) % _ring.size()];
                _xs[i] = p.real();
                _ys[i] = p.imag();
            }
        }

        ImGui::SetNextWindowPos(_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(_size, ImGuiCond_FirstUseEver);
        ImGui::Begin(name());
        if (ImPlot::BeginPlot("##constellation", ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("I", "Q");
            ImPlot::SetupAxesLimits(-1.5, 1.5, -1.5, 1.5, ImPlotCond_Always);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.0f);
            ImPlot::PlotScatter("points", _xs.data(), _ys.data(), static_cast<int>(count));
            if (_has_metrics) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "EVM %.1f%%  SNR %.1f dB  %s", _evm, _snr,
                              _locked ? "LOCK" : "no lock");
                ImPlot::PlotText(buf, 0.0, 1.32);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    std::vector<std::complex<float>> _ring;
    std::vector<std::complex<float>> _scratch;
    std::vector<float> _xs, _ys;
    size_t _head = 0;
    size_t _filled = 0;
    std::mutex _mutex;

    ImVec2 _pos{10.0f, 10.0f};
    ImVec2 _size{560.0f, 560.0f};
    float _evm = 0.0f, _snr = 0.0f;
    bool _locked = false, _has_metrics = false;
};
