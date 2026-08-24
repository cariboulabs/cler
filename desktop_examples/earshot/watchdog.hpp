#pragma once

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

namespace earshot {

// What the main loop knows about the receiver when it is deciding whether to
// tell systemd it is alive. `delivered` is the only positive evidence: it counts
// data the sink handed to the web server since the last check.
struct Health {
    bool running = false;
    bool lost = false;
    bool paused = false;
    bool ended = false;
    bool delivered = false;
    std::chrono::steady_clock::duration lost_for{0};
};

// A restart only helps when the process is stuck, so idle states are healthy and
// the one failure the retry loop cannot talk its way out of — a source that has
// been lost for longer than the grace period — is not.
inline bool flowing(const Health& h, std::chrono::steady_clock::duration lost_grace) {
    if (h.lost && h.lost_for > lost_grace) return false;
    if (!h.running) return true;
    if (h.paused || h.ended) return true;
    return h.delivered;
}

// systemd's sd_notify wire protocol is one datagram of "KEY=value\n" pairs to
// $NOTIFY_SOCKET, so it needs no libsystemd. Everything is a no-op when the
// environment is absent, which is how it stays silent outside systemd.
class SdNotify {
public:
    SdNotify() {
        const char* sock = std::getenv("NOTIFY_SOCKET");
        if (!sock || !*sock) return;
        _path = sock;
        if (_path.size() >= sizeof(sockaddr_un::sun_path)) { _path.clear(); return; }
        _fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        if (_fd < 0) { _path.clear(); return; }
        // SOCK_CLOEXEC is not portable; the flag is
        ::fcntl(_fd, F_SETFD, ::fcntl(_fd, F_GETFD, 0) | FD_CLOEXEC);

        const char* pid = std::getenv("WATCHDOG_PID");
        if (pid && *pid && std::atoll(pid) != static_cast<long long>(::getpid())) return;
        const char* usec = std::getenv("WATCHDOG_USEC");
        const long long us = usec ? std::atoll(usec) : 0;
        // systemd wants the ping well inside the deadline; half is what sd_watchdog_enabled callers use
        if (us > 0) _interval = std::chrono::microseconds(us / 2);
    }

    ~SdNotify() { if (_fd >= 0) ::close(_fd); }
    SdNotify(const SdNotify&) = delete;
    SdNotify& operator=(const SdNotify&) = delete;

    bool active() const { return _fd >= 0; }
    std::chrono::microseconds interval() const { return _interval; }

    bool send(const char* msg) {
        if (_fd < 0) return false;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, _path.data(), _path.size());
        // systemd spells the abstract namespace with a leading '@'
        if (addr.sun_path[0] == '@') addr.sun_path[0] = '\0';
        const socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + _path.size());
        return ::sendto(_fd, msg, std::strlen(msg), 0,
                        reinterpret_cast<sockaddr*>(&addr), len) >= 0;
    }

private:
    int _fd = -1;
    std::string _path;
    std::chrono::microseconds _interval{0};
};

}  // namespace earshot
