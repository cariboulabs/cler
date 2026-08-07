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

template<typename Kernel, typename = void>
struct fused_kernel_traits {
    static_assert(!std::is_same_v<Kernel, Kernel>,
        "FusedBlock: Kernel must expose `Out operator()(In)`");
};

template<typename Kernel>
struct fused_kernel_traits<Kernel, std::void_t<decltype(&Kernel::operator())>>
    : fused_member_fn_traits<decltype(&Kernel::operator())> {};

template<typename... Kernels>
struct fused_chain;

template<typename Kernel>
struct fused_chain<Kernel> {
    using In = typename fused_kernel_traits<Kernel>::In;
    using Out = typename fused_kernel_traits<Kernel>::Out;
};

template<typename Kernel0, typename Kernel1, typename... Rest>
struct fused_chain<Kernel0, Kernel1, Rest...> {
    using Head = fused_kernel_traits<Kernel0>;
    using Next = fused_chain<Kernel1, Rest...>;
    static_assert(std::is_same_v<typename Head::Out, typename Next::In>,
        "FusedBlock: adjacent kernels in chain have mismatched operator() Out/In types");
    using In = typename Head::In;
    using Out = typename Next::Out;
};

template<typename... Kernels>
struct FusedBlock : public cler::BlockBase {
    static_assert(sizeof...(Kernels) >= 1, "FusedBlock requires at least one kernel");

    using FirstIn = typename fused_chain<Kernels...>::In;
    using LastOut = typename fused_chain<Kernels...>::Out;

    cler::Channel<FirstIn> in;

    FusedBlock(const char* name, Kernels... kernels, const size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(FirstIn) : buffer_size),
          _kernels(std::move(kernels)...)
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
    std::tuple<Kernels...> _kernels;

    template<size_t I, typename Val>
    auto apply_chain(Val v) {
        if constexpr (I == sizeof...(Kernels)) {
            return v;
        } else {
            return apply_chain<I + 1>(std::get<I>(_kernels)(v));
        }
    }
};
