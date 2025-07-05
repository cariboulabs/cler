#pragma once

#include "cler.hpp"
template <typename T>
struct SinkTerminalBlock : public cler::BlockBase {
    cler::Channel<T> in;

    SinkTerminalBlock(const char* name)
        : cler::BlockBase(name), in(cler::DEFAULT_BUFFER_SIZE) {
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        in.commit_read(in.size()); // Commit all read samples
        return cler::Empty{};
    }
};