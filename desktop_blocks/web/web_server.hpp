#pragma once

#include "cler.hpp"
#include "desktop_blocks/spectrum/spectrum_frame.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ix { class HttpServer; class WebSocket; }

namespace web {

struct EmbeddedFile {
    const char* name;
    const char* data;
    size_t size;
};

struct ServerOptions {
    std::string bind = "127.0.0.1";
    int port = 8080;
    std::string token;
    std::string client_dir;
    const EmbeddedFile* files = nullptr;
    size_t file_count = 0;
    std::string version = "dev";
    double audio_rate = 48000.0;   // sizes the audio ring; codec 0 fixes the wire rate
};

struct ClientStats {
    uint64_t spectrum_dropped = 0, audio_dropped = 0;
};

struct HttpReply {
    int status = 404;
    std::string body = "not found";
    std::string content_type = "text/plain";
};

// Runs on an HTTP thread: whatever it touches must outlive the server or be guarded.
using HttpRoute = std::function<HttpReply(const std::string& path, const std::string& query)>;

// push_* are single-producer (one cler worker) into SPSC rings; call set_on_control before start().
class WebServer {
public:
    explicit WebServer(ServerOptions opts);
    ~WebServer();

    void start();
    void stop();
    int port() const { return _opts.port; }

    // Serves `prefix` and `prefix/...`; longest match wins, token-gated like any
    // other route. Register before start(). The library owns transport and access
    // control, the app owns what its own paths mean.
    void add_http_route(std::string prefix, HttpRoute handler);
    uint64_t uptime_seconds() const;
    // "" when the name could escape its directory; use it for any app route that
    // maps a path segment onto a file
    static std::string safe_name(const std::string& name);

    bool push_spectrum(const SpectrumFrame& f);
    size_t push_audio(const int16_t* pcm, size_t n);
    void push_text(const std::string& stream, const std::string& json);

    void set_gen(uint32_t gen) { _gen.store(gen, std::memory_order_relaxed); }
    void set_state(const std::string& json_object);
    void set_hello_extra(const std::string& json_object);
    void resend_hello();
    void set_stats_extra(const std::string& json_object);
    void send_error(const std::string& code, const std::string& msg);

    bool pop_control(std::string& json);
    void set_on_control(std::function<void(const std::string&)> fn) { _on_control = std::move(fn); }

    size_t client_count() const;
    ClientStats total_dropped() const;

private:
    struct Client;
    struct Impl;
    ServerOptions _opts;
    std::unique_ptr<Impl> _impl;
    cler::Channel<SpectrumFrame> _spec;
    cler::Channel<int16_t> _audio;
    std::atomic<uint32_t> _gen{0};
    std::atomic<uint32_t> _seq_spec{0}, _seq_audio{0};
    mutable std::mutex _mutex;
    std::deque<std::string> _text;
    std::deque<std::string> _control;
    std::vector<std::pair<std::string, HttpRoute>> _routes;
    std::string _state, _hello_extra, _stats_extra;
    std::function<void(const std::string&)> _on_control;
    std::thread _tick;
    std::atomic<bool> _running{false};
    std::atomic<uint64_t> _spectrum_dropped{0}, _audio_dropped{0}, _text_dropped{0};

    const HttpRoute* match_route(const std::string& path) const;
    void tick_loop();
    void broadcast(const std::string& text);
    std::string hello_for(const Client& c);
};

}
