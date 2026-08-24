#include "desktop_blocks/web/web_server.hpp"
#include "desktop_blocks/web/proto.hpp"
#include "cler_desktop_utils.hpp"

#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace web {

namespace {

bool is_loopback(const std::string& host) {
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

std::string percent_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else if (s[i] == '+') out += ' ';
        else out += s[i];
    }
    return out;
}

std::string query_param(const std::string& uri, const std::string& name) {
    const size_t q = uri.find('?');
    if (q == std::string::npos) return "";
    size_t i = q + 1;
    while (i < uri.size()) {
        size_t amp = uri.find('&', i);
        if (amp == std::string::npos) amp = uri.size();
        const size_t eq = uri.find('=', i);
        if (eq != std::string::npos && eq < amp && uri.compare(i, eq - i, name) == 0) return percent_decode(uri.substr(eq + 1, amp - eq - 1));
        i = amp + 1;
    }
    return "";
}

std::string strip_scheme(const std::string& origin) {
    const size_t p = origin.find("://");
    return p == std::string::npos ? origin : origin.substr(p + 3);
}

std::string host_part(const std::string& host_port) {
    if (!host_port.empty() && host_port[0] == '[') { const size_t e = host_port.find(']'); return e == std::string::npos ? host_port : host_port.substr(1, e - 1); }
    return host_port.substr(0, host_port.find(':'));
}

const char* content_type(const std::string& name) {
    auto ends = [&](const char* suf) {
        const size_t n = std::strlen(suf);
        return name.size() >= n && name.compare(name.size() - n, n, suf) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".json")) return "application/json";
    return "application/octet-stream";
}

}

struct WebServer::Client {
    std::weak_ptr<ix::WebSocket> ws;
    uint64_t order = 0;
    bool controller = false;
    std::atomic<int> ping_misses{0};
    std::atomic<uint64_t> spectrum_dropped{0}, audio_dropped{0};
};

struct WebServer::Impl {
    std::vector<std::unique_ptr<ix::HttpServer>> srvs;
    std::map<ix::WebSocket*, std::shared_ptr<Client>> clients;
    uint64_t next_order = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::vector<uint8_t> buf = std::vector<uint8_t>(64 * 1024);
    std::vector<int16_t> pcm = std::vector<int16_t>(AUDIO_CHUNK);
};

std::string WebServer::safe_name(const std::string& name) {
    if (name.empty() || name[0] == '.' || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return "";
    return name;
}

void WebServer::add_http_route(std::string prefix, HttpRoute handler) {
    // _routes is read from HTTP threads with no lock, so registration has to be
    // over before the server is listening
    if (_running.load(std::memory_order_relaxed)) cler::panic("web: add_http_route after start()");
    if (prefix.size() < 2 || prefix[0] != '/' || prefix.back() == '/') {
        cler::panic(("web: bad route prefix '" + prefix + "'").c_str());
    }
    if (prefix == "/client") cler::panic("web: /client is served by the library");
    for (const auto& r : _routes) if (r.first == prefix) cler::panic(("web: duplicate route '" + prefix + "'").c_str());
    _routes.emplace_back(std::move(prefix), std::move(handler));
}

const HttpRoute* WebServer::match_route(const std::string& path) const {
    const HttpRoute* best = nullptr;
    size_t best_len = 0;
    for (const auto& r : _routes) {
        if (path.rfind(r.first, 0) != 0) continue;
        if (path.size() != r.first.size() && path[r.first.size()] != '/') continue;
        if (r.first.size() >= best_len) { best = &r.second; best_len = r.first.size(); }
    }
    return best;
}

uint64_t WebServer::uptime_seconds() const {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - _impl->started).count());
}

WebServer::WebServer(ServerOptions opts)
    : _opts(std::move(opts)), _impl(new Impl), _spec(16), _audio(static_cast<size_t>(_opts.audio_rate * 2)) {
    if (!is_loopback(_opts.bind) && _opts.token.empty()) {
        cler::panic("web: a token is required when binding to a non-loopback address");
    }
    if (!(_opts.audio_rate >= 1000.0) || _opts.audio_rate > 1e7) cler::panic("web: audio_rate out of range");
}

WebServer::~WebServer() { stop(); }

