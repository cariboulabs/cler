#include <gtest/gtest.h>
#include <complex>
#include <vector>
#include <cstdint>

#include "desktop_blocks/sinks/sink_hackrf.hpp"

TEST(HackRFPackTest, MidScaleAndZeroMapExactly) {
    std::vector<std::complex<float>> src = {
        {0.0f, 0.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {0.5f, -0.5f}
    };
    std::vector<uint8_t> dst(2 * src.size(), 0xAA);

    hackrf_pack_iq(src.data(), dst.data(), src.size());

    EXPECT_EQ(static_cast<int8_t>(dst[0]), 0);
    EXPECT_EQ(static_cast<int8_t>(dst[1]), 0);

    EXPECT_EQ(static_cast<int8_t>(dst[2]), 127);
    EXPECT_EQ(static_cast<int8_t>(dst[3]), -127);

    EXPECT_EQ(static_cast<int8_t>(dst[4]), -127);
    EXPECT_EQ(static_cast<int8_t>(dst[5]), 127);

    EXPECT_EQ(static_cast<int8_t>(dst[6]), 63);
    EXPECT_EQ(static_cast<int8_t>(dst[7]), -63);
}

TEST(HackRFPackTest, OutOfRangeInputIsClampedNotWrapped) {
    std::vector<std::complex<float>> src = {
        {5.0f, -5.0f}, {1e9f, -1e9f}, {1.0001f, -1.0001f}
    };
    std::vector<uint8_t> dst(2 * src.size(), 0);

    hackrf_pack_iq(src.data(), dst.data(), src.size());

    for (size_t i = 0; i < src.size(); ++i) {
        EXPECT_EQ(static_cast<int8_t>(dst[2 * i]), 127)
            << "sample " << i << " I wrapped instead of clamping";
        EXPECT_EQ(static_cast<int8_t>(dst[2 * i + 1]), -127)
            << "sample " << i << " Q wrapped instead of clamping";
    }
}

TEST(HackRFPackTest, InterleavingIsIThenQPerSample) {
    std::vector<std::complex<float>> src;
    for (int i = 0; i < 8; ++i) {
        src.push_back({static_cast<float>(i) / 8.0f, -static_cast<float>(i) / 8.0f});
    }
    std::vector<uint8_t> dst(2 * src.size(), 0);

    hackrf_pack_iq(src.data(), dst.data(), src.size());

    for (size_t i = 0; i < src.size(); ++i) {
        const int8_t expected_i = static_cast<int8_t>(src[i].real() * 127.0f);
        const int8_t expected_q = static_cast<int8_t>(src[i].imag() * 127.0f);
        EXPECT_EQ(static_cast<int8_t>(dst[2 * i]), expected_i) << "at " << i;
        EXPECT_EQ(static_cast<int8_t>(dst[2 * i + 1]), expected_q) << "at " << i;
    }
}

TEST(HackRFPackTest, WritesExactlyTwoBytesPerSampleAndNoMore) {
    std::vector<std::complex<float>> src(4, {1.0f, 1.0f});
    std::vector<uint8_t> dst(2 * src.size() + 4, 0x5A);

    hackrf_pack_iq(src.data(), dst.data(), src.size());

    for (size_t i = 2 * src.size(); i < dst.size(); ++i) {
        EXPECT_EQ(dst[i], 0x5A) << "wrote past 2*n bytes at index " << i;
    }
}
