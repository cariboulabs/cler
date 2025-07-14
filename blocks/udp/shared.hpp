#pragma once
#include "cler.hpp"
#include <stdexcept>
#include <cstring>
#include <cerrno>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <assert.h>

namespace UDPBlock {

enum class SocketType {
    INET_UDP,     // IPv4 UDP
    INET6_UDP,    // IPv6 UDP
    UNIX_DGRAM    // UNIX datagram
};

struct Slab;

struct BlobSlice {
    uint8_t* data;   // pointer to slab region
    size_t len;      // valid length
    size_t slot_idx; // slab index for recycling
    Slab* owner_slab;

    void release();
};

struct Slab {
    Slab(size_t num_slots, size_t max_blob_size);

    // Allocate a slice: pops a free slot, returns pointer to region
    // Returns nullptr if no space
    cler::Result<BlobSlice, cler::Error> take_slot();
    void release_slot(size_t slot_idx);

    inline size_t capacity() const { return _num_slots; }
    inline size_t available_slots() const { return _free_slots.size(); }
    inline size_t max_blob_size() const { return _max_blob_size;}

private:
    size_t _num_slots;
    size_t _max_blob_size;
    std::unique_ptr<uint8_t[]> _data;
    cler::Channel<size_t> _free_slots;
};



struct GenericDatagramSocket {
    static GenericDatagramSocket make_receiver(SocketType type,
                                           const std::string& bind_addr,
                                           uint16_t port);

    static GenericDatagramSocket make_sender(SocketType type,
                                            const std::string& dest_addr,
                                            uint16_t port);

    ~GenericDatagramSocket();
    void bind(const std::string& bind_addr_or_path, uint16_t port = 0);
    ssize_t send(const uint8_t* data, size_t len) const;
    ssize_t recv(uint8_t* buffer, size_t max_len, int flags = 0) const;
    inline bool is_valid() const { return _sockfd >= 0; }

private:
    GenericDatagramSocket(SocketType type,
                const std::string& host_or_path,
                uint16_t port = 0);

    SocketType _type;
    int _sockfd;

    struct sockaddr_in  _dest_inet {};
    struct sockaddr_in6 _dest_inet6 {};
    struct sockaddr_un  _dest_un {};

    std::string _bound_unix_path {}; // track UNIX socket file for cleanup
};

} // namespace UDPBlock
