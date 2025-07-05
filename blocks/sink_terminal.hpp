#pragma once

#include "cler.hpp"
#include <type_traits>
#include <cstddef>

template <typename T>
struct SinkTerminalBlock : public cler::BlockBase {
    SinkTerminalBlock(const char* name)
        : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* in) {
        in.commit_read(in->size()); // Commit all read samples
        return cler::Empty{};
    }
};