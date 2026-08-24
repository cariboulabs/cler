#pragma once

#include "desktop_blocks/sigmf/sigmf.hpp"

#include <sys/statvfs.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace websdr {

inline uint64_t free_disk(const std::string& dir) {
    struct statvfs st;
    if (statvfs(dir.empty() ? "." : dir.c_str(), &st) != 0) return 0;
    return static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
}

// Byte counts spelled like the other numeric flags, so --record-max-bytes 20e9
// means what --freq 100e6 means. 0 is unlimited; anything else is a hard no.
inline bool parse_bytes(const char* s, uint64_t& out) {
    if (!s || !*s) return false;
    // strtod reads 0x10 as 16, and a 16-byte cap would wipe the archive
    for (const char* p = s; *p; ++p) if (*p == 'x' || *p == 'X') return false;
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (end == s || *end != '\0') return false;
    if (!(v >= 0.0) || v >= 1.8e19) return false;   // negative, NaN, or past uint64
    out = static_cast<uint64_t>(v);
    return true;
}

struct Pruned {
    size_t recordings = 0;
    uint64_t bytes = 0;
};

// A recording touched within this window may still be growing under another
// process, so it is neither counted nor deleted — the same treatment `keep`
// gets. Freshness is judged on the newest of the pair: a meta is written once at
// the start and never rewritten, so meta mtime alone would read a capture that
// began two minutes ago as stale while its data file is still being appended to.
inline constexpr std::chrono::seconds kFreshWindow{60};

// Deletes whole recordings, oldest meta first, until the archive holds at most
// max_bytes (0 = unlimited) and the filesystem has min_free to spare.
//
// The cap governs the *archive*: the recording being written (`keep`) and
// anything younger than kFreshWindow are excluded from the total as well as
// from deletion, so a live capture larger than the cap can never make the
// archive look over budget and wipe it. A capture that outgrows the disk is
// stopped by the free-space floor, not by deleting history.
inline Pruned prune_recordings(const std::string& dir, uint64_t max_bytes, uint64_t min_free,
                               const std::string& keep) {
    Pruned out;
    if (dir.empty()) return out;

    struct Entry {
        std::filesystem::file_time_type when;   // meta mtime: capture start, the sort key
        // ::min(), not {}: a default-constructed file_time_type is in the far
        // future on libstdc++, which would make every recording look fresh
        std::filesystem::file_time_type newest = std::filesystem::file_time_type::min();
        uint64_t bytes = 0;
        bool has_meta = false;
        bool has_data = false;
    };
    std::map<std::string, Entry> found;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        const auto ext = e.path().extension();
        const bool is_meta = ext == ".sigmf-meta";
        const bool is_data = ext == ".sigmf-data";
        if (!is_meta && !is_data) continue;
        std::error_code fe;
        const auto size = std::filesystem::file_size(e.path(), fe);
        const auto when = std::filesystem::last_write_time(e.path(), fe);
        if (fe) continue;
        Entry& en = found[sigmf::base_path(e.path().string())];
        en.bytes += size;
        en.newest = std::max(en.newest, when);
        if (is_meta) { en.has_meta = true; en.when = when; }
        if (is_data) { en.has_data = true; if (!en.has_meta) en.when = when; }
    }

    struct Candidate { std::string base; std::filesystem::file_time_type when; uint64_t bytes; };
    std::vector<Candidate> candidates;
    uint64_t total = 0;
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const auto& [base, en] : found) {
        // A lone meta is not a recording of ours. It is left alone rather than
        // tidied away: it may be another tool's capture between writing its meta
        // and creating its data, and it costs a few hundred bytes to be wrong.
        if (!en.has_data) continue;
        if (base == keep) continue;
        if (now - en.newest < kFreshWindow) continue;
        total += en.bytes;
        candidates.push_back({base, en.when, en.bytes});
    }

    const uint64_t over_cap = (max_bytes && total > max_bytes) ? total - max_bytes : 0;
    const uint64_t free_now = free_disk(dir);
    const uint64_t under_floor = free_now < min_free ? min_free - free_now : 0;
    const uint64_t need = std::max(over_cap, under_floor);
    if (need == 0) return out;

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.when < b.when; });
    for (const auto& c : candidates) {
        if (out.bytes >= need) break;
        std::error_code rm_ec;
        if (!std::filesystem::remove(c.base + ".sigmf-data", rm_ec)) continue;
        std::filesystem::remove(c.base + ".sigmf-meta", rm_ec);
        out.bytes += c.bytes;
        ++out.recordings;
        // a headless box must not delete a client's captures silently
        std::fprintf(stderr, "websdr: pruned %s (%llu MB)\n", c.base.c_str(),
                     static_cast<unsigned long long>(c.bytes >> 20));
    }
    return out;
}

}  // namespace websdr
