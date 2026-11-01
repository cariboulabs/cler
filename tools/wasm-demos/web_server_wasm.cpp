// Browser edition of web::WebServer: same header, no sockets. The page is the
// single, always-controller client; frames go out through a JS callback on the
// main thread and controls come back in through exported C functions. Built
// only by the Emscripten demo build (tools/flowgraph_gui/web-build/build.sh),
// never by CMake.
#include "desktop_blocks/web/web_server.hpp"
#include "desktop_blocks/web/proto.hpp"

#include <emscripten.h>
#include <emscripten/threading.h>

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace web {

struct WebServer::Client { bool controller = true; };

struct WebServer::Impl {
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::vector<uint8_t> buf = std::vector<uint8_t>(64 * 1024);
};

// File-scope so the extern "C" entry points below need no members added to the
// shared web_server.hpp. One server, one page-client.
static WebServer* g_server = nullptr;
static std::atomic<bool> g_connected{false};

// Copies land on the JS main thread asynchronously; JS frees the heap copy.
static void emit(const void* data, size_t len, bool text) {
    void* p = std::malloc(len);
    if (!p) return;
    std::memcpy(p, data, len);
    MAIN_THREAD_ASYNC_EM_ASM({ window.__earshotFrame($0, $1, $2); }, p, len, text ? 1 : 0);
}
static void emit_text(const std::string& s) { emit(s.data(), s.size(), true); }

static std::mutex g_control_mutex;
static std::deque<std::string> g_control;

extern "C" {

EMSCRIPTEN_KEEPALIVE void earshot_ws_open() {
    g_connected.store(true);
    if (WebServer* s = g_server) s->resend_hello();
}

EMSCRIPTEN_KEEPALIVE void earshot_ws_send(const char* json) {
    Fields f;
    if (!json_parse_object(json, f)) return;
    if (json_str(f, "t") == "hello") return;   // hello is pushed on open, like the native server
    std::lock_guard<std::mutex> lock(g_control_mutex);
    g_control.push_back(json);
}

}

std::string WebServer::safe_name(const std::string& name) {
    if (name.empty() || name[0] == '.' || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return "";
    return name;
}

void WebServer::add_http_route(std::string, HttpRoute) {}   // no HTTP in the page
const HttpRoute* WebServer::match_route(const std::string&) const { return nullptr; }

uint64_t WebServer::uptime_seconds() const {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - _impl->started).count());
}

WebServer::WebServer(ServerOptions opts)
    : _opts(std::move(opts)), _impl(new Impl), _spec(16), _audio(static_cast<size_t>(_opts.audio_rate * 2)) {}

WebServer::~WebServer() { stop(); }

void WebServer::start() {
    g_server = this;
    _running.store(true);
    _tick = std::thread([this] { tick_loop(); });
    MAIN_THREAD_ASYNC_EM_ASM({ if (window.__earshotReady) window.__earshotReady(); });
}

void WebServer::stop() {
    if (_running.exchange(false) && _tick.joinable()) _tick.join();
    g_server = nullptr;
}

bool WebServer::push_spectrum(const SpectrumFrame& f) {
    auto [wptr, wsize] = _spec.write_dbf();
    if (wsize == 0) { ++_spectrum_dropped; return false; }
    wptr[0] = f;
    _spec.commit_write(1);
    return true;
}

size_t WebServer::push_audio(const int16_t* pcm, size_t n) {
    const uint32_t gen = _gen.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(_audio_gen_mutex);
        if (_audio_gen_marks.empty() ? gen != _audio_gen : gen != _audio_gen_marks.back().second) {
            _audio_gen_marks.emplace_back(_audio_written, gen);
        }
    }
    size_t w = 0;
    while (w < n) {
        auto [wptr, wsize] = _audio.write_dbf();
        if (wsize == 0) break;
        const size_t chunk = std::min(n - w, wsize);
        std::memcpy(wptr, pcm + w, chunk * sizeof(int16_t));
        _audio.commit_write(chunk);
        w += chunk;
    }
    _audio_written += w;
    _audio_dropped += n - w;
    return w;
}

void WebServer::push_text(const std::string& stream, const std::string& json) {
    std::lock_guard<std::mutex> lock(_mutex);
    JsonWriter w;
    w.begin_obj().key("t").str("text").key("stream").str(stream).key("data").raw(json).end();
    _text.push_back(std::move(w.out));
    if (_text.size() > 256) { _text.pop_front(); ++_text_dropped; }
}

