// Liquid-free BPSK message link: text -> BPSKFramer -> BPSKModulator -> channel
// -> BPSKReceiver -> BPSKDeframer -> text. The TX and RX share no clock: in --sim
// the channel adds a sample-clock offset (ppm), a carrier offset and noise; with
// --tx/--rx it is two radios.
//   ./bpsk_link --sim [--msg <text>] [--period-ms <n>] [--ppm <p>] [--cfo <Hz>] [--esn0 <dB>]
//               [--rate <S/s>] [--sps <n>] [--headless] [--seconds <n>] [--screenshot <png>]
//   ./bpsk_link --tx hackrf [--msg ...] [--freq <Hz>] [--rate <S/s>] [--gain <dB>] [--scale <0..1>]
//   ./bpsk_link --rx pluto [--uri ip:192.168.2.1] [--freq <Hz>] [--rate <S/s>] [--gain <dB>]
//               [--record <file.cf32>] [--headless]
// Each decoded message is printed; --headless prints one status line per second.
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/bpsk/deframer.hpp"
#include "desktop_blocks/bpsk/framer.hpp"
#include "desktop_blocks/bpsk/modulator.hpp"
#include "desktop_blocks/bpsk/receiver.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/noise/awgn.hpp"
#include "desktop_blocks/noise/clock_drift.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#ifdef BPSK_LINK_GUI
#include "desktop_blocks/gui/cler_palette.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "imgui.h"
#include "implot.h"
#endif
#ifdef CLER_HAS_HACKRF
#include "desktop_blocks/math/gain.hpp"
#include "desktop_blocks/sinks/sink_hackrf.hpp"
#endif
#ifdef CLER_HAS_LIBIIO
#include "desktop_blocks/sinks/sink_file.hpp"
#include "desktop_blocks/sources/source_pluto.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

// Emits "<text> #<n>" as a length-prefixed record every period.
struct MessageSourceBlock : public cler::BlockBase {
    MessageSourceBlock(const char* name, const std::string& text, int period_ms)
        : cler::BlockBase(name), _period(std::chrono::milliseconds(period_ms)) {
        std::snprintf(_text, sizeof(_text), "%s", text.c_str());
        _next = std::chrono::steady_clock::now();
    }

    uint64_t sent() const { return _sent.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        const auto now = std::chrono::steady_clock::now();
        if (now < _next) return cler::Error::NotEnoughSamples;
        char payload[bpsk::MAX_PAYLOAD + 1];
        const int len = std::snprintf(payload, sizeof(payload), "%s #%llu", _text,
                                      static_cast<unsigned long long>(_sent.load(std::memory_order_relaxed) + 1));
        const size_t n = std::min(static_cast<size_t>(len), bpsk::MAX_PAYLOAD);
        if (out->space() < 1 + n) return cler::Error::NotEnoughSpace;
        const uint8_t l = static_cast<uint8_t>(n);
        out->writeN(&l, 1);
        out->writeN(reinterpret_cast<const uint8_t*>(payload), n);
        _sent.fetch_add(1, std::memory_order_relaxed);
        _next += _period;
        if (_next < now) _next = now + _period;
        return cler::Empty{};
    }

private:
    char _text[bpsk::MAX_PAYLOAD + 1];
    std::chrono::steady_clock::duration _period;
    std::chrono::steady_clock::time_point _next;
    std::atomic<uint64_t> _sent{0};
};

// Takes length-prefixed records, prints each, keeps the last few for the GUI.
struct MessageSinkBlock : public cler::BlockBase {
    static constexpr size_t KEEP = 10;
    cler::Channel<uint8_t> in;

    explicit MessageSinkBlock(const char* name) : cler::BlockBase(name), in(4096) {}

    uint64_t received() const { return _count.load(std::memory_order_relaxed); }

