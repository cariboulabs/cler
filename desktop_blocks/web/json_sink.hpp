#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"

#include <string>

namespace web {

// One JSON line per decoded item, straight to every browser. `to_json(const T&,
// JsonWriter&)` is found by ADL — JsonWriter makes `web` an associated namespace,
// so an app can add adapters for its own types without touching this header.
// ponytail: push_text copies into a std::string per item — packet rates are a
// few per second, not per sample, so the writer's reused buffer is the only
// thing worth preallocating here.
template <typename T>
struct JsonTextSinkBlock : public cler::BlockBase {
    cler::Channel<T> in;

    JsonTextSinkBlock(const char* name, WebServer& server, const char* stream, size_t buffer_size = 256)
        : cler::BlockBase(name), in(buffer_size), _server(server), _stream(stream ? stream : "") {
        // the client routes text frames by stream id; an empty one is dropped silently
        if (_stream.empty()) cler::panic("JsonTextSinkBlock: empty stream id");
        _w.out.reserve(1024);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [ptr, n] = in.read_dbf();
        if (n == 0) return cler::Error::NotEnoughSamples;
        for (size_t i = 0; i < n; ++i) {
            _w.out.clear();
            to_json(ptr[i], _w);
            _server.push_text(_stream, _w.out);
        }
        in.commit_read(n);
        return cler::Empty{};
    }

    size_t buffer_capacity() const { return _w.out.capacity(); }

private:
    WebServer& _server;
    std::string _stream;
    JsonWriter _w;
};

}
