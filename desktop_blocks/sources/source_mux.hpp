#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_sim.hpp"
#ifdef CLER_HAS_HACKRF
#include "desktop_blocks/sources/source_hackrf.hpp"
#endif
#ifdef CLER_HAS_CARIBOULITE
#include "desktop_blocks/sources/source_cariboulite.hpp"
#endif

#include <complex>
#include <string>
#include <variant>
#include <vector>

// One source block over every SDR backend compiled in, plus a simulator, so an
// app can list what is plugged in and switch between them while its graph is
// stopped. select() and the describe/set calls are control-path (allocate
// freely); procedure() only forwards to the active backend.
struct SourceMux : public cler::BlockBase {
    static constexpr bool may_block = true;

    enum class Kind { None, HackRF, Pluto, UHD, Cariboulite, Soapy, SigMF, Sim };

    struct DeviceInfo {
        Kind kind;
        std::string id;     // serial / address / file; what select() takes back
        std::string label;  // what the user sees
    };

    struct Control {
        std::string id, label, type, unit;   // type: range | enum | bool
        double min = 0, max = 0, step = 0, value = 0;
        std::vector<std::string> options;
        bool ro = false;
    };

    explicit SourceMux(const char* name) : cler::BlockBase(name) {}

    static const char* kind_name(Kind k) {
        switch (k) {
            case Kind::HackRF: return "hackrf";
            case Kind::Pluto: return "pluto";
            case Kind::UHD: return "uhd";
            case Kind::Cariboulite: return "cariboulite";
            case Kind::Soapy: return "soapy";
            case Kind::SigMF: return "sigmf";
            case Kind::Sim: return "sim";
            default: return "none";
        }
    }

    std::vector<DeviceInfo> enumerate() const {
        std::vector<DeviceInfo> out;
#ifdef CLER_HAS_HACKRF
        if (std::holds_alternative<SourceHackRFBlock>(_v)) {
            out.push_back({Kind::HackRF, _id, "HackRF " + short_serial(_id)});
        } else if (hackrf_init() == HACKRF_SUCCESS) {
            if (hackrf_device_list_t* list = hackrf_device_list()) {
                for (int i = 0; i < list->devicecount; ++i) {
                    const std::string serial = list->serial_numbers[i] ? list->serial_numbers[i] : "";
                    out.push_back({Kind::HackRF, serial, "HackRF " + short_serial(serial)});
                }
                hackrf_device_list_free(list);
            }
            hackrf_exit();
        }
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (std::holds_alternative<CBL>(_v) || CBL::can_open()) {
            out.push_back({Kind::Cariboulite, "s1g", "CaribouLite S1G"});
            out.push_back({Kind::Cariboulite, "hif", "CaribouLite HiF"});
        }
#endif
        out.push_back({Kind::Sim, "", "Simulator"});
        return out;
    }

    // Graph must be stopped. Closes the current device, opens the new one;
    // false (and no source) if the device is gone, busy or not compiled in.
    bool select(Kind kind, const std::string& id, double freq_hz, double rate_hz) {
        _v.emplace<std::monostate>();
        _id = id;
        switch (kind) {
#ifdef CLER_HAS_HACKRF
            case Kind::HackRF:
                if (!SourceHackRFBlock::can_open(id.empty() ? nullptr : id.c_str())) return false;
                _v.emplace<SourceHackRFBlock>("hackrf", static_cast<uint64_t>(freq_hz + 0.5),
                                              static_cast<uint32_t>(rate_hz + 0.5), 40, 16, false, 0,
                                              id.empty() ? nullptr : id.c_str());
                return true;
#endif
#ifdef CLER_HAS_CARIBOULITE
            case Kind::Cariboulite:
                if ((id != "s1g" && id != "hif") || !CBL::can_open()) return false;
                {
                    const auto type = id == "hif" ? CaribouLiteRadio::HiF : CaribouLiteRadio::S1G;
                    CaribouLiteRadio* r = CaribouLite::GetInstance(false).GetRadioChannel(type);
                    if (!r) return false;
                    const auto ranges = r->GetFrequencyRange();
                    bool ok = false;
                    for (const auto& fr : ranges) ok = ok || (freq_hz > fr.fmin() && freq_hz < fr.fmax());
                    if (!ok && !ranges.empty()) freq_hz = 0.5 * (ranges.front().fmin() + ranges.front().fmax());
                    rate_hz = std::min<double>(std::max<double>(rate_hz, r->GetRxSampleRateMin()), r->GetRxSampleRateMax());
                    _v.emplace<CBL>("cariboulite", type, static_cast<float>(freq_hz),
                                    static_cast<float>(rate_hz), false, 40.0f);
                }
                return true;
#endif
            case Kind::Sim:
                _v.emplace<SimSourceBlock>("sim", rate_hz, freq_hz, 400e3);
                return true;
            default:
                return false;
        }
    }

    void close() { _v.emplace<std::monostate>(); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        return std::visit([&](auto& src) -> cler::Result<cler::Empty, cler::Error> {
            if constexpr (std::is_same_v<std::decay_t<decltype(src)>, std::monostate>) {
                return cler::Error::NotEnoughSamples;
            } else {
                return src.procedure(out);
            }
        }, _v);
    }