void WebServer::start() {
    ix::initNetSystem();
    // "localhost" resolves to ::1 before 127.0.0.1 on a stock Debian, so an IPv4-only
    // listener makes the obvious `ssh -L 8080:localhost:8080` hang. Loopback binds get
    // both families; a named bind gets the family its address is written in.
    if (is_loopback(_opts.bind)) {
        _impl->srvs.emplace_back(new ix::HttpServer(_opts.port, "127.0.0.1", ix::SocketServer::kDefaultTcpBacklog,
                                                    ix::SocketServer::kDefaultMaxConnections, AF_INET));
        _impl->srvs.emplace_back(new ix::HttpServer(_opts.port, "::1", ix::SocketServer::kDefaultTcpBacklog,
                                                    ix::SocketServer::kDefaultMaxConnections, AF_INET6));
    } else {
        const int family = _opts.bind.find(':') == std::string::npos ? AF_INET : AF_INET6;
        _impl->srvs.emplace_back(new ix::HttpServer(_opts.port, _opts.bind, ix::SocketServer::kDefaultTcpBacklog,
                                                    ix::SocketServer::kDefaultMaxConnections, family));
    }

    const auto on_connection = ([this](ix::HttpRequestPtr req, std::shared_ptr<ix::ConnectionState>) -> ix::HttpResponsePtr {
        ix::WebSocketHttpHeaders h;
        h["Cache-Control"] = "no-store";
        const size_t qpos = req->uri.find('?');
        std::string path = req->uri.substr(0, qpos);
        const std::string query = qpos == std::string::npos ? std::string() : req->uri.substr(qpos + 1);
        if (const HttpRoute* route = match_route(path)) {
            if (!_opts.token.empty()) {
                if (query_param(req->uri, "token") != _opts.token) {
                    return std::make_shared<ix::HttpResponse>(401, "Unauthorized", ix::HttpErrorCode::Ok, h, "token required");
                }
            } else {
                // same rebinding guard as the WebSocket upgrade: without a token a
                // page on another origin must not read an app route through DNS
                const auto it = req->headers.find("Host");
                const std::string hp = host_part(it == req->headers.end() ? std::string() : it->second);
                if (!is_loopback(hp) && hp != _opts.bind) {
                    return std::make_shared<ix::HttpResponse>(403, "Forbidden", ix::HttpErrorCode::Ok, h, "bad host");
                }
            }
            HttpReply rep = (*route)(path, query);
            h["Content-Type"] = rep.content_type;
            return std::make_shared<ix::HttpResponse>(rep.status, rep.status == 200 ? "OK" : "Error", ix::HttpErrorCode::Ok, h, std::move(rep.body));
        }
        const std::string name = safe_name(path == "/" ? "index.html" : path.rfind("/client/", 0) == 0 ? path.substr(8) : "");
        if (name.empty()) return std::make_shared<ix::HttpResponse>(404, "Not Found", ix::HttpErrorCode::Ok, h, "not found");
        std::string body;
        bool found = false;
        if (!_opts.client_dir.empty() && std::filesystem::is_regular_file(_opts.client_dir + "/" + name)) {
            std::ifstream f(_opts.client_dir + "/" + name, std::ios::binary);
            if (f) { std::ostringstream ss; ss << f.rdbuf(); body = ss.str(); found = true; }
        }
        for (size_t i = 0; !found && i < _opts.file_count; ++i) {
            if (name == _opts.files[i].name) { body.assign(_opts.files[i].data, _opts.files[i].size); found = true; }
        }
        if (!found) return std::make_shared<ix::HttpResponse>(404, "Not Found", ix::HttpErrorCode::Ok, h, "not found");
        h["Content-Type"] = content_type(name);
        return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok, h, body);
    });

    const auto on_message = ([this](std::shared_ptr<ix::ConnectionState>, ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            const auto& info = msg->openInfo;
            auto hdr = [&](const char* k) { auto it = info.headers.find(k); return it == info.headers.end() ? std::string() : it->second; };
            const std::string origin = strip_scheme(hdr("Origin")), host = hdr("Host");
            if (!origin.empty() && origin != host) { ws.close(1008, "origin"); return; }
            if (_opts.token.empty()) {
                const std::string hp = host_part(host);
                if (!is_loopback(hp) && hp != _opts.bind) { ws.close(1008, "host"); return; }
            } else if (query_param(info.uri, "token") != _opts.token) { ws.close(1008, "token"); return; }
            std::shared_ptr<ix::WebSocket> sp;
            for (auto& srv : _impl->srvs) for (auto& c : srv->getClients()) if (c.get() == &ws) sp = c;
            std::string hello;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                auto cp = std::make_shared<Client>();
                _impl->clients[&ws] = cp;
                Client& c = *cp;
                c.ws = sp;
                c.order = _impl->next_order++;
                bool have_ctl = false;
                for (auto& kv : _impl->clients) have_ctl = have_ctl || kv.second->controller;
                c.controller = !have_ctl;
                hello = hello_for(c);
            }
            ws.sendText(hello);
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Close || msg->type == ix::WebSocketMessageType::Error) {
            std::string promote;
            std::shared_ptr<ix::WebSocket> target;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _impl->clients.find(&ws);
                if (it == _impl->clients.end()) return;
                const bool was_ctl = it->second->controller;
                _impl->clients.erase(it);
                if (was_ctl && !_impl->clients.empty()) {
                    auto best = _impl->clients.begin();
                    for (auto i = _impl->clients.begin(); i != _impl->clients.end(); ++i) if (i->second->order < best->second->order) best = i;
                    best->second->controller = true;
                    target = best->second->ws.lock();
                    JsonWriter pw;
                    pw.begin_obj().key("t").str("state").key("role").str("ctl").fields_of(_state).end();
                    promote = std::move(pw.out);
                }
            }
            if (target) target->sendText(promote);
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Pong) {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _impl->clients.find(&ws);
            if (it != _impl->clients.end()) it->second->ping_misses = 0;
            return;
        }
        if (msg->type != ix::WebSocketMessageType::Message || msg->binary) return;
        Fields f;
        if (!json_parse_object(msg->str, f)) { ws.sendText("{\"t\":\"error\",\"code\":\"bad_json\",\"msg\":\"malformed message\"}"); return; }
        const std::string t = json_str(f, "t");
        if (t == "hello") return;
        bool ctl = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _impl->clients.find(&ws);
            ctl = it != _impl->clients.end() && it->second->controller;
        }
        if (t != "set" && t != "source" && t != "rescan" && t != "record" && t != "play") {
            ws.sendText("{\"t\":\"error\",\"code\":\"unknown\",\"msg\":\"unknown message type\"}");
            return;
        }
        if (!ctl) { ws.sendText("{\"t\":\"error\",\"code\":\"view\",\"msg\":\"viewer cannot control\"}"); return; }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _control.push_back(msg->str);
        }
        if (_on_control) _on_control(msg->str);
    });

    for (auto& srv : _impl->srvs) {
        srv->disablePerMessageDeflate();
        srv->setOnConnectionCallback(on_connection);
        srv->setOnClientMessageCallback(on_message);
        auto res = srv->listen();
        if (!res.first) cler::panic(("web server: listen failed: " + res.second).c_str());
        srv->start();
    }
    _running.store(true);
    _tick = std::thread([this] { tick_loop(); });
}

