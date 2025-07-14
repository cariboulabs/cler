#pragma once
#include "utils.hpp"

struct SinkUDPSocketBlock : public cler::BlockBase {
    cler::Channel<UDPBlock::BlobSlice> in;
    typedef void (*OnSendCallback)(const UDPBlock::BlobSlice&, void* context);

    SinkUDPSocketBlock(std::string name,
                        const UDPBlock::SocketType type,
                        const std::string& dest_host_or_path,
                        const uint16_t port,
                        OnSendCallback callback = nullptr,
                        void* callback_context = nullptr,
                        const size_t buffer_size = cler::DEFAULT_BUFFER_SIZE);
    cler::Result<cler::Empty, cler::Error> procedure();

private:
    UDPBlock::GenericDatagramSocket _socket;
    OnSendCallback _callback = nullptr;
    void* _callback_context = nullptr;
    size_t _buffer_size;
};
