#pragma once

#include "cler.hpp"
#include "utils.hpp"
#include <queue>

namespace UDPBlock {

    struct SinkUDPSocketBlock : public cler::BlockBase {
        cler::Channel<BlobSlice> in { cler::DEFAULT_BUFFER_SIZE };

        SinkUDPSocketBlock(const char* name,
                           SocketType type,
                           const std::string& dest_host_or_path,
                           uint16_t port,
                           std::queue<size_t>& free_slots)
            : cler::BlockBase(name),
              _socket(type, dest_host_or_path, port),
              _free_slots(free_slots)
        {}

        cler::Result<cler::Empty, cler::Error> procedure() {
            if (!_socket.is_valid()) {
                return cler::Error::IOError;
            }
            if (in.size() == 0) {
                return cler::Error::NotEnoughSamples;
            }

            size_t to_send = std::min(in.size(), cler::DEFAULT_BUFFER_SIZE);
            for (size_t i = 0; i < to_send; ++i) {
                BlobSlice slice;
                in.pop(slice);
                if (_socket.send(slice.data, slice.len) < 0) {
                    return cler::Error::IOError;
                }
                _free_slots.push(slice.slot_idx);
            }

            return cler::Empty{};
        }

    private:
        GenericDatagramSocket _socket;
        std::queue<size_t>& _free_slots;
    };

} // namespace UDPBlock
