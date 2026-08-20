#include <gtest/gtest.h>
#include <complex>
#include <cstring>
#include <random>
#include <vector>
#include "cler.hpp"
#include "desktop_blocks/fec/deframer.hpp"
#include "desktop_blocks/fec/fec_decoder.hpp"
#include "desktop_blocks/fec/fec_encoder.hpp"
#include "desktop_blocks/fec/framer.hpp"
#include "desktop_blocks/noise/awgn.hpp"

namespace {

constexpr size_t PAYLOAD_BYTES = 32;

// Schemes liquid compiles unconditionally. The convolutional and Reed-Solomon
// codecs need libfec, which this build does not have, so they are probed rather
// than assumed.
const fec_scheme kBlockSchemes[] = {
    LIQUID_FEC_NONE, LIQUID_FEC_REP3, LIQUID_FEC_REP5,
    LIQUID_FEC_HAMMING74, LIQUID_FEC_HAMMING84, LIQUID_FEC_HAMMING128,
    LIQUID_FEC_GOLAY2412,
    LIQUID_FEC_SECDED2216, LIQUID_FEC_SECDED3932, LIQUID_FEC_SECDED7264,
};

std::vector<uint8_t> random_bytes(size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = static_cast<uint8_t>(rng() & 0xFF);
    return v;
}

// Encodes one block through FECEncoderBlock and returns the coded bytes.
std::vector<uint8_t> encode_block(fec_scheme scheme, const std::vector<uint8_t>& payload) {
    FECEncoderBlock enc("enc", payload.size(), scheme);
    cler::Channel<uint8_t> out(4 * enc.encoded_bytes());
    enc.in.writeN(payload.data(), payload.size());
    EXPECT_TRUE(enc.procedure(&out).is_ok());
    std::vector<uint8_t> coded(enc.encoded_bytes());
    out.readN(coded.data(), coded.size());
    return coded;
}

std::vector<uint8_t> decode_block(fec_scheme scheme, const std::vector<uint8_t>& coded) {
    FECDecoderBlock dec("dec", PAYLOAD_BYTES, scheme, 4 * coded.size());
    cler::Channel<uint8_t> out(4 * PAYLOAD_BYTES);
    dec.in.writeN(coded.data(), coded.size());
    EXPECT_TRUE(dec.procedure(&out).is_ok());
    std::vector<uint8_t> payload(PAYLOAD_BYTES);
    out.readN(payload.data(), payload.size());
    return payload;
}

TEST(FECBlocks, RoundTripEveryAvailableScheme) {
    const auto payload = random_bytes(PAYLOAD_BYTES, 7);
    for (fec_scheme scheme : kBlockSchemes) {
        ASSERT_TRUE(fec_scheme_available(scheme)) << fec_scheme_str[scheme][0];
        const auto coded = encode_block(scheme, payload);
        EXPECT_EQ(coded.size(), fec_get_enc_msg_length(scheme, PAYLOAD_BYTES));
        EXPECT_EQ(decode_block(scheme, coded), payload) << fec_scheme_str[scheme][0];
    }
}

TEST(FECBlocks, ConvolutionalAndReedSolomonRoundTripWhenLibfecIsPresent) {
    // liquid only compiles these codecs against libfec; without it fec_create()
    // returns NULL and the blocks panic rather than silently degrade.
    for (fec_scheme scheme : {LIQUID_FEC_CONV_V27, LIQUID_FEC_RS_M8}) {
        if (!fec_scheme_available(scheme)) {
            GTEST_SKIP() << "no libfec in this liquid build";
        }
        const auto payload = random_bytes(PAYLOAD_BYTES, 43);
        EXPECT_EQ(decode_block(scheme, encode_block(scheme, payload)), payload);
    }
}

TEST(FECBlocks, SingleBitErrorIsCorrected) {
    const auto payload = random_bytes(PAYLOAD_BYTES, 11);
    for (fec_scheme scheme : kBlockSchemes) {
        if (scheme == LIQUID_FEC_NONE) continue;
        auto coded = encode_block(scheme, payload);
        // One flipped bit is inside the correction capability of every one of
        // these codes (majority vote, Hamming, Golay, SEC-DED).
        coded[coded.size() / 2] ^= 0x04;
        EXPECT_EQ(decode_block(scheme, coded), payload) << fec_scheme_str[scheme][0];
    }
}

TEST(FECBlocks, ErrorsBeyondCapabilityChangeThePayload) {
    // liquid's fec_decode() reports no uncorrectable-error flag for the block
    // codes, so all that can be asserted is that the payload comes out wrong.
    const auto payload = random_bytes(PAYLOAD_BYTES, 13);
    for (fec_scheme scheme : kBlockSchemes) {
        if (scheme == LIQUID_FEC_NONE) continue;
        auto coded = encode_block(scheme, payload);
        for (size_t i = 0; i < coded.size(); ++i) coded[i] ^= 0xFF;
        EXPECT_NE(decode_block(scheme, coded), payload) << fec_scheme_str[scheme][0];
    }
}

TEST(FECBlocks, EncoderHonoursProgressContractUnderBackpressure) {
    FECEncoderBlock enc("enc", PAYLOAD_BYTES, LIQUID_FEC_HAMMING128);
    const auto payload = random_bytes(2 * PAYLOAD_BYTES, 17);
    enc.in.writeN(payload.data(), payload.size());

    // One byte short of a coded block: nothing may be consumed.
    cler::Channel<uint8_t> tight(enc.encoded_bytes() - 1);
    EXPECT_FALSE(enc.procedure(&tight).is_ok());
    EXPECT_EQ(enc.in.size(), payload.size());
    EXPECT_EQ(tight.size(), 0u);

    // Room for exactly one coded block: exactly one payload block leaves.
    cler::Channel<uint8_t> one(enc.encoded_bytes());
    EXPECT_TRUE(enc.procedure(&one).is_ok());
    EXPECT_EQ(enc.in.size(), PAYLOAD_BYTES);
    EXPECT_EQ(one.size(), enc.encoded_bytes());
}

TEST(FECBlocks, DecoderHonoursProgressContractUnderBackpressure) {
    FECDecoderBlock dec("dec", PAYLOAD_BYTES, LIQUID_FEC_HAMMING128);
    const auto coded = encode_block(LIQUID_FEC_HAMMING128, random_bytes(PAYLOAD_BYTES, 19));
    dec.in.writeN(coded.data(), coded.size());

    cler::Channel<uint8_t> tight(PAYLOAD_BYTES - 1);
    EXPECT_FALSE(dec.procedure(&tight).is_ok());
    EXPECT_EQ(dec.in.size(), coded.size());

    // A partial coded block on the input is likewise not consumed.
    FECDecoderBlock partial("partial", PAYLOAD_BYTES, LIQUID_FEC_HAMMING128);
    partial.in.writeN(coded.data(), coded.size() - 1);
    cler::Channel<uint8_t> roomy(4 * PAYLOAD_BYTES);
    EXPECT_FALSE(partial.procedure(&roomy).is_ok());
    EXPECT_EQ(partial.in.size(), coded.size() - 1);
}

// Drives framer -> (optional noise) -> deframer by hand and returns the
// recovered byte stream.
std::vector<uint8_t> loopback(const std::vector<uint8_t>& packets, size_t packet_bytes,
                              float noise_stddev, size_t out_capacity) {
    PacketFramerBlock framer("framer", packet_bytes, LIQUID_MODEM_QPSK, LIQUID_CRC_32,
                             LIQUID_FEC_NONE, LIQUID_FEC_HAMMING128, 8 * packet_bytes);
    NoiseAWGNBlock<std::complex<float>> awgn("awgn", noise_stddev, 8192);
    PacketDeframerBlock deframer("deframer", packet_bytes, 64, 8192);
    cler::Channel<uint8_t> out(out_capacity);

    std::vector<uint8_t> received;
    size_t sent = 0;
    // Two trailing packets flush the last real frame past the synchronizer's
    // filter delay; their payloads are recovered too and simply ignored.
    const size_t total = packets.size() + 2 * packet_bytes;
    std::vector<uint8_t> padded = packets;
    padded.resize(total, 0x5A);

    for (int i = 0; i < 20000; ++i) {
        while (sent < total && framer.in.space() >= packet_bytes) {
            framer.in.writeN(padded.data() + sent, packet_bytes);
            sent += packet_bytes;
        }
        framer.procedure(&awgn.in);
        awgn.procedure(&deframer.in);
        deframer.procedure(&out);
        while (out.size() > 0) {
            uint8_t b;
            out.pop(b);
            received.push_back(b);
        }
        if (sent == total && framer.in.size() == 0 && awgn.in.size() == 0 && deframer.in.size() == 0) {
            break;
        }
    }
    received.resize(std::min(received.size(), packets.size()));
    return received;
}

TEST(FramingBlocks, CleanLoopbackRecoversEveryPacketByteExact) {
    constexpr size_t PACKET_BYTES = 48;
    constexpr size_t NUM_PACKETS = 20;
    const auto packets = random_bytes(NUM_PACKETS * PACKET_BYTES, 23);
    EXPECT_EQ(loopback(packets, PACKET_BYTES, 0.0f, 8192), packets);
}

TEST(FramingBlocks, NoisyLoopbackRecoversMostPackets) {
    constexpr size_t PACKET_BYTES = 48;
    constexpr size_t NUM_PACKETS = 40;
    const auto packets = random_bytes(NUM_PACKETS * PACKET_BYTES, 29);
    // Frame samples have unit mean power; 12 dB SNR is generous for QPSK with a
    // coded header and payload.
    const float stddev = std::sqrt(0.5f * std::pow(10.0f, -12.0f / 10.0f));
    const auto received = loopback(packets, PACKET_BYTES, stddev, 8192);

    size_t good = 0;
    for (size_t p = 0; p * PACKET_BYTES + PACKET_BYTES <= received.size(); ++p) {
        if (std::memcmp(received.data() + p * PACKET_BYTES,
                        packets.data() + p * PACKET_BYTES, PACKET_BYTES) == 0) {
            ++good;
        }
    }
    EXPECT_GE(good, (NUM_PACKETS * 9) / 10) << "recovered " << good << " of " << NUM_PACKETS;
}

TEST(FramingBlocks, SmallOutputChannelStillRecoversEveryPacket) {
    constexpr size_t PACKET_BYTES = 48;
    constexpr size_t NUM_PACKETS = 20;
    const auto packets = random_bytes(NUM_PACKETS * PACKET_BYTES, 31);
    // Output holds a single packet, so the deframer must stage and drain rather
    // than lose payloads the callback delivers.
    EXPECT_EQ(loopback(packets, PACKET_BYTES, 0.0f, PACKET_BYTES), packets);
}

TEST(FramingBlocks, FramerNeverSplitsAFrameUnderBackpressure) {
    constexpr size_t PACKET_BYTES = 48;
    PacketFramerBlock framer("framer", PACKET_BYTES);
    const auto packet = random_bytes(PACKET_BYTES, 37);
    framer.in.writeN(packet.data(), packet.size());

    // No output space at all: the packet stays put and no progress is claimed.
    cler::Channel<std::complex<float>> full(64);
    std::vector<std::complex<float>> fill(64);
    full.writeN(fill.data(), fill.size());
    EXPECT_FALSE(framer.procedure(&full).is_ok());
    EXPECT_EQ(framer.in.size(), PACKET_BYTES);

    // A 32-sample trickle still emits exactly one whole frame.
    cler::Channel<std::complex<float>> trickle(32);
    size_t emitted = 0;
    for (int i = 0; i < 10000; ++i) {
        framer.procedure(&trickle);
        while (trickle.size() > 0) {
            std::complex<float> s;
            trickle.pop(s);
            ++emitted;
        }
        if (framer.frame_samples() > 0 && emitted == framer.frame_samples()) break;
    }
    EXPECT_EQ(framer.in.size(), 0u);
    EXPECT_GT(framer.frame_samples(), 0u);
    EXPECT_EQ(emitted, framer.frame_samples());
}

TEST(FramingBlocks, CorruptFrameCountsAsDetectedButNotValid) {
    constexpr size_t PACKET_BYTES = 48;
    PacketFramerBlock framer("framer", PACKET_BYTES);
    PacketDeframerBlock deframer("deframer", PACKET_BYTES, 64, 8192);
    cler::Channel<uint8_t> out(4 * PACKET_BYTES);
    const auto packet = random_bytes(PACKET_BYTES, 41);
    framer.in.writeN(packet.data(), packet.size());

    // Heavy noise: the preamble still correlates but neither header nor payload
    // survives, so no payload reaches the output.
    NoiseAWGNBlock<std::complex<float>> awgn("awgn", 1.0f, 8192);
    for (int i = 0; i < 4000 && framer.in.size() + awgn.in.size() + deframer.in.size() > 0; ++i) {
        framer.procedure(&awgn.in);
        awgn.procedure(&deframer.in);
        deframer.procedure(&out);
    }
    EXPECT_EQ(out.size(), 0u);
    EXPECT_EQ(deframer.payloads_valid(), 0u);
}

} // namespace
