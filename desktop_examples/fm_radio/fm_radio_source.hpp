#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#ifdef FM_RADIO_HAVE_HACKRF
#include "desktop_blocks/sources/source_hackrf.hpp"
#endif
#ifdef FM_RADIO_HAVE_PLUTO
#include "desktop_blocks/sources/source_pluto.hpp"
#endif
#ifdef FM_RADIO_HAVE_SOAPY
#include "desktop_blocks/sources/source_soapysdr.hpp"
#endif

#include <complex>
#include <string>
#include <variant>

// One source block, device picked at runtime. Each device is compiled in only
// when its library was found; asking for a missing one panics at startup.
struct RadioSource : public cler::BlockBase {
    static constexpr bool may_block = true;
    enum class Kind { HackRF, Pluto, Soapy };

    static Kind parse_kind(const std::string& s) {
        if (s == "hackrf") return Kind::HackRF;
        if (s == "pluto") return Kind::Pluto;
        if (s == "soapy") return Kind::Soapy;
        cler::panic("fm_radio: --source must be hackrf, pluto or soapy");
    }

    RadioSource(const char* name, Kind kind, const std::string& device, double freq_hz,
                double rate_hz, double gain_db, int lna_db, int vga_db, bool amp)
        : cler::BlockBase(name), _kind(kind), _source(make(kind, device, freq_hz, rate_hz, gain_db, lna_db, vga_db, amp)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        return std::visit([&](auto& src) -> cler::Result<cler::Empty, cler::Error> {
            if constexpr (std::is_same_v<std::decay_t<decltype(src)>, std::monostate>) {
                cler::panic("fm_radio: procedure() on an empty source");
            } else {
                return src.procedure(out);
            }
        }, _source);
    }

    Kind kind() const { return _kind; }
    const char* kind_name() const {
        switch (_kind) { case Kind::HackRF: return "HackRF"; case Kind::Pluto: return "Pluto"; default: return "SoapySDR"; }
    }

    void set_frequency(double hz) {
#ifdef FM_RADIO_HAVE_HACKRF
        if (auto* s = std::get_if<SourceHackRFBlock>(&_source)) { s->set_frequency(static_cast<uint64_t>(hz + 0.5)); return; }
#endif
#ifdef FM_RADIO_HAVE_PLUTO
        if (auto* s = std::get_if<SourcePlutoBlock>(&_source)) { s->set_frequency(static_cast<long long>(hz + 0.5)); return; }
#endif
#ifdef FM_RADIO_HAVE_SOAPY
        if (auto* s = std::get_if<SourceSoapySDRBlock<std::complex<float>>>(&_source)) { s->set_frequency(hz); return; }
#endif
        (void)hz;
    }

    // single gain knob; HackRF keeps its own three (see below)
    bool has_gain() const { return _kind == Kind::Soapy; }
    void set_gain(double db) {
#ifdef FM_RADIO_HAVE_SOAPY
        if (auto* s = std::get_if<SourceSoapySDRBlock<std::complex<float>>>(&_source)) s->set_gain(db);
#endif
        (void)db;
    }

#ifdef FM_RADIO_HAVE_HACKRF
    SourceHackRFBlock* hackrf() { return std::get_if<SourceHackRFBlock>(&_source); }
#else
    void* hackrf() { return nullptr; }
#endif

    size_t overflow_count() const {
#ifdef FM_RADIO_HAVE_HACKRF
        if (auto* s = std::get_if<SourceHackRFBlock>(&_source)) return s->get_overflow_count();
#endif
        return 0;
    }

private:
    using Variant = std::variant<std::monostate
#ifdef FM_RADIO_HAVE_HACKRF
        , SourceHackRFBlock
#endif
#ifdef FM_RADIO_HAVE_PLUTO
        , SourcePlutoBlock
#endif
#ifdef FM_RADIO_HAVE_SOAPY
        , SourceSoapySDRBlock<std::complex<float>>
#endif
    >;

    static Variant make(Kind kind, const std::string& device, double freq_hz, double rate_hz,
                        double gain_db, int lna_db, int vga_db, bool amp) {
        (void)device; (void)gain_db; (void)lna_db; (void)vga_db; (void)amp;
        switch (kind) {
            case Kind::HackRF:
#ifdef FM_RADIO_HAVE_HACKRF
                return Variant(std::in_place_type<SourceHackRFBlock>, "HackRF",
                               static_cast<uint64_t>(freq_hz + 0.5), static_cast<uint32_t>(rate_hz + 0.5),
                               lna_db, vga_db, amp, size_t{1} << 21);
#else
                cler::panic("fm_radio: built without HackRF support");
#endif
            case Kind::Pluto:
#ifdef FM_RADIO_HAVE_PLUTO
                return Variant(std::in_place_type<SourcePlutoBlock>, "Pluto",
                               device.empty() ? "ip:pluto.local" : device.c_str(),
                               static_cast<long long>(freq_hz + 0.5), static_cast<long long>(rate_hz + 0.5),
                               gain_db, 0LL, size_t{1} << 16);
#else
                cler::panic("fm_radio: built without Pluto (libiio) support");
#endif
            case Kind::Soapy:
#ifdef FM_RADIO_HAVE_SOAPY
                return Variant(std::in_place_type<SourceSoapySDRBlock<std::complex<float>>>, "SoapySDR",
                               device, freq_hz, rate_hz, gain_db);
#else
                cler::panic("fm_radio: built without SoapySDR support");
#endif
        }
        cler::panic("fm_radio: unknown source kind");
    }

    Kind _kind;
    Variant _source;
};
