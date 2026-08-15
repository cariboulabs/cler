// The software-mirror path: read_dbf/write_dbf when the OS cannot double-map.
// Compiled with CLER_DISABLE_DOUBLY_MAPPED so it exercises the fallback on every platform.
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "cler_spsc-queue.hpp"

namespace {

constexpr size_t CAPACITY = 4096;   // >= DOUBLY_MAPPED_MIN_SIZE bytes for uint32_t
constexpr uint32_t TOTAL = 3'000'000; // many wraps

TEST(SPSCMirror, FallbackIsMirroredNotMapped) {
    dro::SPSCQueue<uint32_t> queue(CAPACITY);
    EXPECT_FALSE(queue.is_doubly_mapped());
    auto [ptr, space] = queue.write_dbf();
    EXPECT_NE(ptr, nullptr);
    EXPECT_GT(space, 0u);
}

TEST(SPSCMirror, DbfWriterDbfReaderKeepsOrderAcrossWraps) {
    dro::SPSCQueue<uint32_t> queue(CAPACITY);
    std::thread producer([&] {
        uint32_t next = 0;
        while (next < TOTAL) {
            auto [ptr, space] = queue.write_dbf();
            if (space == 0) continue;
            // odd chunk sizes so the wrap lands everywhere
            size_t n = std::min<size_t>(space, 1 + (next % 997));
            n = std::min<size_t>(n, TOTAL - next);
            for (size_t i = 0; i < n; ++i) ptr[i] = next + i;
            queue.commit_write(n);
            next += n;
        }
    });
    uint32_t expect = 0;
    while (expect < TOTAL) {
        auto [ptr, avail] = queue.read_dbf();
        if (avail == 0) continue;
        size_t n = std::min<size_t>(avail, 1 + (expect % 613));
        for (size_t i = 0; i < n; ++i) ASSERT_EQ(ptr[i], expect + i) << "at " << expect + i;
        queue.commit_read(n);
        expect += n;
    }
    producer.join();
}

TEST(SPSCMirror, DbfWriterCopyReaderAndCopyWriterDbfReader) {
    dro::SPSCQueue<uint32_t> queue(CAPACITY);
    std::vector<uint32_t> tmp(1000);
    // dbf writer -> readN reader
    std::thread producer([&] {
        uint32_t next = 0;
        while (next < TOTAL) {
            auto [ptr, space] = queue.write_dbf();
            if (space == 0) continue;
            size_t n = std::min<size_t>({space, size_t(1 + (next % 771)), size_t(TOTAL - next)});
            for (size_t i = 0; i < n; ++i) ptr[i] = next + i;
            queue.commit_write(n);
            next += n;
        }
    });
    uint32_t expect = 0;
    while (expect < TOTAL) {
        size_t n = std::min<size_t>({tmp.size(), size_t(1 + (expect % 333)), size_t(TOTAL - expect)});
        size_t got = queue.readN(tmp.data(), n);
        for (size_t i = 0; i < got; ++i) ASSERT_EQ(tmp[i], expect + i);
        expect += got;
    }
    producer.join();

    // writeN writer -> dbf reader
    dro::SPSCQueue<uint32_t> queue2(CAPACITY);
    std::thread producer2([&] {
        uint32_t next = 0;
        std::vector<uint32_t> out(1000);
        while (next < TOTAL) {
            size_t n = std::min<size_t>({out.size(), size_t(1 + (next % 555)), size_t(TOTAL - next)});
            for (size_t i = 0; i < n; ++i) out[i] = next + i;
            size_t put = queue2.writeN(out.data(), n);
            next += put;
        }
    });
    expect = 0;
    while (expect < TOTAL) {
        auto [ptr, avail] = queue2.read_dbf();
        if (avail == 0) continue;
        for (size_t i = 0; i < avail; ++i) ASSERT_EQ(ptr[i], expect + i);
        queue2.commit_read(avail);
        expect += avail;
    }
    producer2.join();
}

} // namespace

TEST(SPSCMirror, AbandonedDbfSpanDoesNotClobberWrappedPeekWrite) {
    dro::SPSCQueue<uint32_t> queue(CAPACITY);
    const size_t cap = queue.capacity();
    std::vector<uint32_t> tmp(cap);
    // park the write index near the end
    for (size_t i = 0; i < cap - 5; ++i) queue.push(0u);
    queue.readN(tmp.data(), cap - 5);
    // a dbf span is taken and abandoned (block bailed), then a wrapped peek_write is committed
    (void)queue.write_dbf();
    uint32_t* p1; size_t s1; uint32_t* p2; size_t s2;
    size_t got = queue.peek_write(p1, s1, p2, s2);
    ASSERT_GE(got, 20u);
    for (size_t i = 0; i < 20; ++i) (i < s1 ? p1[i] : p2[i - s1]) = 1000 + i;
    queue.commit_write(20);
    ASSERT_EQ(queue.readN(tmp.data(), 20), 20u);
    for (size_t i = 0; i < 20; ++i) EXPECT_EQ(tmp[i], 1000 + i) << "idx " << i;
}

TEST(SPSCMirror, MoveOnlyTypeStillCompiles) {
    dro::SPSCQueue<std::unique_ptr<int>> queue(8);
    std::unique_ptr<int>* p1; size_t s1; std::unique_ptr<int>* p2; size_t s2;
    ASSERT_GE(queue.peek_write(p1, s1, p2, s2), 1u);
    p1[0] = std::make_unique<int>(7);
    queue.commit_write(1);
    std::unique_ptr<int> out;
    ASSERT_TRUE(queue.try_pop(out));
    EXPECT_EQ(*out, 7);
}
