#include "sink_udp.hpp"

SinkUDPSocketBlock::SinkUDPSocketBlock(const char* name,
                    const UDPBlock::SocketType type,
                    const std::string& dest_host_or_path,
                    OnSendCallback callback,
                    void* callback_context,
                    const size_t buffer_size)
    : cler::BlockBase(name),
    in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(UDPBlock::BlobSlice) : buffer_size),
    _socket(UDPBlock::GenericDatagramSocket::make_sender(type, dest_host_or_path)),
    _callback(callback),
    _callback_context(callback_context)
{
    // If user provided a non-zero buffer size, validate it's sufficient
    if (buffer_size > 0 && buffer_size * sizeof(UDPBlock::BlobSlice) < cler::DOUBLY_MAPPED_MIN_SIZE) {
        throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " + 
            std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(UDPBlock::BlobSlice)) + " BlobSlice elements");
    }
    
    _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(UDPBlock::BlobSlice) : buffer_size;
}

cler::Result<cler::Empty, cler::Error> SinkUDPSocketBlock::procedure() {
    if (!_socket.is_valid()) {
        return cler::Error::TERM_IOError;
    }
    if (in.size() == 0) {
        return cler::Error::NotEnoughSamples;
    }

    size_t to_send = std::min(in.size(), _buffer_size);
    for (size_t i = 0; i < to_send; ++i) {
        UDPBlock::BlobSlice slice;
        in.pop(slice);
        if (_socket.send(slice.data, slice.len) < 0) {
            return cler::Error::TERM_IOError;
        }
        if (_callback) {
            _callback(slice, _callback_context);
        }

        slice.release();
    }

    return cler::Empty{};
}
