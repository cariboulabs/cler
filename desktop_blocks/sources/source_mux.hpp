#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_sim.hpp"
#include "desktop_blocks/sigmf/source_sigmf.hpp"
#ifdef CLER_HAS_HACKRF
#include "desktop_blocks/sources/source_hackrf.hpp"
#endif
#ifdef CLER_HAS_CARIBOULITE
#include "desktop_blocks/sources/source_cariboulite.hpp"
#endif
#ifdef CLER_HAS_LIBIIO
#include "desktop_blocks/sources/source_pluto.hpp"
#endif
#ifdef CLER_HAS_UHD
#include "desktop_blocks/sources/source_uhd.hpp"
#endif
#ifdef CLER_HAS_SOAPYSDR
#include "desktop_blocks/sources/source_soapysdr.hpp"
#endif
#include <algorithm>

#include <complex>
#include <filesystem>
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

    void set_sigmf_dir(const std::string& dir) { _sigmf_dir = dir; }
    std::string sigmf_path(const std::string& name) const { return _sigmf_dir + "/" + name + ".sigmf-meta"; }
    static bool bare_name(const std::string& n) {
        return !n.empty() && n.find('/') == std::string::npos && n.find("..") == std::string::npos;
    }

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
#ifdef CLER_HAS_LIBIIO
        if (std::holds_alternative<Pluto>(_v)) {
            out.push_back({Kind::Pluto, _id, "PlutoSDR " + _id});
        } else if (iio_scan_context* sc = iio_create_scan_context("usb", 0)) {
            iio_context_info** info = nullptr;
            const ssize_t n = iio_scan_context_get_info_list(sc, &info);
            for (ssize_t i = 0; i < n; ++i) {
                const std::string uri = iio_context_info_get_uri(info[i]);
                out.push_back({Kind::Pluto, uri, "PlutoSDR " + uri});
            }
            if (n >= 0) iio_context_info_list_free(info);
            iio_scan_context_destroy(sc);
        }
#endif
#ifdef CLER_HAS_UHD
        if (std::holds_alternative<UHD>(_v)) {
            out.push_back({Kind::UHD, _id, "USRP " + _id});
        } else {
            for (const auto& addr : uhd::device::find(uhd::device_addr_t())) {
                const std::string id = addr.has_key("serial") ? "serial=" + addr["serial"] : addr.to_string();
                out.push_back({Kind::UHD, id, "USRP " + (addr.has_key("product") ? addr["product"] + " " : "") + id});
            }
        }
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (std::holds_alternative<Soapy>(_v)) {
            out.push_back({Kind::Soapy, _id, "Soapy " + _id});
        } else {
            for (const auto& dev : enumerate_devices()) {
                if (native_driver(dev.driver)) continue;
                out.push_back({Kind::Soapy, dev.get_args_string(),
                               dev.label.empty() ? "Soapy " + dev.driver : dev.label});
            }
        }
