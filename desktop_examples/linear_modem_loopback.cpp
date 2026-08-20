// Digital modem loopback: PRBS symbols -> linear modulation -> AWGN and a
// carrier offset -> demodulation, with a live constellation, EVM/SNR and BER.
//   ./linear_modem_loopback [--scheme bpsk|qpsk|psk8|qam16|qam64] [--snr <dB>]
//                    [--offset <Hz>] [--rate <S/s>] [--sps <n>] [--screenshot <png>]
// The modulation scheme is fixed at start-up (the modem objects are built once);
// SNR and carrier offset are live sliders.
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/cler_palette.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/linear_modem/ber_counter.hpp"
#include "desktop_blocks/linear_modem/demodulator.hpp"
#include "desktop_blocks/linear_modem/modulator.hpp"
#include "desktop_blocks/linear_modem/plot_constellation.hpp"
#include "desktop_blocks/linear_modem/symbol_source.hpp"
#include "desktop_blocks/noise/awgn.hpp"
#include "desktop_blocks/utils/throttle.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

// Control panel: scheme readout, live SNR and carrier-offset sliders, receiver
// metrics. GUI thread only; talks to the other blocks through their setters.
struct ModemPanel : public cler::BlockBase {
    static constexpr bool is_gui = true;

    ModemPanel(const char* name, const char* scheme_name, unsigned int bps,
               NoiseAWGNBlock<std::complex<float>>& awgn, FrequencyShiftBlock& shift,
               LinearDemodulatorBlock& demod, BERCounterBlock& ber, PlotConstellationBlock& plot,
               float snr_db, float offset_hz, double symbol_rate)
        : cler::BlockBase(name), _scheme_name(scheme_name), _bps(bps), _awgn(awgn),
          _shift(shift), _demod(demod), _ber(ber), _plot(plot), _snr_db(snr_db),
          _offset_hz(offset_hz), _symbol_rate(symbol_rate) {}

    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Error::NotEnoughSamples; }

    void render() {
        using namespace cler::palette;
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 460), ImGuiCond_FirstUseEver);
        ImGui::Begin("Modem");

        ImGui::SeparatorText("Link");
        ImGui::PushStyleColor(ImGuiCol_Text, accent_hi);
        ImGui::SetWindowFontScale(1.8f);
        ImGui::Text("%s", _scheme_name);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%u bit/symbol   %.0f ksym/s   fixed by --scheme", _bps, _symbol_rate / 1e3);

        ImGui::SeparatorText("Channel");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##snr", &_snr_db, 0.0f, 40.0f, "Es/N0 %.1f dB")) {
            _awgn.set_noise_stddev(awgn_stddev_for_esn0_db(_snr_db));
            _ber.reset();
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##offset", &_offset_hz, -5000.0f, 5000.0f, "carrier offset %.0f Hz")) {
            _shift.set_frequency_shift(_offset_hz);
            _ber.reset();
        }

        ImGui::SeparatorText("Receiver");
        const float evm = _demod.evm_percent();
        const float snr = _demod.snr_db();
        const bool locked = _demod.locked();
        _plot.set_metrics(evm, snr, locked);

        ImGui::TextColored(locked ? ok : danger, locked ? "LOCKED" : "NO LOCK");
        ImGui::SameLine(0, 16);
        ImGui::Text("%.1f ksym/s", _demod.symbol_rate() / 1e3);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "EVM %.1f %%", evm);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, evm < 15.0f ? ok : warn);
        ImGui::ProgressBar(std::min(evm / 60.0f, 1.0f), ImVec2(-1, 0), buf);
        ImGui::PopStyleColor();
        ImGui::Text("estimated Es/N0  %.1f dB", snr);

        ImGui::SeparatorText("BER");
        if (_ber.aligned()) {
            ImGui::Text("%.2e", _ber.ber());
            ImGui::TextDisabled("%llu bit errors / %llu bits",
                                (unsigned long long)_ber.bit_errors(), (unsigned long long)_ber.bits());
        } else {
            ImGui::TextDisabled("searching for the reference sequence...");
        }
        if (ImGui::Button("reset BER", ImVec2(-1, 0))) _ber.reset();

        ImGui::End();
    }

