#include <gtest/gtest.h>

#include "desktop_examples/websdr/recordings.hpp"
#include "desktop_examples/websdr/watchdog.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string temp_dir(const char* tag) {
    const auto dir = fs::temp_directory_path() / (std::string("websdr_ops_") + tag + "_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir.string();
}

// A pair the pruner recognises: real meta JSON plus `bytes` of data.
void make_recording(const std::string& dir, const std::string& name, size_t bytes,
                    std::chrono::seconds age = std::chrono::seconds(0)) {
    const std::string base = dir + "/" + name;
    sigmf::Meta meta;
    meta.datatype = sigmf::Datatype::ci16_le;
    meta.sample_rate = 1e6;
    ASSERT_TRUE(sigmf::write_meta(base + ".sigmf-meta", meta));
    std::ofstream data(base + ".sigmf-data", std::ios::binary);
    std::vector<char> zeros(bytes, 0);
    data.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    data.close();
    if (age.count()) {
        const auto when = fs::last_write_time(base + ".sigmf-meta") - age;
        fs::last_write_time(base + ".sigmf-meta", when);
    }
}

bool exists(const std::string& dir, const std::string& name) {
    return fs::exists(dir + "/" + name + ".sigmf-data") && fs::exists(dir + "/" + name + ".sigmf-meta");
}

}  // namespace

TEST(WebsdrRecordings, PrunesOldestPairsUntilUnderTheCap) {
    const std::string dir = temp_dir("cap");
    make_recording(dir, "old", 400 * 1024, std::chrono::seconds(300));
    make_recording(dir, "mid", 400 * 1024, std::chrono::seconds(200));
    make_recording(dir, "new", 400 * 1024, std::chrono::seconds(100));

    const auto p = websdr::prune_recordings(dir, 900 * 1024, 0, "");

    EXPECT_EQ(p.recordings, 1u);
    EXPECT_GE(p.bytes, 400u * 1024u);
    EXPECT_FALSE(exists(dir, "old"));
    EXPECT_TRUE(exists(dir, "mid"));
    EXPECT_TRUE(exists(dir, "new"));
    // both halves of the pair go, never a lone meta
    EXPECT_FALSE(fs::exists(dir + "/old.sigmf-meta"));
    fs::remove_all(dir);
}

TEST(WebsdrRecordings, NeverPrunesTheRecordingBeingWritten) {
    const std::string dir = temp_dir("keep");
    make_recording(dir, "inprogress", 800 * 1024, std::chrono::seconds(600));   // oldest on purpose
    make_recording(dir, "done", 400 * 1024, std::chrono::seconds(100));

    const auto p = websdr::prune_recordings(dir, 100 * 1024, 0, dir + "/inprogress");

    EXPECT_EQ(p.recordings, 1u);
    EXPECT_TRUE(exists(dir, "inprogress"));
    EXPECT_FALSE(exists(dir, "done"));
    fs::remove_all(dir);
}

TEST(WebsdrRecordings, StopsAtTheCapAndIsANoOpBelowIt) {
    const std::string dir = temp_dir("stop");
    for (int i = 0; i < 4; ++i) {
        make_recording(dir, "r" + std::to_string(i), 400 * 1024, std::chrono::seconds(400 - 100 * i));
    }

    // 4 x ~400 KB, cap 1 MB: the two oldest cover the excess, the rest stay
    const auto p = websdr::prune_recordings(dir, 1024 * 1024, 0, "");
    EXPECT_EQ(p.recordings, 2u);
    EXPECT_FALSE(exists(dir, "r0"));
    EXPECT_FALSE(exists(dir, "r1"));
    EXPECT_TRUE(exists(dir, "r2"));
    EXPECT_TRUE(exists(dir, "r3"));

    const auto again = websdr::prune_recordings(dir, 10 * 1024 * 1024, 0, "");
    EXPECT_EQ(again.recordings, 0u);
    EXPECT_EQ(again.bytes, 0u);

    const auto unlimited = websdr::prune_recordings(dir, 0, 0, "");
    EXPECT_EQ(unlimited.recordings, 0u);
    EXPECT_TRUE(exists(dir, "r2"));
    fs::remove_all(dir);
}

TEST(WebsdrRecordings, IgnoresFilesThatAreNotRecordings) {
    const std::string dir = temp_dir("junk");
    std::ofstream(dir + "/notes.txt") << "hello";
    std::ofstream(dir + "/orphan.sigmf-meta") << "{}";
    make_recording(dir, "real", 400 * 1024);

    const auto p = websdr::prune_recordings(dir, 1, 0, "");

    EXPECT_EQ(p.recordings, 1u);
    EXPECT_FALSE(exists(dir, "real"));
    EXPECT_TRUE(fs::exists(dir + "/notes.txt"));
    EXPECT_TRUE(fs::exists(dir + "/orphan.sigmf-meta"));
    fs::remove_all(dir);
}

TEST(WebsdrWatchdog, SendsReadyAndPingsToNotifySocket) {
    const std::string dir = temp_dir("notify");
    const std::string sock_path = dir + "/notify.sock";

    const int srv = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(srv, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, sock_path.c_str(), sock_path.size());
    ASSERT_EQ(::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    timeval tv{2, 0};
    ::setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ::setenv("NOTIFY_SOCKET", sock_path.c_str(), 1);
    ::setenv("WATCHDOG_USEC", "4000000", 1);
    ::setenv("WATCHDOG_PID", std::to_string(::getpid()).c_str(), 1);
    {
        websdr::SdNotify notify;
        ASSERT_TRUE(notify.active());
        EXPECT_EQ(notify.interval(), std::chrono::microseconds(2000000));
        ASSERT_TRUE(notify.send("READY=1"));
        ASSERT_TRUE(notify.send("WATCHDOG=1"));

        char buf[64];
        ssize_t n = ::recv(srv, buf, sizeof(buf), 0);
        ASSERT_GT(n, 0);
        EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "READY=1");
        n = ::recv(srv, buf, sizeof(buf), 0);
        ASSERT_GT(n, 0);
        EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "WATCHDOG=1");
    }
    ::unsetenv("WATCHDOG_PID");
    ::unsetenv("WATCHDOG_USEC");
    ::unsetenv("NOTIFY_SOCKET");
    ::close(srv);
    fs::remove_all(dir);
}

TEST(WebsdrWatchdog, IsInertWithoutSystemd) {
    ::unsetenv("NOTIFY_SOCKET");
    websdr::SdNotify notify;
    EXPECT_FALSE(notify.active());
    EXPECT_EQ(notify.interval().count(), 0);
    EXPECT_FALSE(notify.send("READY=1"));
}

TEST(WebsdrWatchdog, NoIntervalWhenTheWatchdogBelongsToAnotherPid) {
    const std::string dir = temp_dir("otherpid");
    const std::string sock_path = dir + "/notify.sock";
    ::setenv("NOTIFY_SOCKET", sock_path.c_str(), 1);
    ::setenv("WATCHDOG_USEC", "4000000", 1);
    ::setenv("WATCHDOG_PID", std::to_string(::getpid() + 1).c_str(), 1);

    websdr::SdNotify notify;
    EXPECT_EQ(notify.interval().count(), 0);

    ::unsetenv("WATCHDOG_PID");
    ::unsetenv("WATCHDOG_USEC");
    ::unsetenv("NOTIFY_SOCKET");
    fs::remove_all(dir);
}
