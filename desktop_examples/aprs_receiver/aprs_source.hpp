#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "aprs_sim_source.hpp"
#ifdef APRS_HAVE_HACKRF
#include "desktop_blocks/sources/source_hackrf.hpp"
#endif

#include <chrono>
#include <complex>
#include <cstdio>
#include <string>
#include <thread>
#include <variant>
#include <vector>

// 8-bit IQ capture (hackrf_transfer -r) replayed in a loop
struct FileIQSourceBlock : public cler::BlockBase {
    FileIQSourceBlock(const char* name, const std::string& path, size_t chunk = 1 << 16)
        : cler::BlockBase(name), _raw(2 * chunk) {
        _f = std::fopen(path.c_str(), "rb");
        if (!_f) cler::panic("FileIQSourceBlock: cannot open file");
    }
    ~FileIQSourceBlock() { if (_f) std::fclose(_f); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [wptr, wsize] = out->write_dbf();
        size_t n = std::min(wsize, _raw.size() / 2);
        if (n == 0) return cler::Error::NotEnoughSpace;
        size_t got = std::fread(_raw.data(), 2, n, _f);
        if (got == 0) { std::rewind(_f); got = std::fread(_raw.data(), 2, n, _f); }
        if (got == 0) return cler::Error::TERM_IOError;
        for (size_t i = 0; i < got; ++i) wptr[i] = {_raw[2 * i] / 128.0f, _raw[2 * i + 1] / 128.0f};
        out->commit_write(got);
        return cler::Empty{};
    }

private:
    FILE* _f = nullptr;
    std::vector<int8_t> _raw;
};

// One source block, picked at runtime: HackRF, IQ file or the synthetic stations.
// File and sim are paced to real time here so the map's clocks mean something.
struct APRSSourceBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    enum class Kind { HackRF, File, Sim };

    APRSSourceBlock(const char* name, Kind kind, const std::string& file, double center_hz, double rate_hz,
                   int lna, int vga, bool amp)
        : cler::BlockBase(name), _kind(kind), _rate(rate_hz), _src(make(kind, file, center_hz, rate_hz, lna, vga, amp)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        if (_kind != Kind::HackRF) pace(out);
        return std::visit([&](auto& s) -> cler::Result<cler::Empty, cler::Error> {
            if constexpr (std::is_same_v<std::decay_t<decltype(s)>, std::monostate>) {
                cler::panic("APRSSourceBlock: empty source");
            } else {
                auto r = s.procedure(out);
                if (r.is_ok()) _emitted = out->producer_thread_cumulative_write_count();
                return r;
            }
        }, _src);
    }

    Kind kind() const { return _kind; }
    const char* kind_name() const { return _kind == Kind::HackRF ? "HackRF" : _kind == Kind::File ? "file" : "simulation"; }
    size_t overflow_count() const {
#ifdef APRS_HAVE_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_src)) return h->get_overflow_count();
#endif
        return 0;
    }

private:
    using Variant = std::variant<std::monostate, APRSSimSourceBlock, FileIQSourceBlock
#ifdef APRS_HAVE_HACKRF
        , SourceHackRFBlock
#endif
    >;

    static Variant make(Kind kind, const std::string& file, double center_hz, double rate_hz, int lna, int vga, bool amp) {
        (void)center_hz; (void)lna; (void)vga; (void)amp;
        switch (kind) {
            case Kind::Sim: return Variant(std::in_place_type<APRSSimSourceBlock>, "Sim stations", rate_hz, -250e3, 3e3, size_t{1} << 18);
            case Kind::File: return Variant(std::in_place_type<FileIQSourceBlock>, "IQ file", file);
            case Kind::HackRF:
#ifdef APRS_HAVE_HACKRF
                return Variant(std::in_place_type<SourceHackRFBlock>, "HackRF", static_cast<uint64_t>(center_hz + 0.5),
                               static_cast<uint32_t>(rate_hz + 0.5), lna, vga, amp, size_t{1} << 21);
#else
                cler::panic("aprs_receiver: built without HackRF; use --sim or --file");
#endif
        }
        cler::panic("APRSSourceBlock: unknown kind");
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
    size_t _emitted = 0;
    std::chrono::steady_clock::time_point _t0{};
};