    // Newest first; returns how many were copied.
    size_t recent(std::array<std::array<char, bpsk::MAX_PAYLOAD + 1>, KEEP>& dst) {
        std::lock_guard<std::mutex> lock(_mutex);
        const size_t n = std::min<size_t>(_count.load(std::memory_order_relaxed), KEEP);
        for (size_t i = 0; i < n; ++i) dst[i] = _ring[(_head + KEEP - 1 - i) % KEEP];
        return n;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0 || rsize < 1u + rptr[0]) return cler::Error::NotEnoughSamples;
        const size_t len = rptr[0];
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto& slot = _ring[_head];
            std::memcpy(slot.data(), rptr + 1, len);
            slot[len] = '\0';
            _head = (_head + 1) % KEEP;
            std::printf("RX  %.*s\n", static_cast<int>(len), reinterpret_cast<const char*>(rptr + 1));
            std::fflush(stdout);
        }
        in.commit_read(1 + len);
        _count.fetch_add(1, std::memory_order_relaxed);
        return cler::Empty{};
    }

private:
    std::mutex _mutex;
    std::array<std::array<char, bpsk::MAX_PAYLOAD + 1>, KEEP> _ring{};
    size_t _head = 0;
    std::atomic<uint64_t> _count{0};
};

void print_status(double t, const MessageSourceBlock* src, const MessageSinkBlock& sink,
                  const BPSKDeframerBlock& deframer, BPSKReceiverBlock& rx) {
    std::printf("[%6.1f s] %s%llu msgs OK  %llu CRC fail  CFO %+9.1f Hz  rate %+8.1f ppm  %s  EVM %5.1f dB\n", t,
                src ? ("sent " + std::to_string(src->sent()) + "  ").c_str() : "",
                static_cast<unsigned long long>(sink.received()),
                static_cast<unsigned long long>(deframer.crc_failures()), rx.cfo_hz() + rx.costas_freq_hz(),
                rx.rate_error_ppm(), rx.locked() ? "LOCK   " : "NO LOCK", rx.evm_db());
    std::fflush(stdout);
}

#ifdef BPSK_LINK_GUI
// Channel sliders (sim only), constellations before/after the Costas loop, loop
// state over time, decoded messages. GUI thread only.
struct LinkPanel : public cler::BlockBase {
    static constexpr bool is_gui = true;
    static constexpr size_t HISTORY = 600;  // 60 s at 10 samples/s

    LinkPanel(const char* name, BPSKReceiverBlock& rx, BPSKDeframerBlock& deframer, MessageSinkBlock& sink,
              MessageSourceBlock* src, ClockDriftBlock* drift, FrequencyShiftBlock* shift,
              NoiseAWGNBlock<std::complex<float>>* awgn, float ppm, float cfo_hz, float esn0_db, double symbol_rate)
        : cler::BlockBase(name), _rx(rx), _deframer(deframer), _sink(sink), _src(src), _drift(drift),
          _shift(shift), _awgn(awgn), _ppm(ppm), _cfo(cfo_hz), _esn0(esn0_db), _symbol_rate(symbol_rate),
          _started(std::chrono::steady_clock::now()) {}

    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Error::NotEnoughSamples; }

    void render() {
        sample_history();
        draw_controls();
        draw_constellations();
        draw_history();
    }

