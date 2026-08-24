#include <gtest/gtest.h>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "cler.hpp"
#include "desktop_examples/aprs_receiver/aprs_source.hpp"

// The shared wrapper: the simulator is constructed in place from the forwarded
// args (these blocks own liquid handles and must not be moved), and a cs8
// capture replays through the same seam.
TEST(PacedSelectableSource, SimAndFileFeedTheSameChannel) {
    cler::Channel<std::complex<float>> out(1 << 18);

    APRSSourceBlock sim("src", APRSSourceBlock::Kind::Sim, "", 144.8e6, 2.4e6, 0, 0, false,
                        "Sim stations", 2.4e6, -250e3, 3e3, size_t{1} << 18);
    EXPECT_EQ(sim.kind(), APRSSourceBlock::Kind::Sim);
    EXPECT_STREQ(sim.kind_name(), "simulation");
    ASSERT_TRUE(sim.procedure(&out).is_ok());
    EXPECT_GT(out.size(), 0u);
    out.commit_read(out.size());

    const std::string path = std::string(testing::TempDir()) + "/paced_source.cs8";
    {
        std::vector<int8_t> iq(2048);
        for (size_t i = 0; i < iq.size(); i += 2) { iq[i] = 64; iq[i + 1] = -64; }
        FILE* f = std::fopen(path.c_str(), "wb");
        ASSERT_NE(f, nullptr);
        std::fwrite(iq.data(), 1, iq.size(), f);
        std::fclose(f);
    }
    APRSSourceBlock file("src", APRSSourceBlock::Kind::File, path, 144.8e6, 2.4e6, 0, 0, false,
                         "unused", 2.4e6, -250e3, 3e3, size_t{1} << 18);
    EXPECT_STREQ(file.kind_name(), "file");
    ASSERT_TRUE(file.procedure(&out).is_ok());
    auto [p, n] = out.read_dbf();
    ASSERT_GT(n, 0u);
    EXPECT_NEAR(p[0].real(), 0.5f, 1e-6);
    EXPECT_NEAR(p[0].imag(), -0.5f, 1e-6);
    out.commit_read(n);
    std::remove(path.c_str());
}
