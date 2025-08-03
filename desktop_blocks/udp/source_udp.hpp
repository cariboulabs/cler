#pragma once
#include "shared.hpp"
#include "../blob.hpp"
struct SourceUDPSocketBlock : public cler::BlockBase {

    //we add a callback because sometimes we want to do something with the received data
    //that is not just pushing it to the output channel
    typedef void (*OnReceiveCallback)(const Blob&, void* context);

    SourceUDPSocketBlock(const char* name,
                            UDPBlock::SocketType type,
                            const std::string& bind_addr_or_path,
                            size_t max_blob_size,
                            size_t num_slab_slots = 100,
                            OnReceiveCallback callback = nullptr,
                            [[maybe_unused]] void* callback_context = nullptr);

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Blob>* out);

    private:
        UDPBlock::GenericDatagramSocket _socket;
        Slab _slab;
        OnReceiveCallback _callback;
        void* _callback_context = nullptr; // Optional context for callback
};
