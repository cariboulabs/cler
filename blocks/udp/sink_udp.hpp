#pragma once

#include "cler.hpp"
#include "utils.hpp"
#include <queue>

struct SinkUDPSocketBlock : public cler::BlockBase {
    cler::Channel<UDPBlock::BlobSlice> in { cler::DEFAULT_BUFFER_SIZE };

    //we add a callback because sometimes we want to do something with the received data
    //that is not just pushing it to the output channel
    typedef void (*OnSendCallback)(const UDPBlock::BlobSlice&, void* context);

    SinkUDPSocketBlock(const char* name,
                        UDPBlock::SocketType type,
                        const std::string& dest_host_or_path,
                        uint16_t port,
                        std::queue<size_t>& slab_free_slots,
                        OnSendCallback callback = nullptr,
                        void* callback_context = nullptr)
        : cler::BlockBase(name),
        _socket(UDPBlock::GenericDatagramSocket::make_sender(type, dest_host_or_path, port)),
        _slab_free_slots(slab_free_slots),
        _callback(callback),
        _callback_context(callback_context)
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
            UDPBlock::BlobSlice slice;
            in.pop(slice);
            if (_socket.send(slice.data, slice.len) < 0) {
                return cler::Error::IOError;
            }
            _slab_free_slots.push(slice.slot_idx);
            if (_callback) {
                _callback(slice, _callback_context);
            }
        }

        return cler::Empty{};
    }

private:
    UDPBlock::GenericDatagramSocket _socket;
    std::queue<size_t>& _slab_free_slots;
    OnSendCallback _callback = nullptr;
    void* _callback_context = nullptr;
};
