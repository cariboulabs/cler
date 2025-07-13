#pragma once

#include "cler.hpp"
#include "utils.hpp"
#include <queue>


struct SourceUDPSocketBlock : public cler::BlockBase {

    //we add a callback because sometimes we want to do something with the received data
    //that is not just pushing it to the output channel
    typedef void (*OnReceiveCallback)(const UDPBlock::BlobSlice&, void* context);

    SourceUDPSocketBlock(std::string name,
                            UDPBlock::SocketType type,
                            const std::string& bind_addr_or_path,
                            uint16_t port,
                            size_t max_blob_size,
                            size_t num_slab_slots = cler::DEFAULT_BUFFER_SIZE,
                            OnReceiveCallback callback = nullptr,
                            [[maybe_unused]] void* callback_context = nullptr)
        : cler::BlockBase(std::move(name)),
        _socket(UDPBlock::GenericDatagramSocket::make_receiver(type, bind_addr_or_path, port)),
        _slab(UDPBlock::Slab(num_slab_slots, max_blob_size)),
        _callback(callback)
            
    {}

    void set_callback(OnReceiveCallback cb) {
        _callback = cb;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<UDPBlock::BlobSlice>* out) {
        if (!_socket.is_valid()) {
            return cler::Error::IOError;
        }
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < out->space(); ++i) {
            auto result = _slab.take_slot();
            if (result.is_err()) {
                return result.unwrap_err();
            }
            UDPBlock::BlobSlice slice = result.unwrap();

            // Receive data into the allocated slab slot
            ssize_t bytes_received = _socket.recv(slice.data, slice.len);
            if (bytes_received  == -1) {
                slice.release();
                return cler::Error::IOError;
            }
            if (bytes_received == -2) {
                slice.release();
                continue;
            }

            slice.len = static_cast<size_t>(bytes_received);

            if (_callback) {
                _callback(slice, _callback_context);
            }
            out->push(slice);
        }

        return cler::Empty{};
    }

private:
    UDPBlock::GenericDatagramSocket _socket;
    UDPBlock::Slab _slab; 
    OnReceiveCallback _callback;
    void* _callback_context = nullptr; // Optional context for callback
};
