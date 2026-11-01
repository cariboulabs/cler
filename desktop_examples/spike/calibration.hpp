#pragma once

#include "cler.hpp"
#include "spike_source.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// offset(freq, gain) table measured by robin-sw dev-tools/drivers/radio/usrp_cal.py:
// dBm at the USRP RX2 connector = dBFS + offset. Loaded from its usrp_cal_<date>.tsv
// (columns freq_hz, rx_gain, offset_db), or the newest such file in a directory.
struct RxCalibration {
    static constexpr const char* FILE_GLOB_PREFIX = "usrp_cal_";
    static constexpr const char* FILE_EXT = ".tsv";

    static std::optional<RxCalibration> load(const std::string& file_or_dir) {
        std::filesystem::path path(file_or_dir);
        if (std::filesystem::is_directory(path)) {
            std::vector<std::filesystem::path> candidates;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(FILE_GLOB_PREFIX, 0) == 0 && entry.path().extension() == FILE_EXT)
                    candidates.push_back(entry.path());
            }
            if (candidates.empty()) {
                std::cerr << "No " << FILE_GLOB_PREFIX << "*" << FILE_EXT << " in " << file_or_dir << "\n";
                return std::nullopt;
            }
            path = *std::max_element(candidates.begin(), candidates.end());
        }
        std::ifstream in(path);
        if (!in) {
            std::cerr << "Cannot open calibration " << path << "\n";
            return std::nullopt;
        }
        RxCalibration cal;
        cal.path = path.string();
        std::string line;
        std::getline(in, line);
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            double freq_hz, gain_db, offset_db;
            if (fields >> freq_hz >> gain_db >> offset_db) {
                cal._freqs.insert(freq_hz);
                cal._gains.insert(gain_db);
                cal._offsets[{gain_db, freq_hz}] = offset_db;
            }
        }
        if (cal._offsets.empty()) {
            std::cerr << "Calibration " << path << " holds no points\n";
            return std::nullopt;
        }
        return cal;
    }

    double offset_db(double freq_hz, double gain_db) const {
        std::vector<double> per_gain;
        for (double gain : _gains) per_gain.push_back(interp_over_freq(gain, freq_hz));
        return interp(std::vector<double>(_gains.begin(), _gains.end()), per_gain, gain_db);
    }

    std::string path;

private:
    double interp_over_freq(double gain, double freq_hz) const {
        std::vector<double> xs, ys;
        for (double freq : _freqs) {
            auto it = _offsets.find({gain, freq});
            if (it != _offsets.end()) { xs.push_back(freq); ys.push_back(it->second); }
        }
        return interp(xs, ys, freq_hz);
    }

    static double interp(const std::vector<double>& xs, const std::vector<double>& ys, double x) {
        if (xs.empty()) return 0.0;
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back()) return ys.back();
        size_t hi = std::upper_bound(xs.begin(), xs.end(), x) - xs.begin();
        double t = (x - xs[hi - 1]) / (xs[hi] - xs[hi - 1]);
        return ys[hi - 1] + t * (ys[hi] - ys[hi - 1]);
    }

    std::set<double> _freqs, _gains;
    std::map<std::pair<double, double>, double> _offsets;
};

// Multiplies the I/Q stream so every downstream dB readout (spectrum, waterfall,
// power detector, trigger) is dBm at the connector instead of dBFS.
struct CalibrationScaleBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    explicit CalibrationScaleBlock(const char* name)
        : cler::BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>)),
          _buffer(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>)) {}

    void set_offset_db(double offset_db) {
        _scale.store(static_cast<float>(std::pow(10.0, offset_db / 20.0)), std::memory_order_relaxed);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        size_t transferable = std::min({in.size(), out->space(), _buffer.size()});
        if (transferable == 0) return cler::Error::NotEnoughSpaceOrSamples;
        in.readN(_buffer.data(), transferable);
        const float scale = _scale.load(std::memory_order_relaxed);
        for (size_t i = 0; i < transferable; ++i) _buffer[i] *= scale;
        out->writeN(_buffer.data(), transferable);
        return cler::Empty{};
    }

private:
    std::atomic<float> _scale{1.0f};
    std::vector<std::complex<float>> _buffer;
};

// Sits between the control panel and the radio: every tune or gain change also
// re-points the scale block at the matching calibration offset.
struct CalibratedSource : public ISource {
    CalibratedSource(ISource& radio, CalibrationScaleBlock& scale, RxCalibration calibration)
        : _radio(radio), _scale(scale), _calibration(std::move(calibration)) {}

    double actual_sample_rate() const override { return _radio.actual_sample_rate(); }
    size_t get_overflow_count() const override { return _radio.get_overflow_count(); }
    SourceKind kind() const override { return _radio.kind(); }

    void request_configure(const SourceConfig& cfg) override {
        _scale.set_offset_db(_calibration.offset_db(cfg.center_freq_Hz, cfg.gain_db));
        _radio.request_configure(cfg);
    }

private:
    ISource& _radio;
    CalibrationScaleBlock& _scale;
    RxCalibration _calibration;
};
