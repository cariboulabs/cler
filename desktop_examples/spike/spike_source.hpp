#pragma once

#include <algorithm>
#include <complex>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#ifdef SPIKE_HAVE_UHD
#include "desktop_blocks/sources/source_uhd.hpp"
#endif
#ifdef SPIKE_HAVE_HACKRF
#include "desktop_blocks/sources/source_hackrf.hpp"
#endif
#ifdef SPIKE_HAVE_PLUTO
#include "desktop_blocks/sources/source_pluto.hpp"
#endif

enum class SourceKind { UHD, HackRF, Pluto };

#ifdef SPIKE_HAVE_PLUTO
static constexpr const char* DEFAULT_PLUTO_URI = "ip:192.168.2.1";
#endif

#ifdef SPIKE_HAVE_HACKRF
inline constexpr double HACKRF_RATE_MIN_HZ = 2e6;
inline constexpr double HACKRF_RATE_MAX_HZ = 20e6;
#endif

struct SourceConfig {
    double center_freq_Hz = 915e6;
    double sample_rate_Hz = 1e6;
    double bandwidth_Hz   = 1e6;
    double gain_db        = 30.0;
    int    lna_gain_db    = 40;
    int    vga_gain_db    = 16;
    bool   amp_enable     = false;
};

struct ISource {
    virtual ~ISource() = default;
    virtual double actual_sample_rate() const = 0;
    virtual void   request_configure(const SourceConfig& cfg) = 0;
    virtual size_t get_overflow_count() const = 0;
    virtual SourceKind kind() const = 0;
};

// Runs before the source constructor, which panics on anything the driver rejects.
inline std::string check_and_clamp_source(SourceKind kind, const std::string& dev,
                                          double& freq_hz, double& rate_hz) {
    (void)dev; (void)freq_hz; (void)rate_hz;
    switch (kind) {
        case SourceKind::Pluto: {
#ifdef SPIKE_HAVE_PLUTO
            const std::string uri = dev.empty() ? DEFAULT_PLUTO_URI : dev;
            const auto pr = SourcePlutoBlock::probe(uri.c_str());
            if (!pr.reached) return "cannot reach pluto at " + uri;
            if (!pr.ok) return "pluto at " + uri +
                               " has no receiver - its firmware exposes no cf-ad9361-lpc";
            // probe() already nudges rmin past the floor the driver rejects
            freq_hz = std::clamp(freq_hz, double(pr.fmin), double(pr.fmax));
            rate_hz = std::clamp(rate_hz, double(pr.rmin), double(pr.rmax));
            return {};
#else
            return "built without Pluto support (libiio not found at configure time)";
#endif
        }
        case SourceKind::HackRF: {
#ifdef SPIKE_HAVE_HACKRF
            const int r = SourceHackRFBlock::open_status(dev.empty() ? nullptr : dev.c_str());
            if (r == HACKRF_ERROR_NOT_FOUND) return "no hackrf found";
            if (r != HACKRF_SUCCESS) {
                return "hackrf: " + std::string(hackrf_error_name(static_cast<hackrf_error>(r))) +
                       " - another program may have it open, or the udev rules are missing";
            }
            rate_hz = std::clamp(rate_hz, HACKRF_RATE_MIN_HZ, HACKRF_RATE_MAX_HZ);
            return {};
#else
            return "built without HackRF support (libhackrf not found at configure time)";
#endif
        }
        case SourceKind::UHD:
#ifdef SPIKE_HAVE_UHD
            return {};
#else
            return "built without UHD support (UHD not found at configure time)";
#endif
    }
    return {};
}

struct SpikeSourceBlock : public cler::BlockBase, public ISource {
    static constexpr bool may_block = true;

#ifdef SPIKE_HAVE_UHD
    using UHDSource = SourceUHDBlock<std::complex<float>>;
#endif
    using SourceVariant = std::variant<
        std::monostate
#ifdef SPIKE_HAVE_UHD
        , UHDSource
#endif
#ifdef SPIKE_HAVE_HACKRF
        , SourceHackRFBlock
#endif
#ifdef SPIKE_HAVE_PLUTO
        , SourcePlutoBlock
#endif
    >;

    SpikeSourceBlock(const char* name, SourceKind kind,
                     double freq_hz, double rate_hz,
                     const std::string& device_address = "",
                     double gain_db = 30.0,
                     int lna_gain_db = 40, int vga_gain_db = 16,
                     bool amp_enable = false)
        : cler::BlockBase(name),
          _source(make_source(kind, freq_hz, rate_hz, device_address, gain_db,
                              lna_gain_db, vga_gain_db, amp_enable)) {
#ifdef SPIKE_HAVE_HACKRF
        if (auto* hackrf = std::get_if<SourceHackRFBlock>(&_source))
            _hackrf_rate = hackrf->get_sample_rate();
#endif
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        return std::visit([&](auto& src) -> cler::Result<cler::Empty, cler::Error> {
            if constexpr (std::is_same_v<std::decay_t<decltype(src)>, std::monostate>) {
                cler::panic("spike: procedure() on an empty source");
            } else {
                return src.procedure(out);
            }
        }, _source);
    }

    double actual_sample_rate() const override {
        return std::visit([](const auto& src) -> double {
            if constexpr (std::is_same_v<std::decay_t<decltype(src)>, std::monostate>) {
                cler::panic("spike: actual_sample_rate() on an empty source");
            }
#ifdef SPIKE_HAVE_UHD
            else if constexpr (std::is_same_v<std::decay_t<decltype(src)>, UHDSource>) {
                return src.actual_sample_rate();
            }
#endif
            else {
                return static_cast<double>(src.get_sample_rate());
            }
        }, _source);
    }

