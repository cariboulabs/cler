#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <unistd.h>
#include <string>
#include <vector>

#include "cler.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"
#include "desktop_blocks/sigmf/sink_sigmf.hpp"
#include "desktop_blocks/sigmf/source_sigmf.hpp"

namespace {

std::string unique_base() {
    return "/tmp/cler_test_sigmf_" + std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

void remove_recording(const std::string& base) {
    std::remove(sigmf::meta_path(base).c_str());
    std::remove(sigmf::data_path(base).c_str());
}

void write_text(const std::string& path, const std::string& text) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    ASSERT_NE(fp, nullptr);
    std::fwrite(text.data(), 1, text.size(), fp);
    std::fclose(fp);
}

size_t file_size(const std::string& path) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return 0;
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fclose(fp);
    return static_cast<size_t>(size);
}

std::vector<std::complex<float>> complex_tone(size_t n) {
    std::vector<std::complex<float>> samples(n);
    for (size_t i = 0; i < n; ++i) {
        float phase = 2.0f * 3.14159265f * 0.03f * static_cast<float>(i);
        samples[i] = std::complex<float>(0.5f * std::cos(phase), 0.5f * std::sin(phase));
    }
    return samples;
}

// pushes every sample through the sink block, one procedure() call at a time
template <typename T>
void drain(SinkSigMFBlock<T>& sink, const std::vector<T>& samples) {
    size_t pushed = 0;
    while (pushed < samples.size()) {
        size_t room = sink.in.space();
        size_t n = std::min(room, samples.size() - pushed);
        for (size_t i = 0; i < n; ++i) sink.in.push(samples[pushed + i]);
        pushed += n;
        while (sink.procedure().is_ok()) {}
    }
}

template <typename T>
std::vector<T> pull(SourceSigMFBlock<T>& source, size_t max_samples) {
    cler::Channel<T> channel(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T));
    std::vector<T> out;
    size_t idle = 0;
    while (out.size() < max_samples && idle < 4) {
        auto result = source.procedure(&channel);
        while (channel.size() > 0 && out.size() < max_samples) {
            T sample;
            channel.pop(sample);
            out.push_back(sample);
        }
        if (result.is_err()) {
            if (cler::is_fatal(result.unwrap_err())) break;
            idle++;
        } else {
            idle = 0;
        }
    }
    return out;
}

} // namespace