void WebServer::stop() {
    if (_running.exchange(false) && _tick.joinable()) _tick.join();
    for (auto& srv : _impl->srvs) srv->stop();
    _impl->srvs.clear();
}

bool WebServer::push_spectrum(const SpectrumFrame& f) {
    auto [wptr, wsize] = _spec.write_dbf();
    if (wsize == 0) { ++_spectrum_dropped; return false; }
    wptr[0] = f;
    _spec.commit_write(1);
    return true;
}

size_t WebServer::push_audio(const int16_t* pcm, size_t n) {
    // write_dbf reports free space from a cached reader index, so a short write
    // means "ask again", not "the ring is full"; only a zero is authoritative.
    size_t w = 0;
    while (w < n) {
        auto [wptr, wsize] = _audio.write_dbf();
        if (wsize == 0) break;
        const size_t chunk = std::min(n - w, wsize);
        std::memcpy(wptr, pcm + w, chunk * sizeof(int16_t));
        _audio.commit_write(chunk);
        w += chunk;
    }
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
    std::vector<std::pair<std::shared_ptr<ix::WebSocket>, std::string>> out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _state = json_object;
        for (auto& kv : _impl->clients) {
            if (auto sp = kv.second->ws.lock()) {
                JsonWriter w;
                w.begin_obj().key("t").str("state").key("role").str(kv.second->controller ? "ctl" : "view")
                 .fields_of(_state).end();
                out.emplace_back(sp, std::move(w.out));
            }
        }
    }
    for (auto& p : out) p.first->sendText(p.second);
}

void WebServer::set_hello_extra(const std::string& json_object) { std::lock_guard<std::mutex> lock(_mutex); _hello_extra = json_object; }

