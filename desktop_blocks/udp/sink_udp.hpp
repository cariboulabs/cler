#pragma once
#include "shared.hpp"

struct SinkUDPSocketBlock : public cler::BlockBase {
    cler::Channel<UDPBlock::BlobSlice> in;
    typedef void (*OnSendCallback)(const UDPBlock::BlobSlice&, void* context);

    SinkUDPSocketBlock(const char* name,
                        const UDPBlock::SocketType type,
                        const std::string& dest_host_or_path,
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
