#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_iq_file.hpp"
#ifdef CLER_HAS_HACKRF
#include "desktop_blocks/sources/source_hackrf.hpp"
#endif

#include <chrono>
#include <complex>
#include <string>
#include <thread>
#include <variant>

// One source block for the VHF receivers, picked at runtime: HackRF, IQ file or
// the app's own synthetic transmitter. File and sim are paced to real time here
// so the map's clocks mean something.
template <typename SimBlock>
struct PacedSelectableSource : public cler::BlockBase {
    static constexpr bool may_block = true;
    enum class Kind { HackRF, File, Sim };

    // sim_args are forwarded to SimBlock, which is constructed in place: these
    // blocks own liquid handles and must never be moved.
    template <typename... SimArgs>
    PacedSelectableSource(const char* name, Kind kind, const std::string& file, double center_hz,
                          double rate_hz, int lna, int vga, bool amp, SimArgs&&... sim_args)
        : cler::BlockBase(name), _kind(kind), _rate(rate_hz),
          _src(make(kind, file, center_hz, rate_hz, lna, vga, amp, std::forward<SimArgs>(sim_args)...)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        if (_kind != Kind::HackRF) pace(out);
        return std::visit([&](auto& s) -> cler::Result<cler::Empty, cler::Error> {
            if constexpr (std::is_same_v<std::decay_t<decltype(s)>, std::monostate>) {
                cler::panic("PacedSelectableSource: empty source");
            } else {
                return s.procedure(out);
            }
        }, _src);
    }

    Kind kind() const { return _kind; }
    const char* kind_name() const { return _kind == Kind::HackRF ? "HackRF" : _kind == Kind::File ? "file" : "simulation"; }
    size_t overflow_count() const {
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_src)) return h->get_overflow_count();
#endif
        return 0;
    }

private:
    using Variant = std::variant<std::monostate, SimBlock, SourceIQFileBlock
#ifdef CLER_HAS_HACKRF
        , SourceHackRFBlock
#endif
    >;

    template <typename... SimArgs>
    static Variant make(Kind kind, const std::string& file, double center_hz, double rate_hz,
                        int lna, int vga, bool amp, SimArgs&&... sim_args) {
        (void)center_hz; (void)lna; (void)vga; (void)amp;
        switch (kind) {
            case Kind::Sim: return Variant(std::in_place_type<SimBlock>, std::forward<SimArgs>(sim_args)...);
            case Kind::File: return Variant(std::in_place_type<SourceIQFileBlock>, "IQ file", file);
            case Kind::HackRF:
#ifdef CLER_HAS_HACKRF
                return Variant(std::in_place_type<SourceHackRFBlock>, "HackRF", static_cast<uint64_t>(center_hz + 0.5),
                               static_cast<uint32_t>(rate_hz + 0.5), lna, vga, amp, size_t{1} << 21);
#else
                cler::panic("built without HackRF; use --sim or --file");
#endif
        }
        cler::panic("PacedSelectableSource: unknown kind");
    }

    // sleep until the samples already emitted are due in wall time
    void pace(cler::ChannelBase<std::complex<float>>* out) {
        using clock = std::chrono::steady_clock;
        if (_t0 == clock::time_point{}) _t0 = clock::now();
        const size_t emitted = out->producer_thread_cumulative_write_count();
        const auto due = _t0 + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(emitted / _rate));
        const auto now = clock::now();
        if (due > now) std::this_thread::sleep_for(std::min(due - now, clock::duration(std::chrono::milliseconds(200))));
    }

    Kind _kind;
    double _rate;
    Variant _src;
    std::chrono::steady_clock::time_point _t0{};
};
