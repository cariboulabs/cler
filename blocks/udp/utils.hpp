#pragma once

#include <string>
#include <stdexcept>
#include <cstring>
#include <cerrno>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>

namespace UDPBlock {

    // --------------------------------------
    // Shared Socket Types and BlobSlice
    // --------------------------------------
    enum class SocketType {
        INET_UDP,     // IPv4 UDP
        INET6_UDP,    // IPv6 UDP
        UNIX_DGRAM    // UNIX datagram
    };

    struct BlobSlice {
        uint8_t* data;   // pointer to slab region
        size_t len;      // valid length
        size_t slot_idx; // slab index for recycling
    };

    // --------------------------------------
    // GenericDatagramSocket
    // --------------------------------------
    struct GenericDatagramSocket {
        GenericDatagramSocket(SocketType type,
                              const std::string& host_or_path,
                              uint16_t port = 0)
            : _type(type)
        {
            if (type == SocketType::INET_UDP) {
                _sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                if (_sockfd < 0) {
                    throw std::runtime_error("GenericDatagramSocket: failed to create INET socket: " + std::string(strerror(errno)));
                }

                memset(&_dest_inet, 0, sizeof(_dest_inet));
                _dest_inet.sin_family = AF_INET;
                _dest_inet.sin_port = htons(port);
                if (inet_pton(AF_INET, host_or_path.c_str(), &_dest_inet.sin_addr) <= 0) {
                    close(_sockfd);
                    throw std::runtime_error("GenericDatagramSocket: invalid IPv4 address: " + host_or_path);
                }
            }
            else if (type == SocketType::INET6_UDP) {
                _sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
                if (_sockfd < 0) {
                    throw std::runtime_error("GenericDatagramSocket: failed to create INET6 socket: " + std::string(strerror(errno)));
                }

                memset(&_dest_inet6, 0, sizeof(_dest_inet6));
                _dest_inet6.sin6_family = AF_INET6;
                _dest_inet6.sin6_port = htons(port);
                if (inet_pton(AF_INET6, host_or_path.c_str(), &_dest_inet6.sin6_addr) <= 0) {
                    close(_sockfd);
                    throw std::runtime_error("GenericDatagramSocket: invalid IPv6 address: " + host_or_path);
                }
            }
            else if (type == SocketType::UNIX_DGRAM) {
                _sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
                if (_sockfd < 0) {
                    throw std::runtime_error("GenericDatagramSocket: failed to create UNIX socket: " + std::string(strerror(errno)));
                }

                memset(&_dest_unix, 0, sizeof(_dest_unix));
                _dest_unix.sun_family = AF_UNIX;
                if (host_or_path.size() >= sizeof(_dest_unix.sun_path)) {
                    close(_sockfd);
                    throw std::runtime_error("GenericDatagramSocket: UNIX socket path too long");
                }
                std::strncpy(_dest_unix.sun_path, host_or_path.c_str(), sizeof(_dest_unix.sun_path));
            }
            else {
                throw std::runtime_error("GenericDatagramSocket: unknown SocketType");
            }
        }

        ~GenericDatagramSocket() {
            if (_sockfd >= 0) {
                close(_sockfd);
            }
        }

        void bind(const std::string& bind_addr_or_path, uint16_t port = 0) {
            if (_type == SocketType::INET_UDP) {
                struct sockaddr_in local_addr {};
                local_addr.sin_family = AF_INET;
                local_addr.sin_port = htons(port);
                local_addr.sin_addr.s_addr = INADDR_ANY;
                if (::bind(_sockfd, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
                    throw std::runtime_error("GenericDatagramSocket: bind failed (INET_UDP)");
                }
            }
            else if (_type == SocketType::INET6_UDP) {
                struct sockaddr_in6 local_addr6 {};
                local_addr6.sin6_family = AF_INET6;
                local_addr6.sin6_port = htons(port);
                local_addr6.sin6_addr = in6addr_any;
                if (::bind(_sockfd, reinterpret_cast<struct sockaddr*>(&local_addr6), sizeof(local_addr6)) < 0) {
                    throw std::runtime_error("GenericDatagramSocket: bind failed (INET6_UDP)");
                }
            }
            else if (_type == SocketType::UNIX_DGRAM) {
                struct sockaddr_un local_un {};
                local_un.sun_family = AF_UNIX;
                if (bind_addr_or_path.size() >= sizeof(local_un.sun_path)) {
                    throw std::runtime_error("GenericDatagramSocket: UNIX bind path too long");
                }
                std::strncpy(local_un.sun_path, bind_addr_or_path.c_str(), sizeof(local_un.sun_path));
                unlink(local_un.sun_path); // clean up old socket file
                if (::bind(_sockfd, reinterpret_cast<struct sockaddr*>(&local_un), sizeof(local_un)) < 0) {
                    throw std::runtime_error("GenericDatagramSocket: bind failed (UNIX_DGRAM)");
                }
            }
        }

        ssize_t send(const uint8_t* data, size_t len) const {
            if (_type == SocketType::INET_UDP) {
                return sendto(_sockfd, data, len, 0,
                              reinterpret_cast<const struct sockaddr*>(&_dest_inet), sizeof(_dest_inet));
            }
            else if (_type == SocketType::INET6_UDP) {
                return sendto(_sockfd, data, len, 0,
                              reinterpret_cast<const struct sockaddr*>(&_dest_inet6), sizeof(_dest_inet6));
            }
            else if (_type == SocketType::UNIX_DGRAM) {
                return sendto(_sockfd, data, len, 0,
                              reinterpret_cast<const struct sockaddr*>(&_dest_unix), sizeof(_dest_unix));
            }
            return -1;
        }

        ssize_t recv(uint8_t* buffer, size_t max_len) const {
            return recvfrom(_sockfd, buffer, max_len, 0, nullptr, nullptr);
        }

        bool is_valid() const { return _sockfd >= 0; }

    private:
        SocketType _type;
        int _sockfd;

        // Destination addresses for sending:
        struct sockaddr_in  _dest_inet {};
        struct sockaddr_in6 _dest_inet6 {};
        struct sockaddr_un  _dest_unix {};
    };

} // namespace UDPBlock
