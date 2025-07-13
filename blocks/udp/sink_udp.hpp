#pragma once

#include "cler.hpp"
#include "utils.hpp"
#include <queue>

struct SinkUDPSocketBlock : public cler::BlockBase {
    cler::Channel<UDPBlock::BlobSlice> in;

    //we add a callback because sometimes we want to do something with the received data
    //that is not just pushing it to the output channel
    typedef void (*OnSendCallback)(const UDPBlock::BlobSlice&, void* context);

    SinkUDPSocketBlock(std::string name,
                        const UDPBlock::SocketType type,
                        const std::string& dest_host_or_path,
                        const uint16_t port,
                        OnSendCallback callback = nullptr,
                        void* callback_context = nullptr,
                        const size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(std::move(name)),
        in(buffer_size),
        _socket(UDPBlock::GenericDatagramSocket::make_sender(type, dest_host_or_path, port)),
        _callback(callback),
        _callback_context(callback_context)
    {
        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }
        _buffer_size = buffer_size;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (!_socket.is_valid()) {
            return cler::Error::IOError;
        }
        if (in.size() == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t to_send = std::min(in.size(), _buffer_size);
        for (size_t i = 0; i < to_send; ++i) {
            UDPBlock::BlobSlice slice;
            in.pop(slice);
            if (_socket.send(slice.data, slice.len) < 0) {
                return cler::Error::IOError;
            }
            if (_callback) {
                _callback(slice, _callback_context);
            }

            slice.release();
        }

        return cler::Empty{};
    }

private:
    UDPBlock::GenericDatagramSocket _socket;
    OnSendCallback _callback = nullptr;
    void* _callback_context = nullptr;
    size_t _buffer_size;
};
