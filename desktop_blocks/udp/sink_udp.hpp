#pragma once
#include "shared.hpp"
#include "../blob.hpp"

struct SinkUDPSocketBlock : public cler::BlockBase {
    cler::Channel<Blob> in;
    typedef void (*OnSendCallback)(const Blob&, void* context);

    SinkUDPSocketBlock(const char* name,
                        const UDPBlock::SocketType type,
                        const std::string& dest_host_or_path,
                        OnSendCallback callback = nullptr,
                        void* callback_context = nullptr,
                        const size_t buffer_size = 512);
    cler::Result<cler::Empty, cler::Error> procedure();

private:
    UDPBlock::GenericDatagramSocket _socket;
    OnSendCallback _callback = nullptr;
    void* _callback_context = nullptr;
};