    void request_configure(const SourceConfig& cfg) override {
#ifdef SPIKE_HAVE_HACKRF
        if (auto* hackrf = std::get_if<SourceHackRFBlock>(&_source)) {
            configure_hackrf(*hackrf, cfg);
            return;
        }
#endif
#ifdef SPIKE_HAVE_PLUTO
        if (auto* pluto = std::get_if<SourcePlutoBlock>(&_source)) {
            pluto->set_frequency(static_cast<long long>(cfg.center_freq_Hz + 0.5));
            return;
        }
#endif
#ifdef SPIKE_HAVE_UHD
        if (auto* uhd = std::get_if<UHDSource>(&_source)) {
            configure_uhd(*uhd, cfg);
            return;
        }
#endif
        (void)cfg;
        cler::panic("spike: request_configure() on an empty source");
    }

    size_t get_overflow_count() const override {
        return std::visit([](const auto& src) -> size_t {
            if constexpr (std::is_same_v<std::decay_t<decltype(src)>, std::monostate>) {
                cler::panic("spike: get_overflow_count() on an empty source");
            }
#ifdef SPIKE_HAVE_PLUTO
            else if constexpr (std::is_same_v<std::decay_t<decltype(src)>, SourcePlutoBlock>) {
                return 0;
            }
#endif
            else {
                return src.get_overflow_count();
            }
        }, _source);
    }

    SourceKind kind() const override {
#ifdef SPIKE_HAVE_HACKRF
        if (std::holds_alternative<SourceHackRFBlock>(_source)) return SourceKind::HackRF;
#endif
#ifdef SPIKE_HAVE_PLUTO
        if (std::holds_alternative<SourcePlutoBlock>(_source)) return SourceKind::Pluto;
#endif
        return SourceKind::UHD;
    }

private:
#ifdef SPIKE_HAVE_HACKRF
    static constexpr int HACKRF_LNA_MAX_DB  = 40;
    static constexpr int HACKRF_LNA_STEP_DB = 8;
    static constexpr int HACKRF_VGA_MAX_DB  = 62;
    static constexpr int HACKRF_VGA_STEP_DB = 2;

    static int snap_gain(int requested_db, int max_db, int step_db) {
        int clamped = std::min(std::max(requested_db, 0), max_db);
        return (clamped / step_db) * step_db;
    }

    void configure_hackrf(SourceHackRFBlock& hackrf, const SourceConfig& cfg) {
        uint32_t requested_rate = static_cast<uint32_t>(
            std::clamp(cfg.sample_rate_Hz, HACKRF_RATE_MIN_HZ, HACKRF_RATE_MAX_HZ) + 0.5);
        bool rate_change_restarts_stream = requested_rate != 0 && requested_rate != _hackrf_rate;
        if (rate_change_restarts_stream) {
            hackrf.set_sample_rate(requested_rate);
            _hackrf_rate = hackrf.get_sample_rate();
        }
        hackrf.set_frequency(static_cast<uint64_t>(cfg.center_freq_Hz + 0.5));
        hackrf.set_lna_gain(snap_gain(cfg.lna_gain_db, HACKRF_LNA_MAX_DB, HACKRF_LNA_STEP_DB));
        hackrf.set_vga_gain(snap_gain(cfg.vga_gain_db, HACKRF_VGA_MAX_DB, HACKRF_VGA_STEP_DB));
        hackrf.set_amp_enable(cfg.amp_enable);
    }
#endif

#ifdef SPIKE_HAVE_UHD
    static void configure_uhd(UHDSource& uhd, const SourceConfig& cfg) {
        UHDConfig staged;
        staged.center_freq_Hz = cfg.center_freq_Hz;
        staged.sample_rate_Hz = cfg.sample_rate_Hz;
        staged.gain           = cfg.gain_db;
        staged.bandwidth_Hz   = cfg.bandwidth_Hz;
        uhd.request_configure(staged);
    }
#endif

    static SourceVariant make_source(SourceKind kind, double freq_hz, double rate_hz,
                                     const std::string& device_address, double gain_db,
                                     int lna_gain_db, int vga_gain_db, bool amp_enable) {
        (void)device_address; (void)gain_db;
        (void)lna_gain_db; (void)vga_gain_db; (void)amp_enable;
        switch (kind) {
            case SourceKind::HackRF:
#ifdef SPIKE_HAVE_HACKRF
                return SourceVariant(std::in_place_type<SourceHackRFBlock>, "HackRF",
                                     static_cast<uint64_t>(freq_hz + 0.5),
                                     static_cast<uint32_t>(rate_hz + 0.5),
                                     lna_gain_db, vga_gain_db, amp_enable);
#else
                cler::panic("spike: built without HackRF support (libhackrf not found at configure time)");
#endif
            case SourceKind::Pluto:
#ifdef SPIKE_HAVE_PLUTO
                return SourceVariant(std::in_place_type<SourcePlutoBlock>, "Pluto",
                                     device_address.empty() ? DEFAULT_PLUTO_URI
                                                            : device_address.c_str(),
                                     static_cast<long long>(freq_hz + 0.5),
                                     static_cast<long long>(rate_hz + 0.5),
                                     gain_db);
#else
                cler::panic("spike: built without Pluto support (libiio not found at configure time)");
#endif
            case SourceKind::UHD:
#ifdef SPIKE_HAVE_UHD
                return SourceVariant(std::in_place_type<UHDSource>, "USRP",
                                     freq_hz, rate_hz, device_address, gain_db,
                                     1, "sc16", true);
#else
                cler::panic("spike: built without UHD support (UHD not found at configure time)");
#endif
        }
        cler::panic("spike: unknown source kind");
    }

    SourceVariant _source;
#ifdef SPIKE_HAVE_HACKRF
    uint32_t _hackrf_rate = 0;
#endif
};