private:
    void sample_history() {
        const auto now = std::chrono::steady_clock::now();
        if (now - _last_sample < std::chrono::milliseconds(100)) return;
        _last_sample = now;
        const size_t i = _hist_n < HISTORY ? _hist_n++ : (shift_history(), HISTORY - 1);
        _t[i] = std::chrono::duration<float>(now - _started).count();
        _h_cfo[i] = static_cast<float>(_rx.cfo_hz() + _rx.costas_freq_hz()) / 1e3f;
        _h_ppm[i] = _rx.rate_error_ppm();
        _h_ted[i] = _rx.timing_error();
    }

    void shift_history() {
        for (auto* a : {&_t, &_h_cfo, &_h_ppm, &_h_ted}) std::copy(a->begin() + 1, a->end(), a->begin());
    }

    void draw_controls() {
        using namespace cler::palette;
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 840), ImGuiCond_FirstUseEver);
        ImGui::Begin("BPSK link");
        ImGui::TextDisabled("%.0f kBd, no shared clock between TX and RX", _symbol_rate / 1e3);

        if (_drift && _shift && _awgn) {
            ImGui::SeparatorText("Channel");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##ppm", &_ppm, -200.0f, 200.0f, "clock offset %.1f ppm")) _drift->set_ppm(_ppm);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##cfo", &_cfo, -100e3f, 100e3f, "carrier offset %.0f Hz")) _shift->set_frequency_shift(_cfo);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##esn0", &_esn0, 0.0f, 30.0f, "Es/N0 %.1f dB")) {
                _awgn->set_noise_stddev(bpsk::noise_stddev_for_esn0_db(_esn0));
            }
        }

        ImGui::SeparatorText("Receiver");
        const bool locked = _rx.locked();
        ImGui::TextColored(locked ? ok : danger, locked ? "LOCKED" : "NO LOCK");
        ImGui::SameLine(0, 16);
        ImGui::Text("EVM %.1f dB", _rx.evm_db());
        ImGui::Text("CFO   coarse %+9.1f Hz  + Costas %+7.1f Hz", _rx.cfo_hz(), _rx.costas_freq_hz());
        ImGui::Text("clock rate %+8.1f ppm   mu %.2f", _rx.rate_error_ppm(), _rx.mu());

        ImGui::SeparatorText("Frames");
        if (_src) ImGui::Text("sent         %llu", static_cast<unsigned long long>(_src->sent()));
        ImGui::Text("OK           %llu", static_cast<unsigned long long>(_sink.received()));
        ImGui::Text("CRC fail     %llu", static_cast<unsigned long long>(_deframer.crc_failures()));
        ImGui::Text("detections   %llu", static_cast<unsigned long long>(_deframer.preambles_detected()));
        ImGui::Text("inverted     %llu   (Costas at 180 deg, fixed)",
                    static_cast<unsigned long long>(_deframer.inverted_frames()));

        ImGui::SeparatorText("Decoded messages");
        const size_t n = _sink.recent(_recent);
        for (size_t i = 0; i < n; ++i) {
            ImGui::PushStyleColor(ImGuiCol_Text, i == 0 ? accent_hi : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TextUnformatted(_recent[i].data());
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

    void draw_constellations() {
        const size_t n = _rx.snapshot(_pre.data(), _post.data(), _pre.size());
        for (size_t i = 0; i < n; ++i) {
            _px[i] = _pre[i].real(); _py[i] = _pre[i].imag();
            _qx[i] = _post[i].real(); _qy[i] = _post[i].imag();
        }
        ImGui::SetNextWindowPos(ImVec2(440, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(820, 420), ImGuiCond_FirstUseEver);
        ImGui::Begin("BPSK constellation");
        if (ImPlot::BeginSubplots("##const", 1, 2, ImVec2(-1, -1))) {
            if (ImPlot::BeginPlot("before Costas (timing recovered)", ImVec2(), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
                ImPlot::SetupAxesLimits(-1.6, 1.6, -1.6, 1.6, ImPlotCond_Always);
                ImPlot::PlotScatter("pre", _px.data(), _py.data(), static_cast<int>(n));
                ImPlot::EndPlot();
            }
            if (ImPlot::BeginPlot("after Costas", ImVec2(), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
                ImPlot::SetupAxesLimits(-1.6, 1.6, -1.6, 1.6, ImPlotCond_Always);
                ImPlot::PlotScatter("post", _qx.data(), _qy.data(), static_cast<int>(n));
                ImPlot::EndPlot();
            }
            ImPlot::EndSubplots();
        }
        ImGui::End();
    }

    void draw_history() {
        ImGui::SetNextWindowPos(ImVec2(440, 440), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(820, 410), ImGuiCond_FirstUseEver);
        ImGui::Begin("BPSK loop state");
        const int n = static_cast<int>(_hist_n);
        if (ImPlot::BeginSubplots("##hist", 3, 1, ImVec2(-1, -1), ImPlotSubplotFlags_LinkAllX)) {
            plot_line("estimated CFO [kHz]", _h_cfo.data(), n);
            plot_line("clock rate error [ppm]", _h_ppm.data(), n);
            plot_line("Gardner timing error", _h_ted.data(), n);
            ImPlot::EndSubplots();
        }
        ImGui::End();
    }

    void plot_line(const char* title, const float* y, int n) {
        if (ImPlot::BeginPlot(title, ImVec2(), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("s", nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotLine(title, _t.data(), y, n);
            ImPlot::EndPlot();
        }
    }

    BPSKReceiverBlock& _rx;
    BPSKDeframerBlock& _deframer;
    MessageSinkBlock& _sink;
    MessageSourceBlock* _src;
    ClockDriftBlock* _drift;
    FrequencyShiftBlock* _shift;
    NoiseAWGNBlock<std::complex<float>>* _awgn;
    float _ppm, _cfo, _esn0;
    double _symbol_rate;

    std::array<std::array<char, bpsk::MAX_PAYLOAD + 1>, MessageSinkBlock::KEEP> _recent{};
    std::array<std::complex<float>, 512> _pre{}, _post{};
    std::array<float, 512> _px{}, _py{}, _qx{}, _qy{};
    std::array<float, HISTORY> _t{}, _h_cfo{}, _h_ppm{}, _h_ted{};
    size_t _hist_n = 0;
    std::chrono::steady_clock::time_point _started, _last_sample{};
};
#endif

struct Options {
    std::string mode = "sim", device, msg = "Hello CaribouLabs", uri = "ip:192.168.2.1", record, screenshot;
    int period_ms = 200;
    double ppm = 50.0, cfo_hz = 41e3, esn0_db = 15.0, rate = 2e6, freq = 918.5e6, gain = -1.0, scale = 0.5;
    unsigned int sps = 8;
    bool headless = false;
    double seconds = 0.0;
};

#ifdef BPSK_LINK_GUI
// Renders until the window closes, Ctrl-C, or --seconds; --screenshot grabs a frame after 5 s.
template <typename Fg>
void run_gui(Fg& fg, cler::GuiManager& gui, const Options& o) {
    fg.run();
    const auto t0 = std::chrono::steady_clock::now();
    bool shot = o.screenshot.empty();
    while (!gui.should_close() && !g_stop.load()) {
        gui.render(fg);
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (!shot && t > 5.0) {
            gui.request_screenshot(o.screenshot);
            shot = true;
        }
        if (o.seconds > 0.0 && t >= o.seconds) break;
    }
    fg.stop();
}
#endif

template <typename Fg>
int wait_headless(Fg& fg, const Options& o, const MessageSourceBlock* src, const MessageSinkBlock& sink,
                  const BPSKDeframerBlock& deframer, BPSKReceiverBlock& rx) {
    fg.run();
    const auto t0 = std::chrono::steady_clock::now();
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        print_status(t, src, sink, deframer, rx);
        if (o.seconds > 0.0 && t >= o.seconds) break;
    }
    fg.stop();
    return 0;
}

int run_sim(const Options& o) {
    const double rs = o.rate / o.sps;
    MessageSourceBlock src("Messages", o.msg, o.period_ms);
    BPSKFramerBlock framer("Framer", 128, true);
    ThrottleBlock<float> throttle("Throttle (symbol rate)", static_cast<size_t>(rs), 4096);
    BPSKModulatorBlock mod("Modulator", o.sps, 0.35f, 10, 4096);
    ClockDriftBlock drift("Clock drift", o.ppm, 0.3, 16384);
    FrequencyShiftBlock shift("Carrier offset", o.cfo_hz, o.rate, 16384);
    NoiseAWGNBlock<std::complex<float>> awgn("AWGN", bpsk::noise_stddev_for_esn0_db(static_cast<float>(o.esn0_db)), 16384, 1);
    BPSKReceiverBlock rx("Receiver", o.rate, [&] { BPSKReceiverConfig c; c.sps = o.sps; return c; }());
    BPSKDeframerBlock deframer("Deframer");
    MessageSinkBlock sink("Decoded");

    std::printf("bpsk_link --sim: %.0f kBd, sps %u, fs %.2f MS/s, ppm %+.1f, CFO %+.0f Hz, Es/N0 %.1f dB\n", rs / 1e3,
                o.sps, o.rate / 1e6, o.ppm, o.cfo_hz, o.esn0_db);
#ifdef BPSK_LINK_GUI
    if (!o.headless) {
        LinkPanel panel("Link", rx, deframer, sink, &src, &drift, &shift, &awgn, static_cast<float>(o.ppm),
                        static_cast<float>(o.cfo_hz), static_cast<float>(o.esn0_db), rs);
        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&src, &framer.in), cler::BlockRunner(&framer, &throttle.in),
            cler::BlockRunner(&throttle, &mod.in), cler::BlockRunner(&mod, &drift.in),
            cler::BlockRunner(&drift, &shift.in), cler::BlockRunner(&shift, &awgn.in),
            cler::BlockRunner(&awgn, &rx.in), cler::BlockRunner(&rx, &deframer.in),
            cler::BlockRunner(&deframer, &sink.in), cler::BlockRunner(&sink), cler::BlockRunner(&panel));
        cler::GuiManager gui(1280, 860, "cler BPSK link");
        run_gui(fg, gui, o);
        return 0;
    }
#endif
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&src, &framer.in), cler::BlockRunner(&framer, &throttle.in),
        cler::BlockRunner(&throttle, &mod.in), cler::BlockRunner(&mod, &drift.in),
        cler::BlockRunner(&drift, &shift.in), cler::BlockRunner(&shift, &awgn.in),
        cler::BlockRunner(&awgn, &rx.in), cler::BlockRunner(&rx, &deframer.in),
        cler::BlockRunner(&deframer, &sink.in), cler::BlockRunner(&sink));
    return wait_headless(fg, o, &src, sink, deframer, rx);
}

int run_tx(const Options& o) {
#ifdef CLER_HAS_HACKRF
    MessageSourceBlock src("Messages", o.msg, o.period_ms);
    BPSKFramerBlock framer("Framer", 128, true);
    BPSKModulatorBlock mod("Modulator", o.sps, 0.35f, 10, 4096);
    // Unit-energy pulses peak above 1 at sps 8; scale so the HackRF's int8 packing never clips.
    GainBlock<std::complex<float>> gain("TX scale", std::complex<float>(static_cast<float>(o.scale), 0.0f), 16384);
    SinkHackRFBlock hackrf("HackRF", static_cast<uint64_t>(o.freq), static_cast<uint32_t>(o.rate),
                           o.gain < 0.0 ? 0 : static_cast<int>(o.gain), false, 16384);
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&src, &framer.in), cler::BlockRunner(&framer, &mod.in),
        cler::BlockRunner(&mod, &gain.in), cler::BlockRunner(&gain, &hackrf.in), cler::BlockRunner(&hackrf));
    std::printf("bpsk_link --tx hackrf: %.4f MHz, %.2f MS/s, TXVGA %d dB, amp off, scale %.2f\n", o.freq / 1e6,
                o.rate / 1e6, o.gain < 0.0 ? 0 : static_cast<int>(o.gain), o.scale);
    fg.run();
    const auto t0 = std::chrono::steady_clock::now();
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[%6.1f s] sent %llu frames\n", t, static_cast<unsigned long long>(framer.frames_sent()));
        std::fflush(stdout);
        if (o.seconds > 0.0 && t >= o.seconds) break;
    }
    fg.stop();
    return 0;
