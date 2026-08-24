#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_examples/cler_connector/connector_proto.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace conn {

// Loopback only: OWRX runs the connector on the same host and the stream has no
// authentication of any kind.
inline int listen_loopback(int port, int backlog = 4) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    const int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port < 0 ? 0 : port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(fd, backlog) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// shutdown() does not wake a blocked accept() on Linux, so every loop that has
// to notice a shutdown flag waits with poll() instead of blocking outright.
inline bool wait_readable(int fd, int timeout_ms) {
    pollfd p{fd, POLLIN, 0};
    return ::poll(&p, 1, timeout_ms) > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR));
}

inline int socket_port(int fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof addr;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return -1;
    return ntohs(addr.sin_port);
}

// Interleaved float32 IQ to every connected reader. A reader that cannot keep
// up loses samples at its own ring; the DSP chain is never held back.
class IqServer {
public:
    static constexpr size_t MAX_CLIENTS = 4;
    static constexpr size_t RING_SAMPLES = 1 << 18;

    explicit IqServer(int port) {
        _listen = listen_loopback(port);
        if (_listen < 0) cler::panic("cler_connector: cannot listen on the IQ port");
        _port = socket_port(_listen);
        for (auto& s : _slots) s = std::make_unique<Slot>();
        _accept = std::thread([this] { accept_loop(); });
    }

    ~IqServer() {
        _running = false;
        ::shutdown(_listen, SHUT_RDWR);
        ::close(_listen);
        if (_accept.joinable()) _accept.join();
        for (auto& s : _slots) {
            s->active = false;
            if (s->writer.joinable()) s->writer.join();
        }
    }

    int port() const { return _port; }

    void push(const std::complex<float>* p, size_t n) {
        for (auto& s : _slots) {
            if (!s->active.load(std::memory_order_acquire)) continue;
            // write_dbf() reports a cached lower bound on the free space and
            // only re-reads the consumer index once that bound hits zero, so a
            // short write means "ask again", not "the ring is full".
            size_t done = 0;
            while (done < n) {
                auto [w, space] = s->ring.write_dbf();
                if (space == 0) break;
                const size_t k = std::min(n - done, space);
                std::memcpy(w, p + done, k * sizeof(*p));
                s->ring.commit_write(k);
                done += k;
            }
            if (done < n) s->dropped.fetch_add(n - done, std::memory_order_relaxed);
        }
    }

    size_t clients() const {
        size_t n = 0;
        for (const auto& s : _slots) n += s->active.load(std::memory_order_acquire) ? 1 : 0;
        return n;
    }

    uint64_t dropped() const {
        uint64_t n = 0;
        for (const auto& s : _slots) n += s->dropped.load(std::memory_order_relaxed);
        return n;
    }

private:
    struct Slot {
        std::atomic<bool> active{false};
        std::atomic<uint64_t> dropped{0};
        cler::Channel<std::complex<float>> ring{RING_SAMPLES};
        std::thread writer;
        int fd = -1;
    };

    void accept_loop() {
        while (_running) {
            if (!wait_readable(_listen, 100)) continue;
            const int fd = ::accept(_listen, nullptr, nullptr);
            if (fd < 0) continue;
            Slot* free_slot = nullptr;
            for (auto& s : _slots) {
                if (s->active.load(std::memory_order_acquire)) continue;
                if (s->writer.joinable()) s->writer.join();
                free_slot = s.get();
                break;
            }
            if (!free_slot) {
                ::close(fd);
                std::fprintf(stderr, "cler_connector: too many IQ clients, refusing one\n");
                continue;
            }
            const int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            timeval tv{0, 200000};
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            free_slot->fd = fd;
            free_slot->active.store(true, std::memory_order_release);
            free_slot->writer = std::thread([this, free_slot] { write_loop(*free_slot); });
        }
    }

