#pragma once

#include "cler.hpp"
template <typename T>
struct SinkNullBlock : public cler::BlockBase {
    
    typedef size_t (*OnReceiveCallback)(cler::Channel<T>*, void* context);

    cler::Channel<T> in;

    SinkNullBlock(const char* name,
                      OnReceiveCallback callback = nullptr,
                      [[maybe_unused]] void* callback_context = nullptr,
                      size_t buffer_size = cler::DEFAULT_BUFFER_SIZE)
        : cler::BlockBase(name), in(buffer_size), _callback(callback), _callback_context(callback_context) {

        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        };
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t to_commit;
        if (_callback) {
            to_commit = _callback(&in, _callback_context);
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