TEST(SigMFMeta, RoundTripsKnownFieldsUnknownKeysAndAnnotations) {
    std::string base = unique_base();
    write_text(sigmf::meta_path(base), R"({
  "global": {
    "core:datatype": "ci16_le",
    "core:sample_rate": 2400000,
    "core:version": "1.0.0",
    "core:author": "cler \"tester\"",
    "core:description": "line one\nline two",
    "core:hw": "HackRF One",
    "vendor:extension": {"nested": [1, 2, {"deep": true}], "flag": false},
    "vendor:count": 7
  },
  "captures": [
    {"core:sample_start": 0, "core:frequency": 107500000, "core:datetime": "2026-08-20T00:00:00.000Z", "vendor:gain": 32},
    {"core:sample_start": 4096, "core:frequency": 108000000}
  ],
  "annotations": [
    {"core:sample_start": 100, "core:sample_count": 50, "core:label": "burst", "vendor:snr": 12.5}
  ]
})");

    sigmf::Meta meta = sigmf::read_meta(base);
    EXPECT_EQ(meta.datatype, sigmf::Datatype::ci16_le);
    EXPECT_DOUBLE_EQ(meta.sample_rate, 2400000.0);
    EXPECT_EQ(meta.version, "1.0.0");
    EXPECT_EQ(meta.author, "cler \"tester\"");
    EXPECT_EQ(meta.description, "line one\nline two");
    EXPECT_EQ(meta.hw, "HackRF One");
    ASSERT_EQ(meta.extra_global.size(), 2u);
    EXPECT_EQ(meta.extra_global[0].first, "vendor:extension");
    EXPECT_EQ(meta.extra_global[1].second, "7");
    ASSERT_EQ(meta.captures.size(), 2u);
    EXPECT_EQ(meta.captures[0].sample_start, 0u);
    EXPECT_DOUBLE_EQ(meta.captures[0].frequency, 107500000.0);
    EXPECT_EQ(meta.captures[0].datetime, "2026-08-20T00:00:00.000Z");
    ASSERT_EQ(meta.captures[0].extra.size(), 1u);
    EXPECT_EQ(meta.captures[0].extra[0].first, "vendor:gain");
    EXPECT_EQ(meta.captures[1].sample_start, 4096u);
    ASSERT_EQ(meta.annotations.size(), 1u);
    EXPECT_EQ(meta.annotations[0].size(), 4u);
    EXPECT_DOUBLE_EQ(meta.center_frequency(), 107500000.0);

    // writing back and re-reading must preserve everything the first read saw
    std::string second = unique_base() + "_b";
    ASSERT_TRUE(sigmf::write_meta(second, meta));
    sigmf::Meta again = sigmf::read_meta(second);
    EXPECT_EQ(sigmf::to_json(meta), sigmf::to_json(again));
    EXPECT_EQ(again.author, meta.author);
    EXPECT_EQ(again.description, meta.description);
    ASSERT_EQ(again.extra_global.size(), 2u);
    EXPECT_EQ(again.extra_global[0].second, meta.extra_global[0].second);
    ASSERT_EQ(again.annotations.size(), 1u);
    EXPECT_EQ(again.annotations[0][3].first, "vendor:snr");

    remove_recording(base);
    remove_recording(second);
}

TEST(SigMFMeta, DatatypeMappingAndPathDerivation) {
    EXPECT_EQ(sigmf::datatype_size(sigmf::Datatype::cf32_le), 8u);
    EXPECT_EQ(sigmf::datatype_size(sigmf::Datatype::ci16_le), 4u);
    EXPECT_EQ(sigmf::datatype_size(sigmf::Datatype::ci8), 2u);
    EXPECT_EQ(sigmf::datatype_size(sigmf::Datatype::cu8), 2u);
    EXPECT_EQ(sigmf::datatype_size(sigmf::Datatype::rf32_le), 4u);
    EXPECT_EQ(sigmf::datatype_size(sigmf::Datatype::ri16_le), 2u);
    EXPECT_TRUE(sigmf::datatype_is_complex(sigmf::Datatype::cu8));
    EXPECT_FALSE(sigmf::datatype_is_complex(sigmf::Datatype::ri16_le));
    EXPECT_EQ(sigmf::parse_datatype("ci8_le"), sigmf::Datatype::ci8);
    EXPECT_STREQ(sigmf::datatype_name(sigmf::Datatype::rf32_le), "rf32_le");

    EXPECT_EQ(sigmf::base_path("/tmp/cap.sigmf-meta"), "/tmp/cap");
    EXPECT_EQ(sigmf::base_path("/tmp/cap.sigmf-data"), "/tmp/cap");
    EXPECT_EQ(sigmf::base_path("/tmp/cap"), "/tmp/cap");
    EXPECT_EQ(sigmf::meta_path("/tmp/cap.sigmf-data"), "/tmp/cap.sigmf-meta");
    EXPECT_EQ(sigmf::data_path("/tmp/cap.sigmf-meta"), "/tmp/cap.sigmf-data");
}

