#include "source_udp.hpp"

SourceUDPSocketBlock::SourceUDPSocketBlock(const char* name,
                        UDPBlock::SocketType type,
                        const std::string& bind_addr_or_path,
                        size_t max_blob_size,
                        size_t num_slab_slots,
                        OnReceiveCallback callback,
                        [[maybe_unused]] void* callback_context)
    : cler::BlockBase(name),
    _socket(UDPBlock::GenericDatagramSocket::make_receiver(type, bind_addr_or_path)),
    _slab(Slab(num_slab_slots, max_blob_size)),
    _callback(callback)    
{}

cler::Result<cler::Empty, cler::Error> SourceUDPSocketBlock::procedure(cler::ChannelBase<Blob>* out) {
    if (!_socket.is_valid()) {
        return cler::Error::TERM_IOError;
    }
    if (out->space() == 0) {
        return cler::Error::NotEnoughSpace;
    }

    for (size_t i = 0; i < out->space(); ++i) {
        auto result = _slab.take_slot();
        if (result.is_err()) {
            return result.unwrap_err();
        }
        Blob blob = result.unwrap();
        // Receive data into the allocated slab slot
        ssize_t bytes_received = _socket.recv(blob.data, blob.len);

        if (bytes_received == 0) {
            // No data received: safe to release slot and exit
            blob.release();
            return cler::Empty{};
        }

        if (bytes_received < 0) {
            int err = -bytes_received;  // Get real errno

            switch (err) {
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                case EINTR:
                    // Harmless transient error: try again later
                    blob.release();
                    return cler::Empty{};

                case EMSGSIZE:
                    // Datagram was too big: drop this one
                    blob.release();
                    return cler::Empty{};

                default:
                    // Real I/O error: propagate as terminal
                    blob.release();
                    return cler::Error::TERM_IOError;
            }
        }

        blob.len = static_cast<size_t>(bytes_received);

        if (_callback) {
            _callback(blob, _callback_context);
        }
        out->push(blob);
    }

    return cler::Empty{};
}