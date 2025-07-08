#pragma once

#include "cler.hpp"
#include "utils.hpp"
#include <queue>

namespace UDPBlock {

    struct SourceUDPSocketBlock : public cler::BlockBase {

        //we add a callback because sometimes we want to do something with the received data
        //that is not just pushing it to the output channel
        typedef void (*OnReceiveCallback)(const BlobSlice&, void* context);

        SourceUDPSocketBlock(const char* name,
                             SocketType type,
                             const std::string& bind_addr_or_path,
                             uint16_t port,
                             uint8_t* slab,
                             size_t blob_size,
                             std::queue<size_t>& free_slots,
                            OnReceiveCallback callback = nullptr,
                            void* callback_context = nullptr)
            : cler::BlockBase(name),
            _socket(type, "", 0),
            _slab(slab),
            _blob_size(blob_size),
            _free_slots(free_slots),
            _callback(callback)
              
        {
            _socket.bind(bind_addr_or_path, port);
        }

        void set_callback(OnReceiveCallback cb) {
            _callback = cb;
        }

        cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<BlobSlice>* out) {
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
            out->push(slice);
            _free_slots.pop();

            if (_callback) {
                _callback(slice, _callback_context);
            }

            return cler::Empty{};
        }

    private:
        GenericDatagramSocket _socket;
        uint8_t* _slab;
        size_t _blob_size;
        std::queue<size_t>& _free_slots;
        OnReceiveCallback _callback;
        void* _callback_context = nullptr; // Optional context for callback
    };

} // namespace UDPBlock
