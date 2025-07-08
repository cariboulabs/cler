#pragma once

#include "cler.hpp"
#include "utils.hpp"
#include <queue>

namespace UDPBlock {

    struct SourceUDPSocketBlock : public cler::BlockBase {
        cler::Channel<BlobSlice> out { cler::DEFAULT_BUFFER_SIZE };

        SourceUDPSocketBlock(const char* name,
                             SocketType type,
                             const std::string& bind_addr_or_path,
                             uint16_t port,
                             uint8_t* slab,
                             size_t blob_size,
                             std::queue<size_t>& free_slots)
            : cler::BlockBase(name),
              _socket(type, "", 0),
              _slab(slab),
              _blob_size(blob_size),
              _free_slots(free_slots)
        {
            _socket.bind(bind_addr_or_path, port);
        }

        cler::Result<cler::Empty, cler::Error> procedure() {
            if (!_socket.is_valid()) {
                return cler::Error::IOError;
            }
            if (_free_slots.empty()) {
                return cler::Error::NotEnoughSpace;
            }

            size_t slot_idx = _free_slots.front();
            uint8_t* slot_ptr = _slab + (slot_idx * _blob_size);
            ssize_t n = _socket.recv(slot_ptr, _blob_size);
            if (n <= 0) {
                return cler::Error::NotEnoughSamples;
            }

            BlobSlice slice { slot_ptr, static_cast<size_t>(n), slot_idx };
            out.push(slice);
            _free_slots.pop();

            return cler::Empty{};
        }

    private:
        GenericDatagramSocket _socket;
        uint8_t* _slab;
        size_t _blob_size;
        std::queue<size_t>& _free_slots;
    };

} // namespace UDPBlock
