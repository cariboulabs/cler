#pragma once

#include "desktop_blocks/sigmf/sigmf.hpp"
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace websdr {

// GET /recordings -> the SigMF captures on disk; /recordings/<name>.sigmf-{data,meta} -> one file.
// Runs on an HTTP thread: `dir` is fixed at startup and only the filesystem is read.
inline web::HttpReply recordings_route(const std::string& dir, const std::string& path) {
    if (path == "/recordings") {
        web::JsonWriter w;
        w.begin_arr();
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".sigmf-meta") continue;
            sigmf::Meta meta;
            if (!sigmf::try_read_meta(e.path().string(), meta)) continue;
            std::error_code ec2;
            const auto bytes = std::filesystem::file_size(sigmf::data_path(e.path().string()), ec2);
            if (ec2) continue;
            const double secs = meta.sample_rate > 0
                ? static_cast<double>(bytes) / (sigmf::datatype_size(meta.datatype) * meta.sample_rate) : 0.0;
            w.begin_obj().key("name").str(e.path().stem().string()).key("bytes").num(static_cast<double>(bytes))
             .key("rate").num(meta.sample_rate).key("freq").num(meta.center_frequency())
             .key("seconds").num(secs).end();
        }
        w.end();
        return {200, w.out, "application/json"};
    }
    const std::string fname = web::WebServer::safe_name(path.substr(std::strlen("/recordings/")));
    const bool suffix_ok = fname.size() > 11 &&
        (fname.rfind(".sigmf-data") == fname.size() - 11 || fname.rfind(".sigmf-meta") == fname.size() - 11);
    if (!suffix_ok) return {};
    const std::string full = dir + "/" + fname;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(full, ec)) return {};
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
