#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <vector>
#include "cler.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/modem/ber_counter.hpp"
#include "desktop_blocks/modem/demodulator.hpp"
#include "desktop_blocks/modem/modulator.hpp"
#include "desktop_blocks/modem/symbol_source.hpp"
#include "desktop_blocks/noise/awgn.hpp"

namespace {

constexpr unsigned int SPS = 4;
constexpr float BETA = 0.35f;
constexpr size_t REF_SYMBOLS = 1023;

struct Loopback {
    Loopback(modulation_scheme scheme, float snr_db, double freq_offset_hz, size_t skip_symbols)
        : ref(prbs_symbols(scheme_bits_per_symbol(scheme), REF_SYMBOLS)),
          src("src", ref),
          mod("mod", scheme, SPS, BETA, 5, 4096),
          awgn("awgn", awgn_stddev_for_esn0_db(snr_db), 16384),
          shift("shift", freq_offset_hz, 1.0e6, 16384),
          demod("demod", scheme, SPS, BETA, 5, 0.002f, 0.5f, 16384),
          ber("ber", scheme, ref, skip_symbols),
          constellation(8192) {}

    // One pass of the hand-driven pipeline; returns constellation points seen.
    size_t step() {
        src.procedure(&mod.in);
        mod.procedure(&awgn.in);
        awgn.procedure(&shift.in);
        shift.procedure(&demod.in);
        demod.procedure(&ber.in, &constellation);
        const size_t points = constellation.size();
        for (size_t i = 0; i < points; ++i) {
            std::complex<float> p;
            constellation.pop(p);
        }
        ber.procedure();
        return points;
    }

    std::vector<uint8_t> ref;
    SymbolSourceBlock src;
    ModulatorBlock mod;
    NoiseAWGNBlock<std::complex<float>> awgn;
    FrequencyShiftBlock shift;
    DemodulatorBlock demod;
    BERCounterBlock ber;
    cler::Channel<std::complex<float>> constellation;
};

const modulation_scheme kSchemes[] = {
    LIQUID_MODEM_BPSK, LIQUID_MODEM_QPSK, LIQUID_MODEM_PSK8,
    LIQUID_MODEM_QAM16, LIQUID_MODEM_QAM64,
};

const char* kNames[] = {"BPSK", "QPSK", "8PSK", "16QAM", "64QAM"};

}  // namespace

// A clean channel must decode with no bit errors once the receiver has settled.
TEST(ModemBlocks, LoopbackIsErrorFreeAtHighSnr) {
    for (size_t s = 0; s < std::size(kSchemes); ++s) {
        Loopback lb(kSchemes[s], 35.0f, 0.0, 20000);
        for (int i = 0; i < 500; ++i) lb.step();
        EXPECT_TRUE(lb.ber.aligned()) << kNames[s] << " never aligned";
        EXPECT_GT(lb.ber.bits(), 100000u) << kNames[s];
        EXPECT_EQ(lb.ber.bit_errors(), 0u) << kNames[s] << " BER " << lb.ber.ber();
        EXPECT_TRUE(lb.demod.locked()) << kNames[s];
    }
}

// A carrier offset is pulled in by the decision-directed PLL; the BER block's
// rotation search absorbs the residual constellation ambiguity.
TEST(ModemBlocks, QpskTracksCarrierOffset) {
    Loopback lb(LIQUID_MODEM_QPSK, 30.0f, 2000.0, 20000);
    for (int i = 0; i < 600; ++i) lb.step();
    EXPECT_TRUE(lb.ber.aligned());
    EXPECT_GT(lb.ber.bits(), 100000u);
    EXPECT_EQ(lb.ber.bit_errors(), 0u) << "BER " << lb.ber.ber();
}

// EVM tracks Es/N0: EVM_rms = 10^(-snr/20). Asserted as a band, since the
// symbol-rate power normalisation also scales by the noise it cannot separate.
TEST(ModemBlocks, EvmTracksSnr) {
    for (float snr_db : {20.0f, 8.0f}) {
        Loopback lb(LIQUID_MODEM_QPSK, snr_db, 0.0, 0);
        for (int i = 0; i < 400; ++i) lb.step();
        const float expected = 100.0f * std::pow(10.0f, -snr_db / 20.0f);
        const float measured = lb.demod.evm_percent();
        EXPECT_GT(measured, 0.5f * expected) << "snr " << snr_db;
        EXPECT_LT(measured, 2.0f * expected) << "snr " << snr_db;
        EXPECT_NEAR(lb.demod.snr_db(), snr_db, 6.0f);
    }
}

// One constellation point per demodulated symbol.
TEST(ModemBlocks, ConstellationCountMatchesSymbols) {
    std::vector<uint8_t> ref = prbs_symbols(2, REF_SYMBOLS);
    SymbolSourceBlock src("src", ref);
    ModulatorBlock mod("mod", LIQUID_MODEM_QPSK, SPS, BETA, 5, 4096);
    DemodulatorBlock demod("demod", LIQUID_MODEM_QPSK, SPS, BETA, 5, 0.002f, 0.5f, 16384);
    cler::Channel<uint8_t> symbols(8192);
    cler::Channel<std::complex<float>> constellation(8192);

    size_t n_symbols = 0, n_points = 0;
    for (int i = 0; i < 100; ++i) {
        src.procedure(&mod.in);
        mod.procedure(&demod.in);
        demod.procedure(&symbols, &constellation);
        n_symbols += symbols.size();
        n_points += constellation.size();
        while (symbols.size()) { uint8_t s; symbols.pop(s); }
        while (constellation.size()) { std::complex<float> p; constellation.pop(p); }
    }
    EXPECT_GT(n_symbols, 1000u);
    EXPECT_EQ(n_symbols, n_points);
}
