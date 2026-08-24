#pragma once

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

namespace websdr {

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
        _fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (_fd < 0) { _path.clear(); return; }

        const char* pid = std::getenv("WATCHDOG_PID");
        if (pid && std::atoll(pid) != static_cast<long long>(::getpid())) return;
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
        return ::sendto(_fd, msg, std::strlen(msg), MSG_NOSIGNAL,
                        reinterpret_cast<sockaddr*>(&addr), len) >= 0;
    }

private:
    int _fd = -1;
    std::string _path;
    std::chrono::microseconds _interval{0};
};

}  // namespace websdr