void WebServer::set_state(const std::string& json_object) {
    std::string out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _state = json_object;
        JsonWriter w;
        w.begin_obj().key("t").str("state").key("role").str("ctl").fields_of(_state).end();
        out = std::move(w.out);
    }
    if (g_connected.load()) emit_text(out);
}

void WebServer::set_hello_extra(const std::string& json_object) { std::lock_guard<std::mutex> lock(_mutex); _hello_extra = json_object; }

void WebServer::resend_hello() {
    std::string hello;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Client c;
        hello = hello_for(c);
    }
    if (g_connected.load()) emit_text(hello);
}

void WebServer::set_stats_extra(const std::string& json_object) { std::lock_guard<std::mutex> lock(_mutex); _stats_extra = json_object; }

void WebServer::send_error(const std::string& code, const std::string& msg, const std::string& id) {
    JsonWriter w;
    w.begin_obj().key("t").str("error").key("code").str(code).key("msg").str(msg);
    if (!id.empty()) w.key("id").str(id);
    w.end();
    broadcast(w.out);
}

bool WebServer::pop_control(std::string& json) {
    std::lock_guard<std::mutex> lock(g_control_mutex);
    if (g_control.empty()) return false;
    json = std::move(g_control.front());
    g_control.pop_front();
    return true;
}

size_t WebServer::client_count() const { return g_connected.load() ? 1 : 0; }

ClientStats WebServer::total_dropped() const {
    return ClientStats{_spectrum_dropped.load(), _audio_dropped.load()};
}

std::string WebServer::hello_for(const Client&) {
    JsonWriter w;
    w.begin_obj().key("t").str("hello").key("proto").num(PROTO_VER).key("version").str(_opts.version)
     .key("role").str("ctl")
     .key("codecs").begin_arr().str("pcm16").end()
     .key("state").raw(_state.empty() ? "{}" : _state)
     .fields_of(_hello_extra).end();
    return w.out;
}

void WebServer::broadcast(const std::string& text) {
    if (g_connected.load()) emit_text(text);
}

void WebServer::tick_loop() {
    using namespace std::chrono;
    auto next_stats = steady_clock::now() + seconds(1);
    std::vector<std::string> texts;
    std::string stats_extra;
    while (_running.load(std::memory_order_relaxed)) {
        const auto next = steady_clock::now() + milliseconds(20);
        const bool connected = g_connected.load();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            texts.assign(_text.begin(), _text.end());
            _text.clear();
            stats_extra = _stats_extra;
        }

        auto [sptr, ssize] = _spec.read_dbf();
        for (size_t i = 0; i < ssize; ++i) {
            const size_t len = encode_spectrum(sptr[i], _seq_spec++, _impl->buf.data(), _impl->buf.size());
            if (connected) emit(_impl->buf.data(), len, false);
        }
        if (ssize) { _spec.commit_read(ssize); _sent.fetch_add(ssize, std::memory_order_relaxed); }

        for (;;) {
            auto [aptr, asize] = _audio.read_dbf();
            if (asize < AUDIO_CHUNK) break;
            {
                std::lock_guard<std::mutex> lock(_audio_gen_mutex);
                while (!_audio_gen_marks.empty() && _audio_gen_marks.front().first <= _audio_read) {
                    _audio_gen = _audio_gen_marks.front().second;
                    _audio_gen_marks.pop_front();
                }
            }
            const size_t len = encode_audio(_audio_gen, _seq_audio++, aptr, AUDIO_CHUNK, _impl->buf.data(), _impl->buf.size());
            _audio.commit_read(AUDIO_CHUNK);
            _audio_read += AUDIO_CHUNK;
            if (connected) emit(_impl->buf.data(), len, false);
        }

        if (connected) for (auto& text : texts) emit_text(text);

        if (steady_clock::now() >= next_stats) {
            next_stats += seconds(1);
            if (connected) {
                JsonWriter w;
                w.begin_obj().key("t").str("stats")
                 .key("spectrum_dropped").num(_spectrum_dropped.load())
                 .key("audio_dropped").num(_audio_dropped.load())
                 .key("text_dropped").num(_text_dropped.load(std::memory_order_relaxed))
                 .key("clients").num(1)
                 .fields_of(stats_extra).end();
                emit_text(w.out);
            }
        }
        std::this_thread::sleep_until(next);
    }
}

}
