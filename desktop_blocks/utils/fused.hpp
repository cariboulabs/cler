#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <tuple>
#include <type_traits>

template<typename Ptr>
struct fused_member_fn_traits;

template<typename R, typename C, typename A>
struct fused_member_fn_traits<R (C::*)(A)> {
    using In = std::decay_t<A>;
    using Out = R;
};

template<typename R, typename C, typename A>
struct fused_member_fn_traits<R (C::*)(A) const> {
    using In = std::decay_t<A>;
    using Out = R;
};

template<typename Block, typename = void>
struct fused_kernel_traits {
    static_assert(!std::is_same_v<Block, Block>,
        "FusedBlock: Block must expose a per-sample kernel `Out processOne(In)`");
};

template<typename Block>
struct fused_kernel_traits<Block, std::void_t<decltype(&Block::processOne)>>
    : fused_member_fn_traits<decltype(&Block::processOne)> {};

template<typename... Blocks>
struct fused_chain;

template<typename Block>
struct fused_chain<Block> {
    using In = typename fused_kernel_traits<Block>::In;
    using Out = typename fused_kernel_traits<Block>::Out;
};

template<typename Block0, typename Block1, typename... Rest>
struct fused_chain<Block0, Block1, Rest...> {
    using Kernel0 = fused_kernel_traits<Block0>;
    using Next = fused_chain<Block1, Rest...>;
    static_assert(std::is_same_v<typename Kernel0::Out, typename Next::In>,
        "FusedBlock: adjacent blocks in chain have mismatched processOne Out/In types");
    using In = typename Kernel0::In;
    using Out = typename Next::Out;
};

template<typename... Blocks>
struct FusedBlock : public cler::BlockBase {
    static_assert(sizeof...(Blocks) >= 1, "FusedBlock requires at least one block");

    using FirstIn = typename fused_chain<Blocks...>::In;
    using LastOut = typename fused_chain<Blocks...>::Out;

    cler::Channel<FirstIn> in;

    FusedBlock(const char* name, Blocks*... blocks, const size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(FirstIn) : buffer_size),
          _blocks(blocks...)
    {
        if (buffer_size > 0 && buffer_size * sizeof(FirstIn) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<LastOut>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_space] = out->write_dbf();
        if (!write_ptr || write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t transferable = std::min(read_size, write_space);

        for (size_t i = 0; i < transferable; ++i) {
            write_ptr[i] = apply_chain<0>(read_ptr[i]);
        }

        in.commit_read(transferable);
        out->commit_write(transferable);

        return cler::Empty{};
    }

private:
    std::tuple<Blocks*...> _blocks;

    template<size_t I, typename Val>
    auto apply_chain(Val v) {
        if constexpr (I == sizeof...(Blocks)) {
            return v;
        } else {
            return apply_chain<I + 1>(std::get<I>(_blocks)->processOne(v));
        }
    }
};
