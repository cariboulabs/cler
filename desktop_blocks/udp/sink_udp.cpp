#include "sink_udp.hpp"

SinkUDPSocketBlock::SinkUDPSocketBlock(const char* name,
                    const UDPBlock::SocketType type,
                    const std::string& dest_host_or_path,
                    OnSendCallback callback,
                    void* callback_context,
                    const size_t buffer_size)
    : cler::BlockBase(name),
    in(buffer_size),
    _socket(UDPBlock::GenericDatagramSocket::make_sender(type, dest_host_or_path)),
    _callback(callback),
    _callback_context(callback_context)
{}

cler::Result<cler::Empty, cler::Error> SinkUDPSocketBlock::procedure() {
    if (!_socket.is_valid()) {
        return cler::Error::TERM_IOError;
    }
    if (in.size() == 0) {
        return cler::Error::NotEnoughSamples;
    }

    for (size_t i = 0; i < in.size(); ++i) {
        Blob blob;
        in.pop(blob);
        if (_socket.send(blob.data, blob.len) < 0) {
            return cler::Error::TERM_IOError;
        }
        if (_callback) {
            _callback(blob, _callback_context);
        }

        blob.release();
    }

    return cler::Empty{};
}