TEST(SigMFBlocks, ComplexRoundTripPerDatatype) {
    const std::vector<std::complex<float>> reference = complex_tone(5000);
    const sigmf::Datatype types[] = {sigmf::Datatype::cf32_le, sigmf::Datatype::ci16_le,
                                     sigmf::Datatype::ci8, sigmf::Datatype::cu8};
    for (sigmf::Datatype datatype : types) {
        std::string base = unique_base();
        {
            SinkSigMFBlock<std::complex<float>> sink("SigMFSink", base.c_str(), 2.4e6, 107.5e6, datatype);
            drain(sink, reference);
            EXPECT_EQ(sink.samples_written(), reference.size());
        }
        EXPECT_EQ(file_size(sigmf::data_path(base)),
                  reference.size() * sigmf::datatype_size(datatype));

        SourceSigMFBlock<std::complex<float>> source("SigMFSource", base.c_str());
        EXPECT_EQ(source.datatype(), datatype);
        EXPECT_DOUBLE_EQ(source.sample_rate(), 2.4e6);
        EXPECT_DOUBLE_EQ(source.center_frequency(), 107.5e6);
        EXPECT_FALSE(source.meta().captures[0].datetime.empty());

        std::vector<std::complex<float>> read_back = pull(source, reference.size());
        ASSERT_EQ(read_back.size(), reference.size());
        for (size_t i = 0; i < reference.size(); ++i) {
            if (datatype == sigmf::Datatype::cf32_le) {
                EXPECT_EQ(read_back[i], reference[i]) << "sample " << i;
            } else {
                float tolerance = 1.0f / 32768.0f;
                if (datatype == sigmf::Datatype::ci8) tolerance = 1.0f / 128.0f;
                if (datatype == sigmf::Datatype::cu8) tolerance = 1.0f / 127.5f;
                EXPECT_NEAR(read_back[i].real(), reference[i].real(), tolerance) << "sample " << i;
                EXPECT_NEAR(read_back[i].imag(), reference[i].imag(), tolerance) << "sample " << i;
            }
        }
        remove_recording(base);
    }
}

TEST(SigMFBlocks, RealRampRoundTripPerDatatype) {
    std::vector<float> ramp(4096);
    for (size_t i = 0; i < ramp.size(); ++i) {
        ramp[i] = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(ramp.size());
    }
    for (sigmf::Datatype datatype : {sigmf::Datatype::rf32_le, sigmf::Datatype::ri16_le}) {
        std::string base = unique_base();
        {
            SinkSigMFBlock<float> sink("SigMFSink", base.c_str(), 48000.0, 0.0, datatype);
            drain(sink, ramp);
        }
        SourceSigMFBlock<float> source("SigMFSource", base.c_str());
        EXPECT_EQ(source.datatype(), datatype);
        std::vector<float> read_back = pull(source, ramp.size());
        ASSERT_EQ(read_back.size(), ramp.size());
        for (size_t i = 0; i < ramp.size(); ++i) {
            if (datatype == sigmf::Datatype::rf32_le) {
                EXPECT_EQ(read_back[i], ramp[i]) << "sample " << i;
            } else {
                EXPECT_NEAR(read_back[i], ramp[i], 1.0f / 32768.0f) << "sample " << i;
            }
        }
        remove_recording(base);
    }
}

TEST(SigMFBlocks, IntegerDatatypesSurviveExactly) {
    std::string base = unique_base();
    sigmf::Meta meta;
    meta.datatype = sigmf::Datatype::ci8;
    meta.sample_rate = 8e6;
    ASSERT_TRUE(sigmf::write_meta(base, meta));

    std::vector<int8_t> raw;
    for (int value = -128; value < 128; ++value) {
        raw.push_back(static_cast<int8_t>(value));
        raw.push_back(static_cast<int8_t>(-value - 1));
    }
    FILE* fp = std::fopen(sigmf::data_path(base).c_str(), "wb");
    ASSERT_NE(fp, nullptr);
    std::fwrite(raw.data(), 1, raw.size(), fp);
    std::fclose(fp);

    SourceSigMFBlock<std::complex<float>> source("SigMFSource", base.c_str());
    std::vector<std::complex<float>> read_back = pull(source, raw.size() / 2);
    ASSERT_EQ(read_back.size(), raw.size() / 2);
    for (size_t i = 0; i < read_back.size(); ++i) {
        EXPECT_FLOAT_EQ(read_back[i].real(), static_cast<float>(raw[2 * i]) / 128.0f);
        EXPECT_FLOAT_EQ(read_back[i].imag(), static_cast<float>(raw[2 * i + 1]) / 128.0f);
    }
    remove_recording(base);
}

