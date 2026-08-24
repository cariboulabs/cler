#pragma once

#include "desktop_blocks/sigmf/sigmf.hpp"

#include <sys/statvfs.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace websdr {

inline uint64_t free_disk(const std::string& dir) {
    struct statvfs st;
    if (statvfs(dir.empty() ? "." : dir.c_str(), &st) != 0) return 0;
    return static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
}

struct Pruned {
    size_t recordings = 0;
    uint64_t bytes = 0;
};

// Deletes whole .sigmf-meta/.sigmf-data pairs, oldest meta first, until the
// directory holds at most max_bytes (0 = unlimited) and the filesystem has
// min_free to spare. `keep` is the base path of the recording being written: it
// counts towards the total but is never deleted.
inline Pruned prune_recordings(const std::string& dir, uint64_t max_bytes, uint64_t min_free,
                               const std::string& keep) {
    Pruned out;
    if (dir.empty()) return out;

    struct Entry {
        std::string base;
        std::filesystem::file_time_type when;
        uint64_t bytes;
    };
    std::vector<Entry> entries;
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".sigmf-meta") continue;
        const std::string base = sigmf::base_path(e.path().string());
        std::error_code entry_ec;
        const auto data = std::filesystem::file_size(base + ".sigmf-data", entry_ec);
        if (entry_ec) continue;   // a meta with no data is not a recording we wrote
        const auto meta = std::filesystem::file_size(e.path(), entry_ec);
        const uint64_t bytes = data + (entry_ec ? 0 : meta);
        total += bytes;
        if (base == keep) continue;
        const auto when = std::filesystem::last_write_time(e.path(), entry_ec);
        if (entry_ec) continue;
        entries.push_back({base, when, bytes});
    }

    const uint64_t over_cap = (max_bytes && total > max_bytes) ? total - max_bytes : 0;
    const uint64_t free_now = free_disk(dir);
    const uint64_t under_floor = free_now < min_free ? min_free - free_now : 0;
    const uint64_t need = std::max(over_cap, under_floor);
    if (need == 0) return out;

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.when < b.when; });
    for (const auto& e : entries) {
        if (out.bytes >= need) break;
        std::error_code rm_ec;
        if (!std::filesystem::remove(e.base + ".sigmf-data", rm_ec)) continue;
        std::filesystem::remove(e.base + ".sigmf-meta", rm_ec);
        out.bytes += e.bytes;
        ++out.recordings;
    }
    return out;
}

}  // namespace websdr