#else
    (void)o;
    std::cerr << "bpsk_link: built without libhackrf, --tx hackrf unavailable\n";
    return 1;
#endif
}

int run_rx(const Options& o) {
#ifdef CLER_HAS_LIBIIO
    SourcePlutoBlock pluto("Pluto", o.uri.c_str(), static_cast<long long>(o.freq), static_cast<long long>(o.rate),
                           o.gain, 0, 1 << 16);
    BPSKReceiverBlock rx("Receiver", o.rate, [&] { BPSKReceiverConfig c; c.sps = o.sps; return c; }(), 1 << 17);
    BPSKDeframerBlock deframer("Deframer");
    MessageSinkBlock sink("Decoded");
    std::printf("bpsk_link --rx pluto: %s, %.4f MHz, %.2f MS/s, gain %s\n", o.uri.c_str(), o.freq / 1e6, o.rate / 1e6,
                o.gain < 0.0 ? "AGC" : std::to_string(o.gain).c_str());
    if (!o.record.empty()) {
        FanoutBlock<std::complex<float>> fan("Fanout", 2, 1 << 17);
        SinkFileBlock<std::complex<float>> file("Recorder", o.record.c_str(), 1 << 17);
        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&pluto, &fan.in), cler::BlockRunner(&fan, &rx.in, &file.in), cler::BlockRunner(&file),
            cler::BlockRunner(&rx, &deframer.in), cler::BlockRunner(&deframer, &sink.in), cler::BlockRunner(&sink));
        std::printf("recording raw cf32 IQ to %s\n", o.record.c_str());
        return wait_headless(fg, o, nullptr, sink, deframer, rx);
    }
