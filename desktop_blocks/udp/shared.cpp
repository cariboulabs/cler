#include "shared.hpp"
#include <sstream>

namespace UDPBlock {

ParsedAddress parse_address_string(SocketType type, const std::string& addr_str) {
    ParsedAddress result;
    
    if (type == SocketType::UNIX_DGRAM) {
        // For UNIX sockets, the entire string is the path
        result.address = addr_str;
        result.port = 0;
        return result;
    }
    
    // For IP sockets (IPv4/IPv6), parse IP:port format
    size_t colon_pos = addr_str.find_last_of(':');
    if (colon_pos == std::string::npos) {
        throw std::runtime_error("Invalid address format for IP socket. Expected 'IP:port'");
    }
    
    result.address = addr_str.substr(0, colon_pos);
    std::string port_str = addr_str.substr(colon_pos + 1);
    
    try {
        int port_int = std::stoi(port_str);
        if (port_int < 0 || port_int > 65535) {
            throw std::runtime_error("Port number out of range (0-65535)");
        }
        result.port = static_cast<uint16_t>(port_int);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid port number: " + port_str);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Port number out of range: " + port_str);
    }
    
    return result;
}


void BlobSlice::release() {
  assert(owner_slab != nullptr && "BUG: double release");
  assert(slot_idx < owner_slab->capacity() && "slot_idx out of bounds!");
  if (owner_slab) {
    owner_slab->release_slot(slot_idx);
  }
  owner_slab = nullptr; // Prevent double release
}

Slab::Slab(size_t num_slots, size_t max_blob_size)
    : _num_slots(num_slots), _max_blob_size(max_blob_size), 
        _free_slots(num_slots)
{
    _data = std::make_unique<uint8_t[]>(num_slots * max_blob_size);
    for (size_t i = 0; i < num_slots; ++i) {
        _free_slots.push(i);
    }
}

// Allocate a slice: pops a free slot, returns pointer to region
// Returns nullptr if no space
cler::Result<BlobSlice, cler::Error> Slab::take_slot() {
    size_t slot_idx;
    if (!_free_slots.try_pop(slot_idx)) {
        return cler::Error::ProcedureError;
    }
    uint8_t* ptr = _data.get() + (slot_idx * _max_blob_size);
    return BlobSlice{ptr, _max_blob_size, slot_idx, this};
}

void Slab::release_slot(size_t slot_idx) {
    assert(_free_slots.try_push(slot_idx));
}

GenericDatagramSocket::GenericDatagramSocket(SocketType type,
                    const std::string& host_or_path,
                    uint16_t port)
: _type(type)
{
    const bool is_receiver = host_or_path.empty() && port == 0;

    if (type == SocketType::INET_UDP) {
        _sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (_sockfd < 0) {
            throw std::runtime_error("GenericDatagramSocket: failed to create INET socket: " + std::string(strerror(errno)));
        }

        if (!is_receiver) {
            memset(&_dest_inet, 0, sizeof(_dest_inet));
            _dest_inet.sin_family = AF_INET;
            _dest_inet.sin_port = htons(port);
            if (inet_pton(AF_INET, host_or_path.c_str(), &_dest_inet.sin_addr) <= 0) {
                close(_sockfd);
                throw std::runtime_error("GenericDatagramSocket: invalid IPv4 address: " + host_or_path);
            }
        }
    }
    else if (type == SocketType::INET6_UDP) {
        _sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
        if (_sockfd < 0) {
            throw std::runtime_error("GenericDatagramSocket: failed to create INET6 socket: " + std::string(strerror(errno)));
        }

        if (!is_receiver) {
            memset(&_dest_inet6, 0, sizeof(_dest_inet6));
            _dest_inet6.sin6_family = AF_INET6;
            _dest_inet6.sin6_port = htons(port);
            if (inet_pton(AF_INET6, host_or_path.c_str(), &_dest_inet6.sin6_addr) <= 0) {
                close(_sockfd);
                throw std::runtime_error("GenericDatagramSocket: invalid IPv6 address: " + host_or_path);
            }
        }
    }
    else if (type == SocketType::UNIX_DGRAM) {
        _sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (_sockfd < 0) {
            throw std::runtime_error("GenericDatagramSocket: failed to create UNIX socket: " + std::string(strerror(errno)));
        }

        if (!is_receiver) {
            memset(&_dest_un, 0, sizeof(_dest_un));
            _dest_un.sun_family = AF_UNIX;
            if (host_or_path.size() >= sizeof(_dest_un.sun_path)) {
                close(_sockfd);
                throw std::runtime_error("GenericDatagramSocket: UNIX socket path too long");
            }
            std::strncpy(_dest_un.sun_path, host_or_path.c_str(), sizeof(_dest_un.sun_path) - 1);
        }
    }
    else {
        throw std::runtime_error("GenericDatagramSocket: unknown SocketType");
    }
}

