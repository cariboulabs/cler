#pragma once

#include "cler.hpp"
#include <type_traits>
#include <cstddef>

template <typename T>
struct SinkTerminalBlock : public cler::BlockBase {
    SinkTerminalBlock(const char* name)
        : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<T>* in) {
        size_t available = in->available();
        if (available == 0) {
            return cler::Error::NotEnoughData;
        }

        T tmp;
        for (size_t i = 0; i < available; ++i) {
            in->read(tmp);
            // Just discard `tmp`; it gets eaten.
        }

        return cler::Empty{};
    }
};