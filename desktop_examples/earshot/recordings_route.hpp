#pragma once

#include "desktop_blocks/sigmf/sigmf.hpp"
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace earshot {

// GET /recordings -> the SigMF captures on disk; /recordings/<name>.sigmf-{data,meta} -> one file.
// Runs on an HTTP thread: `dirs` is fixed at startup and only the filesystem is read.
inline web::HttpReply recordings_route(const std::vector<std::string>& dirs, const std::string& path) {
    if (path == "/recordings") {
        web::JsonWriter w;
        w.begin_arr();
        std::vector<std::string> listed;
        for (size_t d = 0; d < dirs.size(); ++d) {
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(dirs[d], ec)) {
                if (!e.is_regular_file() || e.path().extension() != ".sigmf-meta") continue;
                const std::string name = e.path().stem().string();
                // same rule as SourceMux::sigmf_path: the first directory holding a name owns it
                if (std::find(listed.begin(), listed.end(), name) != listed.end()) continue;
                sigmf::Meta meta;
                if (!sigmf::try_read_meta(e.path().string(), meta)) continue;
                std::error_code ec2;
                const auto bytes = std::filesystem::file_size(sigmf::data_path(e.path().string()), ec2);
                if (ec2) continue;
                const double secs = meta.sample_rate > 0
                    ? static_cast<double>(bytes) / (sigmf::datatype_size(meta.datatype) * meta.sample_rate) : 0.0;
                const auto when = std::filesystem::last_write_time(e.path(), ec2);
                const auto age = ec2 ? std::chrono::seconds(0)
                    : std::chrono::duration_cast<std::chrono::seconds>(
                          std::filesystem::file_time_type::clock::now() - when);
                listed.push_back(name);
                w.begin_obj().key("name").str(name).key("dir").str(std::to_string(d))
                 .key("bytes").num(bytes)
                 .key("rate").num(meta.sample_rate).key("freq").num(meta.center_frequency())
                 .key("seconds").num(secs).key("age_s").num(static_cast<int64_t>(age.count())).end();
            }
        }
        w.end();
        return {200, w.out, "application/json"};
    }
    const std::string fname = web::WebServer::safe_name(path.substr(std::strlen("/recordings/")));
    const bool suffix_ok = fname.size() > 11 &&
        (fname.rfind(".sigmf-data") == fname.size() - 11 || fname.rfind(".sigmf-meta") == fname.size() - 11);
    if (!suffix_ok) return {};
    std::string full;
    std::error_code ec;
    for (const auto& dir : dirs) {
        const std::string candidate = dir + "/" + fname;
        if (!std::filesystem::is_regular_file(candidate, ec)) continue;
        // safe_name only clears the name; a symlink planted in the directory still
        // resolves wherever it likes, so check where the file actually lands
        const auto real = std::filesystem::weakly_canonical(candidate, ec);
        const auto root = std::filesystem::weakly_canonical(dir, ec);
        if (ec || real.parent_path() != root) return {};
        full = candidate;
        break;
    }
    if (full.empty()) return {};
    // ponytail: IX responses are one std::string, no chunked send; this is a
    // single exact-size read (no stringstream doubling), ceiling = file size in RAM
    const auto size = std::filesystem::file_size(full, ec);
    std::string body;
    body.resize(size);
    std::ifstream f(full, std::ios::binary);
    if (!f.read(body.data(), static_cast<std::streamsize>(size))) return {500, "read failed", "text/plain"};
    return {200, std::move(body), "application/octet-stream"};
}

}