GenericDatagramSocket GenericDatagramSocket::make_receiver(SocketType type,
                                        const std::string& bind_addr_str)
{
    ParsedAddress parsed = parse_address_string(type, bind_addr_str);
    GenericDatagramSocket sock(type, "", 0); // placeholders
    sock.bind(parsed.address, parsed.port);
    return sock;
}

GenericDatagramSocket GenericDatagramSocket::make_sender(SocketType type,
                                        const std::string& dest_addr_str)
{
    ParsedAddress parsed = parse_address_string(type, dest_addr_str);
    return GenericDatagramSocket(type, parsed.address, parsed.port);
}

GenericDatagramSocket::~GenericDatagramSocket() {
    if (_sockfd >= 0) {
        close(_sockfd);
    }

    // Clean up UNIX socket file if we created one
    if (!_bound_unix_path.empty()) {
        unlink(_bound_unix_path.c_str());
    }
}

void GenericDatagramSocket::bind(const std::string& bind_addr_or_path, uint16_t port) {
    if (_type == SocketType::INET_UDP) {
        struct sockaddr_in local_addr {};
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(port);
        local_addr.sin_addr.s_addr = INADDR_ANY;

        // Enable address reuse for quick rebinding
        int opt = 1;
        if (setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            throw std::runtime_error("GenericDatagramSocket: setsockopt SO_REUSEADDR failed");
        }

        if (::bind(_sockfd, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
            throw std::runtime_error("GenericDatagramSocket: bind failed (INET_UDP)");
        }
    }
    else if (_type == SocketType::INET6_UDP) {
        struct sockaddr_in6 local_addr6 {};
        local_addr6.sin6_family = AF_INET6;
        local_addr6.sin6_port = htons(port);
        local_addr6.sin6_addr = in6addr_any;

        int opt = 1;
        if (setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            throw std::runtime_error("GenericDatagramSocket: setsockopt SO_REUSEADDR failed");
        }

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

        std::strncpy(local_un.sun_path, bind_addr_or_path.c_str(), sizeof(local_un.sun_path) - 1);

        unlink(local_un.sun_path); // clean up old socket file
        if (::bind(_sockfd, reinterpret_cast<struct sockaddr*>(&local_un), sizeof(local_un)) < 0) {
            throw std::runtime_error("GenericDatagramSocket: bind failed (UNIX_DGRAM)");
        }

        // Store path for later cleanup
        _bound_unix_path = bind_addr_or_path;
    }
}

ssize_t GenericDatagramSocket::send(const uint8_t* data, size_t len) const {
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
                        reinterpret_cast<const struct sockaddr*>(&_dest_un), sizeof(_dest_un));
    }
    throw std::runtime_error("GenericDatagramSocket: send called on invalid socket type");
}

ssize_t GenericDatagramSocket::recv(uint8_t* buffer, size_t max_len, int flags) const {
    struct msghdr msg {};
    struct iovec iov {};

    iov.iov_base = buffer;
    iov.iov_len  = max_len;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_flags = 0;

    ssize_t bytes_received = recvmsg(_sockfd, &msg, flags);
    if (bytes_received < 0) {
        return -errno; // propagate real reason
    }

    if (msg.msg_flags & MSG_TRUNC) {
        return -EMSGSIZE; // standard POSIX error for truncation
    }

    return bytes_received;
}
} // namespace UDPBlock