    void write_loop(Slot& s) {
        // A reused slot may still hold the previous reader's samples. Only the
        // consumer may move the read index, so the new writer drops them here
        // rather than the accept thread resetting the ring under the producer.
        for (auto [stale, n] = s.ring.read_dbf(); n; std::tie(stale, n) = s.ring.read_dbf()) {
            s.ring.commit_read(n);
        }
        while (_running && s.active.load(std::memory_order_acquire)) {
            auto [p, n] = s.ring.read_dbf();
            if (n == 0) {
                // An idle reader that went away is only visible as a readable
                // socket that yields EOF; OWRX's up-probe does exactly that.
                if (wait_readable(s.fd, 5)) {
                    char scratch[64];
                    if (::recv(s.fd, scratch, sizeof scratch, MSG_DONTWAIT) <= 0) break;
                }
                continue;
            }
            const char* bytes = reinterpret_cast<const char*>(p);
            const size_t total = n * sizeof(*p);
            size_t off = 0;
            bool alive = true;
            while (off < total && alive) {
                const ssize_t w = ::send(s.fd, bytes + off, total - off, MSG_NOSIGNAL);
                if (w > 0) off += static_cast<size_t>(w);
                else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) alive = _running;
                else alive = false;
            }
            s.ring.commit_read(n);
            if (!alive) break;
        }
        ::close(s.fd);
        s.fd = -1;
        s.active.store(false, std::memory_order_release);
    }

    std::atomic<bool> _running{true};
    int _listen = -1;
    int _port = -1;
    std::array<std::unique_ptr<Slot>, MAX_CLIENTS> _slots;
    std::thread _accept;
};

// One writer at a time sending "<key>:<value>\n"; OWRX never reads a reply.
class ControlServer {
public:
    explicit ControlServer(int port) {
        if (port < 0) return;
        _listen = listen_loopback(port, 1);
        if (_listen < 0) cler::panic("cler_connector: cannot listen on the control port");
        _port = socket_port(_listen);
        _accept = std::thread([this] { accept_loop(); });
    }

    ~ControlServer() {
        _running = false;
        if (_listen >= 0) {
            ::shutdown(_listen, SHUT_RDWR);
            ::close(_listen);
        }
        if (_client >= 0) ::shutdown(_client, SHUT_RDWR);
        if (_accept.joinable()) _accept.join();
    }

    int port() const { return _port; }

    bool pop(std::string& key, std::string& value) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) return false;
        key = _queue.front().first;
        value = _queue.front().second;
        _queue.pop_front();
        return true;
    }

private:
    void accept_loop() {
        while (_running) {
            if (!wait_readable(_listen, 100)) continue;
            const int fd = ::accept(_listen, nullptr, nullptr);
            if (fd < 0) continue;
            _client = fd;
            LineReader reader;
            char chunk[256];
            while (_running) {
                if (!wait_readable(fd, 100)) continue;
                const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
                if (n <= 0) break;
                reader.feed(chunk, static_cast<size_t>(n), [this](const std::string& line) {
                    std::string k, v;
                    if (!split_kv(line, k, v)) {
                        std::fprintf(stderr, "cler_connector: bad control line '%s'\n", line.c_str());
                        return;
                    }
                    std::lock_guard<std::mutex> lock(_mutex);
                    _queue.emplace_back(std::move(k), std::move(v));
                });
            }
            _client = -1;
            ::close(fd);
        }
    }

    std::atomic<bool> _running{true};
    int _listen = -1;
    std::atomic<int> _client{-1};
    int _port = -1;
    std::mutex _mutex;
    std::deque<std::pair<std::string, std::string>> _queue;
    std::thread _accept;
};

struct IqSinkBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;
    std::atomic<bool> iqswap{false};

    IqSinkBlock(const char* name, IqServer& server, size_t buffer = 1 << 16)
        : cler::BlockBase(name), in(buffer), _server(server) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [p, n] = in.read_dbf();
        if (n == 0) return cler::Error::NotEnoughSamples;
        if (!iqswap.load(std::memory_order_relaxed)) {
            _server.push(p, n);
        } else {
            size_t done = 0;
            while (done < n) {
                const size_t k = std::min(n - done, _swap.size());
                for (size_t i = 0; i < k; ++i) _swap[i] = {p[done + i].imag(), p[done + i].real()};
                _server.push(_swap.data(), k);
                done += k;
            }
        }
        in.commit_read(n);
        return cler::Empty{};
    }

private:
    IqServer& _server;
    std::array<std::complex<float>, 8192> _swap{};
};

}  // namespace conn