#endif
        if (!_sigmf_dir.empty()) {
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(_sigmf_dir, ec)) {
                if (!e.is_regular_file() || e.path().extension() != ".sigmf-meta") continue;
                const std::string name = e.path().stem().string();
                std::string label = name;
                std::error_code ec2;
                sigmf::Meta meta;
                if (!sigmf::try_read_meta(e.path().string(), meta)) continue;
                const auto bytes = std::filesystem::file_size(sigmf::data_path(e.path().string()), ec2);
                if (!ec2 && meta.sample_rate > 0) {
                    const double secs = static_cast<double>(bytes) /
                        (sigmf::datatype_size(meta.datatype) * meta.sample_rate);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), " (%.1f s @ %.3g MS/s)", secs, meta.sample_rate / 1e6);
                    label += buf;
                }
                out.push_back({Kind::SigMF, name, label});
            }
        }
        out.push_back({Kind::Sim, "", "Simulator"});
        return out;
    }

    // Availability check that never touches the open source.
    bool probe(Kind kind, const std::string& id) const {
        switch (kind) {
#ifdef CLER_HAS_HACKRF
            case Kind::HackRF:
                return SourceHackRFBlock::can_open(id.empty() ? nullptr : id.c_str());
#endif
#ifdef CLER_HAS_CARIBOULITE
            case Kind::Cariboulite:
                return (id == "s1g" || id == "hif") && CBL::can_open();
#endif
#ifdef CLER_HAS_LIBIIO
            case Kind::Pluto: {
                const std::string uri = id.empty() ? first_pluto_uri() : id;
                return !uri.empty() && SourcePlutoBlock::probe(uri.c_str()).ok;
            }
#endif
#ifdef CLER_HAS_UHD
            case Kind::UHD:
                return !uhd::device::find(uhd::device_addr_t(id)).empty() && UHD::can_open(id);
#endif
#ifdef CLER_HAS_SOAPYSDR
            case Kind::Soapy: {
                double f = 100e6, r = 2e6, g = 30.0;
                return soapy_probe_clamp(id, f, r, g);
            }
#endif
            case Kind::SigMF: {
                if (!bare_name(id)) return false;
                const std::string meta_path = sigmf_path(id);
                std::error_code ec;
                sigmf::Meta meta;
                return std::filesystem::is_regular_file(meta_path, ec) &&
                       std::filesystem::is_regular_file(sigmf::data_path(meta_path), ec) &&
                       sigmf::try_read_meta(meta_path, meta) &&
                       sigmf::datatype_is_complex(meta.datatype) && meta.sample_rate > 0;
            }
            case Kind::Sim:
                return true;
            default:
                return false;
        }
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
                rate_hz = std::clamp(rate_hz, 2e6, 20e6);
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
#ifdef CLER_HAS_LIBIIO
            case Kind::Pluto: {
                const std::string uri = id.empty() ? first_pluto_uri() : id;
                if (uri.empty()) return false;
                const auto pr = SourcePlutoBlock::probe(uri.c_str());
                if (!pr.ok) return false;
                _id = uri;
                _pluto_fmin = pr.fmin; _pluto_fmax = pr.fmax;
                _pluto_rmin = pr.rmin; _pluto_rmax = pr.rmax;
                const long long f = std::clamp<long long>(static_cast<long long>(freq_hz + 0.5), pr.fmin, pr.fmax);
                const long long r = std::clamp<long long>(static_cast<long long>(rate_hz + 0.5), pr.rmin, pr.rmax);
                _v.emplace<Pluto>("pluto", uri.c_str(), f, r, 50.0);
                return true;
            }
#endif
#ifdef CLER_HAS_UHD
            case Kind::UHD: {
                if (uhd::device::find(uhd::device_addr_t(id)).empty() || !UHD::can_open(id)) return false;
                _v.emplace<UHD>("uhd", freq_hz, rate_hz, id, 30.0, 1, "sc16", true);
                _uhd_freq = freq_hz;
                _uhd_gain = 30.0;
                return true;
            }
#endif
#ifdef CLER_HAS_SOAPYSDR
            case Kind::Soapy: {
                double f = freq_hz, r = rate_hz, g = 30.0;
                if (!soapy_probe_clamp(id, f, r, g)) return false;
                _v.emplace<Soapy>("soapy", id, f, r, g);
                return true;
            }
#endif
            case Kind::SigMF: {
                if (!bare_name(id)) return false;
                const std::string meta_path = sigmf_path(id);
                std::error_code ec;
                if (!std::filesystem::is_regular_file(meta_path, ec) ||
                    !std::filesystem::is_regular_file(sigmf::data_path(meta_path), ec)) return false;
                sigmf::Meta meta;
                if (!sigmf::try_read_meta(meta_path, meta)) return false;
                if (!sigmf::datatype_is_complex(meta.datatype) || meta.sample_rate <= 0) return false;
                _v.emplace<SigMFSrc>("sigmf", meta_path.c_str(), false, size_t(8192), true);
                return true;
            }
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
        if (std::holds_alternative<SigMFSrc>(_v)) return Kind::SigMF;
#ifdef CLER_HAS_HACKRF
        if (std::holds_alternative<SourceHackRFBlock>(_v)) return Kind::HackRF;
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (std::holds_alternative<CBL>(_v)) return Kind::Cariboulite;
#endif
#ifdef CLER_HAS_LIBIIO
        if (std::holds_alternative<Pluto>(_v)) return Kind::Pluto;
#endif
#ifdef CLER_HAS_UHD
        if (std::holds_alternative<UHD>(_v)) return Kind::UHD;
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (std::holds_alternative<Soapy>(_v)) return Kind::Soapy;
#endif
        return Kind::None;
    }
    const std::string& id() const { return _id; }

    double rate() const {
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) return s->rate();
        if (auto* f = std::get_if<SigMFSrc>(&_v)) return f->sample_rate();
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return h->get_sample_rate();
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) return const_cast<CBL*>(c)->get_sample_rate();
#endif
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) return static_cast<double>(pl->get_sample_rate());
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) return u->actual_sample_rate();
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) return so->get_sample_rate();
#endif
        return 0.0;
    }

    double center() const {
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) return s->center();
        if (auto* f = std::get_if<SigMFSrc>(&_v)) return f->center_frequency();
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return static_cast<double>(h->get_frequency());
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) return const_cast<CBL*>(c)->get_frequency();
#endif
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) return static_cast<double>(pl->get_frequency());
#endif
#ifdef CLER_HAS_UHD
        if (std::holds_alternative<UHD>(_v)) return _uhd_freq;
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) return so->get_frequency();
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
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) return pl->lost();
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) return u->lost();
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) return so->lost();
#endif
        return false;
    }

    size_t overflows() const {
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return h->get_overflow_count();
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) return u->get_overflow_count();
#endif
        return 0;
    }

    std::vector<Control> capabilities() const {
        std::vector<Control> c;
        if (auto* f = std::get_if<SigMFSrc>(&_v)) {
            c.push_back(range("freq", "Frequency", "Hz", 0, 6e9, 1, f->center_frequency(), true));
            c.push_back(range("rate", "Sample rate", "Hz", 0, 20e6, 1, f->sample_rate(), true));
            return c;
        }
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
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) {
            c.push_back(range("freq", "Frequency", "Hz", static_cast<double>(_pluto_fmin), static_cast<double>(_pluto_fmax), 1,
                              static_cast<double>(const_cast<Pluto*>(pl)->get_frequency())));
            c.push_back(range("rate", "Sample rate", "Hz", static_cast<double>(_pluto_rmin), static_cast<double>(_pluto_rmax), 1,
                              static_cast<double>(pl->get_sample_rate()), true));
            c.push_back(range("gain", "RX gain", "dB", -3, 71, 1, pl->get_gain()));
            Control agc = range("agc", "AGC", "", 0, 1, 1, pl->get_agc() ? 1 : 0);
            agc.type = "bool";
            c.push_back(agc);
        }
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) {
            const auto fr = u->rx_freq_range();
            const auto gr = u->rx_gain_range();
            const auto rr = u->rx_rate_range();
            c.push_back(range("freq", "Frequency", "Hz", fr.start(), fr.stop(), 1, u->get_frequency()));
            c.push_back(range("rate", "Sample rate", "Hz", rr.start(), rr.stop(), 1, u->actual_sample_rate(), true));
            c.push_back(range("gain", "RX gain", "dB", gr.start(), gr.stop(), gr.step() > 0 ? gr.step() : 1, _uhd_gain));
            const auto ants = u->rx_antennas();
            if (ants.size() > 1) {
                Control a;
                a.id = "antenna"; a.label = "Antenna"; a.type = "enum";
                a.options = ants;
                const auto cur = u->rx_antenna();
                a.value = static_cast<double>(std::find(ants.begin(), ants.end(), cur) - ants.begin());
                c.push_back(a);
            }
        }
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) {
            double fmin = 1e12, fmax = 0;
            for (const auto& fr : so->get_frequency_range()) { fmin = std::min(fmin, fr.minimum()); fmax = std::max(fmax, fr.maximum()); }
            double rmin = 1e12, rmax = 0;
            for (const auto& rr : so->get_sample_rate_range()) { rmin = std::min(rmin, rr.minimum()); rmax = std::max(rmax, rr.maximum()); }
            const auto gr = so->get_gain_range();
            c.push_back(range("freq", "Frequency", "Hz", fmin, fmax, 1, so->get_frequency()));
            c.push_back(range("rate", "Sample rate", "Hz", rmin, rmax, 1, so->get_sample_rate(), true));
            for (const auto& name : so->list_gains()) {
                const auto g = so->get_gain_range(name);
                c.push_back(range(("gain_" + name).c_str(), (name + " gain").c_str(), "dB",
                                  g.minimum(), g.maximum(), g.step() > 0 ? g.step() : 1, so->get_gain(name)));
            }
            if (so->list_gains().empty()) {
                c.push_back(range("gain", "RX gain", "dB", gr.minimum(), gr.maximum(), gr.step() > 0 ? gr.step() : 1, so->get_gain()));
            }
        }