void WebServer::resend_hello() {
    std::vector<std::pair<std::shared_ptr<ix::WebSocket>, std::string>> out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& kv : _impl->clients)
            if (auto sp = kv.second->ws.lock()) out.emplace_back(sp, hello_for(*kv.second));
    }
    for (auto& p : out) p.first->sendText(p.second);
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
    std::lock_guard<std::mutex> lock(_mutex);
    if (_control.empty()) return false;
    json = std::move(_control.front());
    _control.pop_front();
    return true;
}

size_t WebServer::client_count() const { std::lock_guard<std::mutex> lock(_mutex); return _impl->clients.size(); }

ClientStats WebServer::total_dropped() const {
    std::lock_guard<std::mutex> lock(_mutex);
    ClientStats s{_spectrum_dropped, _audio_dropped};
    for (auto& kv : _impl->clients) { s.spectrum_dropped += kv.second->spectrum_dropped; s.audio_dropped += kv.second->audio_dropped; }
    return s;
}

std::string WebServer::hello_for(const Client& c) {
    JsonWriter w;
    w.begin_obj().key("t").str("hello").key("proto").num(PROTO_VER).key("version").str(_opts.version)
     .key("role").str(c.controller ? "ctl" : "view")
     .key("codecs").begin_arr().str("pcm16").end()
     .key("state").raw(_state.empty() ? "{}" : _state)
     .fields_of(_hello_extra).end();
    return w.out;
}

void WebServer::broadcast(const std::string& text) {
    std::vector<std::shared_ptr<ix::WebSocket>> targets;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& kv : _impl->clients) if (auto sp = kv.second->ws.lock()) targets.push_back(sp);
    }
    for (auto& t : targets) t->sendText(text);
}

void WebServer::tick_loop() {
    using namespace std::chrono;
    uint32_t tick = 0;
    auto next_stats = steady_clock::now() + seconds(1);
    std::vector<std::pair<std::shared_ptr<ix::WebSocket>, std::shared_ptr<Client>>> targets;
    std::vector<std::string> texts;
    std::string stats_extra;
    while (_running.load(std::memory_order_relaxed)) {
        const auto next = steady_clock::now() + milliseconds(20);
        ++tick;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            targets.clear();
            for (auto& kv : _impl->clients) if (auto sp = kv.second->ws.lock()) targets.emplace_back(sp, kv.second);
            texts.assign(_text.begin(), _text.end());
            _text.clear();
            stats_extra = _stats_extra;
        }

        auto [sptr, ssize] = _spec.read_dbf();
        for (size_t i = 0; i < ssize; ++i) {
            const size_t len = encode_spectrum(sptr[i], _seq_spec++, _impl->buf.data(), _impl->buf.size());
            for (auto& t : targets) {
                if (t.first->bufferedAmount() > 4 * len) { ++t.second->spectrum_dropped; continue; }
                t.first->sendBinary(ix::IXWebSocketSendData(reinterpret_cast<const char*>(_impl->buf.data()), len));
            }
        }
        if (ssize) { _spec.commit_read(ssize); _sent.fetch_add(ssize, std::memory_order_relaxed); }

        for (;;) {
            auto [aptr, asize] = _audio.read_dbf();
            if (asize < AUDIO_CHUNK) break;
            const size_t len = encode_audio(_gen.load(std::memory_order_relaxed), _seq_audio++, aptr, AUDIO_CHUNK, _impl->buf.data(), _impl->buf.size());
            _audio.commit_read(AUDIO_CHUNK);
            for (auto& t : targets) {
                if (t.first->bufferedAmount() > 25 * len) { ++t.second->audio_dropped; continue; }
                t.first->sendBinary(ix::IXWebSocketSendData(reinterpret_cast<const char*>(_impl->buf.data()), len));
            }
        }

        for (auto& text : texts) for (auto& t : targets) t.first->sendText(text);

        if (steady_clock::now() >= next_stats) {
            next_stats += seconds(1);
            for (auto& t : targets) {
                JsonWriter w;
                w.begin_obj().key("t").str("stats")
                 .key("spectrum_dropped").num(t.second->spectrum_dropped)
                 .key("audio_dropped").num(t.second->audio_dropped)
                 .key("text_dropped").num(_text_dropped.load(std::memory_order_relaxed))
                 .key("clients").num(targets.size())
                 .fields_of(stats_extra).end();
                t.first->sendText(w.out);
            }
        }
        if (tick % 250 == 0) {
            for (auto& t : targets) {
                if (t.second->ping_misses >= 2) { t.first->close(1001, "ping timeout"); continue; }
                ++t.second->ping_misses;
                t.first->ping("");
            }
        }
        std::this_thread::sleep_until(next);
    }
}

}