TEST(SigMFBlocks, TruncatedDataFileDropsThePartialSampleAndTerminates) {
    std::string base = unique_base();
    {
        SinkSigMFBlock<std::complex<float>> sink("SigMFSink", base.c_str(), 1e6, 0.0, sigmf::Datatype::ci16_le);
        drain(sink, complex_tone(64));
    }
    // chop the file to 63.5 samples
    std::string data = sigmf::data_path(base);
    ASSERT_EQ(file_size(data), 64u * 4u);
    ASSERT_EQ(truncate(data.c_str(), 63 * 4 + 2), 0);

    SourceSigMFBlock<std::complex<float>> source("SigMFSource", base.c_str());
    cler::Channel<std::complex<float>> channel(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>));
    ASSERT_TRUE(source.procedure(&channel).is_ok());
    EXPECT_EQ(channel.size(), 63u);
    channel.commit_read(channel.size());
    EXPECT_EQ(source.procedure(&channel).unwrap_err(), cler::Error::NotEnoughSamples);
    EXPECT_EQ(source.procedure(&channel).unwrap_err(), cler::Error::TERM_EOFReached);
    remove_recording(base);
}

TEST(SigMFBlocks, RepeatRewindsAtEndOfFile) {
    std::string base = unique_base();
    const std::vector<std::complex<float>> reference = complex_tone(32);
    {
        SinkSigMFBlock<std::complex<float>> sink("SigMFSink", base.c_str(), 1e6, 0.0, sigmf::Datatype::cf32_le);
        drain(sink, reference);
    }
    SourceSigMFBlock<std::complex<float>> source("SigMFSource", base.c_str(), true);
    std::vector<std::complex<float>> read_back = pull(source, reference.size() * 3);
    ASSERT_EQ(read_back.size(), reference.size() * 3);
    for (size_t i = 0; i < read_back.size(); ++i) {
        EXPECT_EQ(read_back[i], reference[i % reference.size()]) << "sample " << i;
    }
    remove_recording(base);
}

TEST(SigMFBlocks, AnnotationsAreRewrittenAtDestruction) {
    std::string base = unique_base();
    {
        SinkSigMFBlock<std::complex<float>> sink("SigMFSink", base.c_str(), 1e6, 433.92e6, sigmf::Datatype::ci16_le);
        sigmf::Meta at_start = sigmf::read_meta(base);
        EXPECT_TRUE(at_start.annotations.empty());
        EXPECT_DOUBLE_EQ(at_start.center_frequency(), 433.92e6);
        drain(sink, complex_tone(128));
        sink.add_annotation(10, 20, "first burst");
        sink.add_annotation(64, 8, "second \"burst\"");
    }
    sigmf::Meta meta = sigmf::read_meta(base);
    ASSERT_EQ(meta.annotations.size(), 2u);
    EXPECT_EQ(meta.annotations[0][0].second, "10");
    EXPECT_EQ(meta.annotations[0][1].second, "20");
    EXPECT_EQ(sigmf::detail::unescape(meta.annotations[1][2].second), "second \"burst\"");
    EXPECT_DOUBLE_EQ(meta.sample_rate, 1e6);
    remove_recording(base);
}