#endif
        return c;
    }

    bool is_file() const { return std::holds_alternative<SigMFSrc>(_v); }
    void seek(double seconds) { if (auto* f = std::get_if<SigMFSrc>(&_v)) f->seek(seconds); }
    void pause(bool p) { if (auto* f = std::get_if<SigMFSrc>(&_v)) f->pause(p); }
    bool paused() const { auto* f = std::get_if<SigMFSrc>(&_v); return f && f->paused(); }
    void set_loop(bool l) { if (auto* f = std::get_if<SigMFSrc>(&_v)) f->set_loop(l); }
    bool looping() const { auto* f = std::get_if<SigMFSrc>(&_v); return f && f->looping(); }
    bool ended() const { auto* f = std::get_if<SigMFSrc>(&_v); return f && f->ended(); }
    double pos_seconds() const { auto* f = std::get_if<SigMFSrc>(&_v); return f ? f->pos_seconds() : 0.0; }
    double duration_seconds() const { auto* f = std::get_if<SigMFSrc>(&_v); return f ? f->duration_seconds() : 0.0; }

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
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) {
            if (id == "freq") pl->set_frequency(static_cast<long long>(value + 0.5));
            else if (id == "gain") pl->set_gain(value);
            else if (id == "agc") pl->set_agc(value >= 0.5);
            return;
        }
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) {
            UHDConfig cfg{_uhd_freq, u->actual_sample_rate(), _uhd_gain, 0.0};
            if (id == "freq") { cfg.center_freq_Hz = value; _uhd_freq = value; }
            else if (id == "gain") { cfg.gain = value; _uhd_gain = value; }
            else if (id == "antenna") {
                const auto ants = u->rx_antennas();
                const size_t i = static_cast<size_t>(value + 0.5);
                if (i < ants.size()) u->set_rx_antenna(ants[i]);
                return;
            } else return;
            u->request_configure(cfg);
            return;
        }
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) {
            if (id == "freq") so->set_frequency(value);
            else if (id == "gain") so->set_gain(value);
            else if (id.rfind("gain_", 0) == 0) so->set_gain(id.substr(5), value);
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

#ifdef CLER_HAS_LIBIIO
    static std::string first_pluto_uri() {
        std::string uri;
        if (iio_scan_context* sc = iio_create_scan_context("usb", 0)) {
            iio_context_info** info = nullptr;
            const ssize_t n = iio_scan_context_get_info_list(sc, &info);
            if (n > 0) uri = iio_context_info_get_uri(info[0]);
            if (n >= 0) iio_context_info_list_free(info);
            iio_scan_context_destroy(sc);
        }
        return uri;
    }
#endif

#ifdef CLER_HAS_SOAPYSDR
    static bool native_driver(const std::string& d) {
        (void)d;
#ifdef CLER_HAS_HACKRF
        if (d == "hackrf") return true;
#endif
#ifdef CLER_HAS_LIBIIO
        if (d == "plutosdr") return true;
#endif
#ifdef CLER_HAS_UHD
        if (d == "uhd") return true;
#endif
        return false;
    }

    static bool soapy_probe_clamp(const std::string& args, double& freq, double& rate, double& gain) {
        SoapySDR::Device* dev = nullptr;
        try {
            dev = SoapySDR::Device::make(args);
        } catch (const std::exception&) {
            return false;
        }
        if (!dev) return false;
        const auto frs = dev->getFrequencyRange(SOAPY_SDR_RX, 0);
        bool ok = frs.empty();
        for (const auto& r : frs) ok = ok || (freq >= r.minimum() && freq <= r.maximum());
        if (!ok) freq = 0.5 * (frs.front().minimum() + frs.front().maximum());
        const auto rrs = dev->getSampleRateRange(SOAPY_SDR_RX, 0);
        ok = rrs.empty();
        for (const auto& r : rrs) ok = ok || (rate >= r.minimum() && rate <= r.maximum());
        if (!ok) rate = std::clamp(rate, rrs.front().minimum(), rrs.front().maximum());
        const auto gr = dev->getGainRange(SOAPY_SDR_RX, 0);
        gain = std::clamp(gain, gr.minimum(), gr.maximum());
        SoapySDR::Device::unmake(dev);
        return true;
    }
#endif

#ifdef CLER_HAS_CARIBOULITE
    using CBL = SourceCaribouliteBlock<std::complex<float>>;
#endif
#ifdef CLER_HAS_LIBIIO
    using Pluto = SourcePlutoBlock;
#endif
#ifdef CLER_HAS_UHD
    using UHD = SourceUHDBlock<std::complex<float>>;
#endif
#ifdef CLER_HAS_SOAPYSDR
    using Soapy = SourceSoapySDRBlock<std::complex<float>>;
#endif
public:
    using SigMFSrc = SourceSigMFBlock<std::complex<float>>;
private:
    std::variant<std::monostate,
#ifdef CLER_HAS_HACKRF
                 SourceHackRFBlock,
#endif
#ifdef CLER_HAS_CARIBOULITE
                 CBL,
#endif
#ifdef CLER_HAS_LIBIIO
                 Pluto,
#endif
#ifdef CLER_HAS_UHD
                 UHD,
#endif
#ifdef CLER_HAS_SOAPYSDR
                 Soapy,
#endif
                 SigMFSrc,
                 SimSourceBlock> _v;
    std::string _id;
    std::string _sigmf_dir;
    long long _pluto_fmin = 0, _pluto_fmax = 0, _pluto_rmin = 0, _pluto_rmax = 0;
    double _uhd_freq = 0.0, _uhd_gain = 30.0;
};