private:
    const char* _scheme_name;
    unsigned int _bps;
    NoiseAWGNBlock<std::complex<float>>& _awgn;
    FrequencyShiftBlock& _shift;
    LinearDemodulatorBlock& _demod;
    BERCounterBlock& _ber;
    PlotConstellationBlock& _plot;
    float _snr_db, _offset_hz;
    double _symbol_rate;
};

int main(int argc, char** argv) {
    std::string scheme_name = "qpsk", screenshot;
    float snr_db = 20.0f, offset_hz = 0.0f, beta = 0.35f;
    double sample_rate = 400e3;
    unsigned int sps = 4;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--scheme" && i + 1 < argc) scheme_name = argv[++i];
        else if (arg == "--snr" && i + 1 < argc) snr_db = std::stof(argv[++i]);
        else if (arg == "--offset" && i + 1 < argc) offset_hz = std::stof(argv[++i]);
        else if (arg == "--rate" && i + 1 < argc) sample_rate = std::stod(argv[++i]);
        else if (arg == "--sps" && i + 1 < argc) sps = std::stoul(argv[++i]);
        else if (arg == "--beta" && i + 1 < argc) beta = std::stof(argv[++i]);
        else if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else {
            std::cout << "Usage: " << argv[0] << " [--scheme bpsk|qpsk|psk8|qam16|qam64] [--snr <dB>]\n"
                         "          [--offset <Hz>] [--rate <S/s>] [--sps <n>] [--beta <rolloff>] [--screenshot <png>]\n";
            return arg == "--help" ? 0 : 1;
        }
    }

    const modulation_scheme scheme = liquid_getopt_str2mod(scheme_name.c_str());
    if (scheme == LIQUID_MODEM_UNKNOWN) {
        std::cerr << "linear_modem_loopback: unknown scheme '" << scheme_name << "'\n";
        return 1;
    }

    const unsigned int bps = scheme_bits_per_symbol(scheme);
    SymbolSourceBlock source("PRBS symbols", prbs_symbols(bps, 1023));
    LinearModulatorBlock modulator("Modulator", scheme, sps, beta, 5, 8192);
    ThrottleBlock<std::complex<float>> throttle("Throttle", static_cast<size_t>(sample_rate), 16384);
    NoiseAWGNBlock<std::complex<float>> awgn("AWGN", awgn_stddev_for_esn0_db(snr_db), 16384);
    FrequencyShiftBlock offset("Carrier offset", offset_hz, sample_rate, 16384);
    LinearDemodulatorBlock demodulator("Demodulator", scheme, sps, beta, 5, 0.002f, 0.5f, 16384);
    BERCounterBlock ber("BER", scheme, prbs_symbols(bps, 1023), 4000);
    PlotConstellationBlock constellation("Constellation", 2048, 8192);
    ModemPanel panel("Modem", scheme_name.c_str(), bps, awgn, offset, demodulator, ber,
                     constellation, snr_db, offset_hz, sample_rate / sps);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &modulator.in),
        cler::BlockRunner(&modulator, &throttle.in),
        cler::BlockRunner(&throttle, &awgn.in),
        cler::BlockRunner(&awgn, &offset.in),
        cler::BlockRunner(&offset, &demodulator.in),
        cler::BlockRunner(&demodulator, &ber.in, &constellation.in),
        cler::BlockRunner(&ber),
        cler::BlockRunner(&constellation),
        cler::BlockRunner(&panel));

    cler::GuiManager gui(1280, 720, "cler modem loopback");
    constellation.set_initial_window(400.0f, 10.0f, 700.0f, 700.0f);

    fg.run();
    const auto started = std::chrono::steady_clock::now();
    bool shot_taken = screenshot.empty();
    while (!gui.should_close()) {
        gui.render(fg);
        if (!shot_taken && std::chrono::steady_clock::now() - started > std::chrono::seconds(4)) {
            gui.request_screenshot(screenshot);
            shot_taken = true;
        }
    }
    fg.stop();
    print_flowgraph_execution_report(fg);
    return 0;
}
