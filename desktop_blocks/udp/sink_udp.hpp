#pragma once
#include "shared.hpp"
#include "../blob.hpp"

template<typename T>
struct SinkUDPSocketBlock : public cler::BlockBase {
    static constexpr bool IS_BLOB = std::is_same_v<T, Blob>;

    cler::Channel<T> in;
    typedef void (*OnSendCallback)(const T&, void* context);

    // Constructor - same signature for both Blob and non-Blob
    // (input channel size is always relevant)
    SinkUDPSocketBlock(const char* name,
                      const UDPBlock::SocketType type,
                      const std::string& dest_host_or_path,
                      OnSendCallback callback = nullptr,
                      void* callback_context = nullptr,
                      const size_t buffer_size = 512)
        : cler::BlockBase(name),
          in(buffer_size),
          _socket(UDPBlock::GenericDatagramSocket::make_sender(type, dest_host_or_path)),
          _callback(callback),
          _callback_context(callback_context) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (!_socket.is_valid()) {
            return cler::Error::TERM_IOError;
        }

        size_t available = in.size();
        if (available == 0) {
            return cler::Empty{};
        }

        T buffer[available];
        in.readN(buffer, available);

        for (size_t i = 0; i < available; ++i) {
            ssize_t bytes;

            if constexpr (IS_BLOB) {
                // Blob-specific: send blob data and release slot
                bytes = _socket.send(buffer[i].data, buffer[i].len);
                if (_callback) {
                    _callback(buffer[i], _callback_context);
                }
                buffer[i].release();
            } else {
                // Generic fixed-size
                bytes = _socket.send(reinterpret_cast<const uint8_t*>(&buffer[i]), sizeof(T));
                if (_callback) {
                    _callback(buffer[i], _callback_context);
                }
            }

            if (bytes < 0) {
                return cler::Error::TERM_IOError;
            }
        }

        return cler::Empty{};
    }

private:
    UDPBlock::GenericDatagramSocket _socket;
    OnSendCallback _callback = nullptr;
    void* _callback_context = nullptr;
};
