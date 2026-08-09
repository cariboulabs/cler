#pragma once
#include "shared.hpp"
#include "../blob.hpp"
#include "cler_desktop_utils.hpp"
#include <new>

template<typename T>
struct SourceUDPSocketBlock : public cler::BlockBase {
    static constexpr bool IS_BLOB = std::is_same_v<T, Blob>;
    static constexpr bool may_block = true;

    typedef bool (*ValidateCallback)(const T&, void* context);
    typedef void (*OnReceiveCallback)(const T&, void* context);

    // Single constructor works for both Blob and generic types
    // For Blob: pass max_blob_size and num_slab_slots (required for pooling)
    // For generic fixed-size types: omit slab parameters (defaults are sufficient)
    SourceUDPSocketBlock(const char* name,
                        UDPBlock::SocketType type,
                        const std::string& bind_addr_or_path,
                        ValidateCallback validate = nullptr,
                        OnReceiveCallback callback = nullptr,
                        void* callback_context = nullptr,
                        size_t max_blob_size = 256, /*only used if IS_BLOB */
                        size_t num_slab_slots = 100, /*only used if IS_BLOB */
                        size_t buffer_size = 512,
                        std::chrono::milliseconds recv_timeout = std::chrono::milliseconds(100))
        : cler::BlockBase(name),
          _socket(UDPBlock::GenericDatagramSocket::make_receiver(type, bind_addr_or_path)),
          _slab(IS_BLOB ? num_slab_slots : 1, IS_BLOB ? max_blob_size : 0), //if not Blob, slab is dummy
          _validate(validate),
          _validate_context(callback_context),
          _callback(callback),
          _callback_context(callback_context),
          _buffer_size(buffer_size) {

        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
        _socket.set_receive_timeout(recv_timeout);
    }

    ~SourceUDPSocketBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (!_socket.is_valid()) {
            return cler::Error::TERM_IOError;
        }

        size_t available = std::min(out->space(), _buffer_size);
        if (available == 0) {
            return cler::Error::NotEnoughSpace;
        }

        T* buffer = _buffer;
        size_t count = 0;

        for (size_t i = 0; i < available; ++i) {
            const int recv_flags = (i == 0) ? 0 : MSG_DONTWAIT;

            if constexpr (IS_BLOB) {
                auto result = _slab.take_slot();
                if (result.is_err()) break;

                Blob blob = result.unwrap();
                ssize_t bytes = _socket.recv(blob.data, blob.len, recv_flags);

                if (bytes == 0) {
                    blob.release();
                    break;
                }

                if (bytes < 0) {
                    const int err = -bytes;
                    blob.release();
                    if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR || err == EMSGSIZE) {
                        break;
                    }
                    return cler::Error::TERM_IOError;
                }

                blob.len = static_cast<size_t>(bytes);

                if (_validate && !_validate(blob, _validate_context)) {
                    blob.release();
                    continue;
                }

                if (_callback) {
                    _callback(blob, _callback_context);
                }

                buffer[count++] = blob;
            } else {
                ssize_t bytes = _socket.recv(reinterpret_cast<uint8_t*>(&buffer[count]), sizeof(T), recv_flags);
                if (bytes <= 0) break;

                if (_validate && !_validate(buffer[count], _validate_context)) {
                    continue;
                }

                if (_callback) {
                    _callback(buffer[count], _callback_context);
                }

                count++;
            }
        }

        if (count == 0) {
            return cler::Error::NotEnoughSamples;
        }
        out->writeN(buffer, count);

        return cler::Empty{};
    }

private:
    UDPBlock::GenericDatagramSocket _socket;
    Slab _slab;  // Only used when IS_BLOB == true
    ValidateCallback _validate = nullptr;
    void* _validate_context = nullptr;
    OnReceiveCallback _callback = nullptr;
    void* _callback_context = nullptr;
    size_t _buffer_size;
    T* _buffer = nullptr;
};
