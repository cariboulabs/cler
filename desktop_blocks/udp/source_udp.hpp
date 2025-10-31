#pragma once
#include "shared.hpp"
#include "../blob.hpp"

template<typename T>
struct SourceUDPSocketBlock : public cler::BlockBase {
    static constexpr bool IS_BLOB = std::is_same_v<T, Blob>;

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
                        size_t num_slab_slots = 100) /*only used if IS_BLOB */
        : cler::BlockBase(name),
          _socket(UDPBlock::GenericDatagramSocket::make_receiver(type, bind_addr_or_path)),
          _slab(IS_BLOB ? num_slab_slots : 1, IS_BLOB ? max_blob_size : 0), //if not Blob, slab is dummy
          _validate(validate),
          _validate_context(callback_context),
          _callback(callback),
          _callback_context(callback_context) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (!_socket.is_valid()) {
            return cler::Error::TERM_IOError;
        }

        size_t available = out->space();
        if (available == 0) {
            return cler::Error::NotEnoughSpace;
        }

        T buffer[available];
        size_t count = 0;

        for (size_t i = 0; i < available; ++i) {
            if constexpr (IS_BLOB) {
                // Blob-specific: slab pooling
                auto result = _slab.take_slot();
                if (result.is_err()) break;

                Blob blob = result.unwrap();
                ssize_t bytes = _socket.recv(blob.data, blob.len);

                if (bytes == 0) {
                    blob.release();
                    return cler::Empty{};
                }

                if (bytes < 0) {
                    int err = -bytes;
                    switch (err) {
                        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                        case EWOULDBLOCK:
#endif
                        case EINTR:
                            blob.release();
                            return cler::Empty{};

                        case EMSGSIZE:
                            blob.release();
                            return cler::Empty{};

                        default:
                            blob.release();
                            return cler::Error::TERM_IOError;
                    }
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
                // Generic fixed-size
                ssize_t bytes = _socket.recv(reinterpret_cast<uint8_t*>(&buffer[i]), sizeof(T));
                if (bytes <= 0) break;

                if (_validate && !_validate(buffer[i], _validate_context)) {
                    continue;
                }

                if (_callback) {
                    _callback(buffer[i], _callback_context);
                }

                count++;
            }
        }

        if (count > 0) {
            out->writeN(buffer, count);
        }

        return cler::Empty{};
    }

private:
    UDPBlock::GenericDatagramSocket _socket;
    Slab _slab;  // Only used when IS_BLOB == true
    ValidateCallback _validate = nullptr;
    void* _validate_context = nullptr;
    OnReceiveCallback _callback = nullptr;
    void* _callback_context = nullptr;
};
