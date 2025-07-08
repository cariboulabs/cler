#pragma once

#include "cler.hpp"
template <typename T>
struct SinkTerminalBlock : public cler::BlockBase {
    
    typedef size_t (*OnReceiveCallback)(cler::Channel<T>&, void* context);

    cler::Channel<T> in;

    SinkTerminalBlock(const char* name,
                      OnReceiveCallback callback = nullptr,
                      [[maybe_unusued]] void* callback_context = nullptr)
        : cler::BlockBase(name), in(cler::DEFAULT_BUFFER_SIZE), 
          _callback(callback), _callback_context(callback_context) {
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t to_commit;
        if (_callback) {
            to_commit = _callback(in, _callback_context);
        } else {
            to_commit = in.size();
        }
        in.commit_read(to_commit);
        return cler::Empty{};
    }

    private:
        OnReceiveCallback _callback = nullptr;
        void* _callback_context = nullptr;
};