    Kind kind() const {
        if (std::holds_alternative<SimSourceBlock>(_v)) return Kind::Sim;
#ifdef CLER_HAS_HACKRF
        if (std::holds_alternative<SourceHackRFBlock>(_v)) return Kind::HackRF;
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (std::holds_alternative<CBL>(_v)) return Kind::Cariboulite;
#endif
        return Kind::None;
    }
    const std::string& id() const { return _id; }

    double rate() const {
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) return s->rate();
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return h->get_sample_rate();
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) return const_cast<CBL*>(c)->get_sample_rate();
#endif
        return 0.0;
    }

    double center() const {
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) return s->center();
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return static_cast<double>(h->get_frequency());
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) return const_cast<CBL*>(c)->get_frequency();
#endif
        return 0.0;
    }

    bool lost() const {
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return h->lost();
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) return c->lost();
#endif
        return false;
    }

    size_t overflows() const {
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return h->get_overflow_count();
#endif
        return 0;
    }

    std::vector<Control> capabilities() const {
        std::vector<Control> c;
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) {
            c.push_back(range("freq", "Frequency", "Hz", 0, 6e9, 1, s->center()));
            c.push_back(range("rate", "Sample rate", "Hz", 48e3, 20e6, 1, s->rate(), true));
            c.push_back(range("tone_hz", "Tone offset", "Hz", -10e6, 10e6, 1, s->tone_hz()));
            c.push_back(range("snr_db", "SNR", "dB", -20, 80, 1, s->snr_db()));
        }
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) {
            c.push_back(range("freq", "Frequency", "Hz", 1e6, 6e9, 1, static_cast<double>(h->get_frequency())));
            Control r = range("rate", "Sample rate", "Hz", 2e6, 20e6, 0, h->get_sample_rate(), true);
            r.type = "enum";
            for (const char* o : {"2000000", "2400000", "4000000", "8000000", "10000000", "12500000", "16000000", "20000000"}) r.options.push_back(o);
            c.push_back(r);
            c.push_back(range("lna", "LNA gain", "dB", 0, 40, 8, h->get_lna_gain()));
            c.push_back(range("vga", "VGA gain", "dB", 0, 62, 2, h->get_vga_gain()));
            Control amp = range("amp", "RF amp", "", 0, 1, 1, h->get_amp_enable() ? 1 : 0);
            amp.type = "bool";
            c.push_back(amp);
        }
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* cc = std::get_if<CBL>(&_v)) {
            CaribouLiteRadio& r = const_cast<CBL*>(cc)->radio();
            double fmin = 1e12, fmax = 0;
            for (const auto& fr : r.GetFrequencyRange()) { fmin = std::min<double>(fmin, fr.fmin()); fmax = std::max<double>(fmax, fr.fmax()); }
            c.push_back(range("freq", "Frequency", "Hz", fmin, fmax, 1, r.GetFrequency()));
            c.push_back(range("rate", "Sample rate", "Hz", r.GetRxSampleRateMin(), r.GetRxSampleRateMax(), 1, r.GetRxSampleRate(), true));
            c.push_back(range("gain", "RX gain", "dB", r.GetRxGainMin(), r.GetRxGainMax(), r.GetRxGainSteps(), r.GetRxGain()));
            Control agc = range("agc", "AGC", "", 0, 1, 1, r.GetAgc() ? 1 : 0);
            agc.type = "bool";
            c.push_back(agc);
            c.push_back(range("bw", "RX bandwidth", "Hz", r.GetRxBandwidthMin(), r.GetRxBandwidthMax(), 1, r.GetRxBandwidth()));
        }
#endif
        return c;
    }

    // Live controls only; rate changes go through select() with the graph stopped.
    void set(const std::string& id, double value) {
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) {
            if (id == "freq") s->set_center(value);
            else if (id == "tone_hz") s->set_tone_hz(value);
            else if (id == "snr_db") s->set_snr_db(static_cast<float>(value));
            return;
        }
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) {
            if (id == "freq") h->set_frequency(static_cast<uint64_t>(value + 0.5));
            else if (id == "lna") h->set_lna_gain(static_cast<int>(value + 0.5));
            else if (id == "vga") h->set_vga_gain(static_cast<int>(value + 0.5));
            else if (id == "amp") h->set_amp_enable(value >= 0.5);
            return;
        }
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) {
            if (id == "freq") c->set_frequency(static_cast<float>(value));
            else if (id == "gain") c->set_rx_gain(static_cast<float>(value));
            else if (id == "agc") c->set_agc(value >= 0.5);
            else if (id == "bw") c->set_bandwidth(static_cast<float>(value));
            return;
        }
#endif
    }

private:
    static Control range(const char* id, const char* label, const char* unit,
                         double min, double max, double step, double value, bool ro = false) {
        Control c;
        c.id = id; c.label = label; c.type = "range"; c.unit = unit;
        c.min = min; c.max = max; c.step = step; c.value = value; c.ro = ro;
        return c;
    }

    static std::string short_serial(const std::string& s) {
        return s.size() > 8 ? s.substr(s.size() - 8) : s;
    }

#ifdef CLER_HAS_CARIBOULITE
    using CBL = SourceCaribouliteBlock<std::complex<float>>;
#endif
    std::variant<std::monostate,
#ifdef CLER_HAS_HACKRF
                 SourceHackRFBlock,
#endif
#ifdef CLER_HAS_CARIBOULITE
                 CBL,
#endif
                 SimSourceBlock> _v;
    std::string _id;
};