#ifdef BPSK_LINK_GUI
    if (!o.headless) {
        LinkPanel panel("Link", rx, deframer, sink, nullptr, nullptr, nullptr, nullptr, 0.0f, 0.0f, 0.0f,
                        o.rate / o.sps);
        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&pluto, &rx.in), cler::BlockRunner(&rx, &deframer.in),
            cler::BlockRunner(&deframer, &sink.in), cler::BlockRunner(&sink), cler::BlockRunner(&panel));
        cler::GuiManager gui(1280, 860, "cler BPSK link (Pluto RX)");
        run_gui(fg, gui, o);
        return 0;
    }
#endif
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&pluto, &rx.in), cler::BlockRunner(&rx, &deframer.in),
        cler::BlockRunner(&deframer, &sink.in), cler::BlockRunner(&sink));
    return wait_headless(fg, o, nullptr, sink, deframer, rx);
#else
    (void)o;
    std::cerr << "bpsk_link: built without libiio, --rx pluto unavailable\n";
    return 1;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--sim") o.mode = "sim";
        else if (a == "--tx") { o.mode = "tx"; o.device = next(); }
        else if (a == "--rx") { o.mode = "rx"; o.device = next(); }
        else if (a == "--msg") o.msg = next();
        else if (a == "--period-ms") o.period_ms = std::stoi(next());
        else if (a == "--ppm") o.ppm = std::stod(next());
        else if (a == "--cfo") o.cfo_hz = std::stod(next());
        else if (a == "--esn0") o.esn0_db = std::stod(next());
        else if (a == "--rate") o.rate = std::stod(next());
        else if (a == "--sps") o.sps = static_cast<unsigned int>(std::stoul(next()));
        else if (a == "--freq") o.freq = std::stod(next());
        else if (a == "--gain") o.gain = std::stod(next());
        else if (a == "--scale") o.scale = std::stod(next());
        else if (a == "--uri") o.uri = next();
        else if (a == "--record") o.record = next();
        else if (a == "--headless") o.headless = true;
        else if (a == "--seconds") o.seconds = std::stod(next());
        else if (a == "--screenshot") o.screenshot = next();
        else {
            std::cout << "Usage: " << argv[0] << " --sim | --tx hackrf | --rx pluto  [options, see the file header]\n";
            return a == "--help" ? 0 : 1;
        }
    }
    std::signal(SIGINT, [](int) { g_stop.store(true); });
    if (o.mode == "tx" && o.device != "hackrf") { std::cerr << "bpsk_link: --tx supports hackrf\n"; return 1; }
    if (o.mode == "rx" && o.device != "pluto") { std::cerr << "bpsk_link: --rx supports pluto\n"; return 1; }
    if (o.mode == "tx") return run_tx(o);
    if (o.mode == "rx") return run_rx(o);
    return run_sim(o);
}