TEST(SigMFMeta, WrapsAnExistingCs8CaptureWithoutTouchingTheSamples) {
    std::string base = unique_base();
    std::string cs8 = base + ".cs8";
    std::vector<int8_t> raw(512);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<int8_t>(i % 251) - 125;
    FILE* fp = std::fopen(cs8.c_str(), "wb");
    ASSERT_NE(fp, nullptr);
    std::fwrite(raw.data(), 1, raw.size(), fp);
    std::fclose(fp);

    // the wrap is metadata only: copy the samples, describe them as ci8
    sigmf::Meta meta;
    meta.datatype = sigmf::Datatype::ci8;
    meta.sample_rate = 2.4e6;
    meta.hw = "HackRF One";
    sigmf::Capture capture;
    capture.frequency = 107.5e6;
    capture.has_frequency = true;
    capture.datetime = sigmf::utc_now();
    meta.captures.push_back(capture);
    ASSERT_TRUE(sigmf::write_meta(base, meta));
    ASSERT_EQ(std::rename(cs8.c_str(), sigmf::data_path(base).c_str()), 0);

    SourceSigMFBlock<std::complex<float>> source("SigMFSource", sigmf::data_path(base).c_str());
    EXPECT_EQ(source.datatype(), sigmf::Datatype::ci8);
    EXPECT_DOUBLE_EQ(source.sample_rate(), 2.4e6);
    EXPECT_DOUBLE_EQ(source.center_frequency(), 107.5e6);
    EXPECT_EQ(source.meta().hw, "HackRF One");
    std::vector<std::complex<float>> read_back = pull(source, raw.size() / 2);
    ASSERT_EQ(read_back.size(), raw.size() / 2);
    EXPECT_FLOAT_EQ(read_back[3].real(), static_cast<float>(raw[6]) / 128.0f);
    remove_recording(base);
}

#include "desktop_blocks/sigmf/recorder_sigmf.hpp"

TEST(SigMFRecorder, StartStopWritesAReadableRecording) {
    const std::string prefix = testing::TempDir() + "/rec";
    SigMFRecorderBlock rec("rec", 48000.0, 1 << 16);

    auto feed = [&](size_t n, float scale) {
        size_t fed = 0;
        while (fed < n) {
            auto [w, ws] = rec.in.write_dbf();
            size_t k = std::min(ws, n - fed);
            for (size_t i = 0; i < k; ++i) {
                const float v = static_cast<float>((fed + i) % 100) / 100.0f * scale;
                w[i] = {v, -v};
            }
            rec.in.commit_write(k);
            fed += k;
            while (rec.procedure().is_ok()) {}
        }
    };

    feed(1000, 1.0f);   // idle: drained, not written
    EXPECT_EQ(rec.samples(), 0u);

    ASSERT_TRUE(rec.start(prefix, 100e6));
    EXPECT_FALSE(rec.start(prefix, 100e6));
    feed(5000, 0.5f);
    EXPECT_EQ(rec.samples(), 5000u);
    const std::string base = rec.base();
    rec.stop();
    feed(1000, 1.0f);
    EXPECT_EQ(rec.samples(), 5000u);

    auto meta = sigmf::read_meta(base + ".sigmf-meta");
    EXPECT_EQ(meta.datatype, sigmf::Datatype::ci16_le);
    EXPECT_DOUBLE_EQ(meta.sample_rate, 48000.0);
    EXPECT_DOUBLE_EQ(meta.center_frequency(), 100e6);

    SourceSigMFBlock<std::complex<float>> src("src", base.c_str(), false, 1 << 16);
    cler::Channel<std::complex<float>> out(1 << 16);
    size_t got = 0;
    float max_err = 0.0f;
    while (src.procedure(&out).is_ok()) {
        auto [r, rs] = out.read_dbf();
        for (size_t i = 0; i < rs; ++i) {
            const float v = static_cast<float>((got + i) % 100) / 100.0f * 0.5f;
            max_err = std::max(max_err, std::abs(r[i].real() - v));
        }
        got += rs;
        out.commit_read(rs);
    }
    EXPECT_EQ(got, 5000u);
    EXPECT_LT(max_err, 2.0f / 32768.0f);
}
