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

// The GUI sliders reach the DSP thread through set_noise_stddev()/
// set_frequency_shift(), applied at the top of the next procedure(). Nothing
// else in the suite exercises that deferred-apply path.
TEST(ModemBlocks, AwgnStddevSetterTakesEffect) {
    NoiseAWGNBlock<std::complex<float>> awgn("awgn", 0.0f, 16384);
    cler::Channel<std::complex<float>> out(16384);

    auto measure = [&]() {
        for (size_t i = 0; i < 8192; ++i) awgn.in.push({0.0f, 0.0f});
        awgn.procedure(&out);
        double acc = 0.0;
        size_t n = out.size();
        for (size_t i = 0; i < n; ++i) {
            std::complex<float> v;
            out.pop(v);
            acc += std::norm(v);
        }
        return acc / static_cast<double>(n);  // complex variance = 2*sigma^2
    };

    EXPECT_LT(measure(), 1e-12);
    awgn.set_noise_stddev(0.5f);
    EXPECT_NEAR(measure(), 0.5, 0.05);
}

TEST(ModemBlocks, FrequencyShiftSetterTakesEffect) {
    FrequencyShiftBlock shift("shift", 0.0, 1000.0, 16384);
    cler::Channel<std::complex<float>> out(16384);

    auto phase_step = [&]() {
        for (size_t i = 0; i < 8; ++i) shift.in.push({1.0f, 0.0f});
        shift.procedure(&out);
        std::complex<float> a, b;
        out.pop(a);
        out.pop(b);
        while (out.size()) { std::complex<float> junk; out.pop(junk); }
        return std::arg(b * std::conj(a));
    };

    EXPECT_NEAR(phase_step(), 0.0f, 1e-5f);
    shift.set_frequency_shift(125.0);  // fs/8 -> pi/4 per sample
    EXPECT_NEAR(phase_step(), static_cast<float>(M_PI) / 4.0f, 1e-4f);
}

// Pins the SNR convention: QPSK at Es/N0 = 7 dB has BER ~1.3e-2, while the same
// number read as Eb/N0 would give ~8e-4. The band separates the two.
TEST(ModemBlocks, QpskBerMatchesEsN0Theory) {
    Loopback lb(LIQUID_MODEM_QPSK, 7.0f, 0.0, 20000);
    for (int i = 0; i < 2000; ++i) lb.step();
    ASSERT_TRUE(lb.ber.aligned());
    ASSERT_GT(lb.ber.bits(), 200000u);
    EXPECT_GT(lb.ber.ber(), 5.0e-3);
    EXPECT_LT(lb.ber.ber(), 4.0e-2);
}

// A carrier-loop cycle slip must not pin the reported BER at ~0.5 for the rest
// of the run: alignment is dropped and re-acquired against the new rotation.
TEST(ModemBlocks, BerCounterRecoversFromAConstellationSlip) {
    std::vector<uint8_t> ref = prbs_symbols(2, REF_SYMBOLS);
    BERCounterBlock ber("ber", LIQUID_MODEM_QPSK, ref, 0);

    size_t pos = 0;
    bool went_unaligned = false;
    auto feed = [&](size_t n, bool slipped) {
        for (size_t i = 0; i < n; ++i) {
            uint8_t s = ref[pos];
            if (++pos == ref.size()) pos = 0;
            ber.in.push(slipped ? static_cast<uint8_t>(s ^ 3u) : s);
            if (ber.in.size() == 256) {
                ber.procedure();
                if (!ber.aligned()) went_unaligned = true;
            }
        }
        ber.procedure();
    };

    feed(4096, false);
    ASSERT_TRUE(ber.aligned());
    ASSERT_GT(ber.bits(), 0u);
    ASSERT_EQ(ber.bit_errors(), 0u);

    // A 180-degree slip: every symbol now decodes to its complement.
    feed(40960, true);
    EXPECT_TRUE(went_unaligned) << "the slip never cost alignment";
    EXPECT_TRUE(ber.aligned());
    EXPECT_LT(ber.ber(), 0.01) << "stale rotation still being counted";
}
