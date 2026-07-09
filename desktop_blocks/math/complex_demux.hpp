#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"

struct ComplexToMagPhaseBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    enum Mode {
        MagPhase = 0,
        RealImag = 1
    };

    ComplexToMagPhaseBlock(const char* name, const Mode block_mode, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size), _block_mode(block_mode)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (block_mode != Mode::MagPhase && block_mode != Mode::RealImag) {
            cler::panic("Invalid block mode. Use MagPhase or RealImag.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* a_out,
        cler::ChannelBase<float>* b_out)
    {
        auto [read_ptr, read_size] = in.read_dbf();
        auto [a_ptr, a_space] = a_out->write_dbf();
        auto [b_ptr, b_space] = b_out->write_dbf();

        size_t transferable = std::min({read_size, a_space, b_space});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        for (size_t i = 0; i < transferable; ++i) {
            switch (_block_mode) {
                case Mode::MagPhase:
                    a_ptr[i] = std::abs(read_ptr[i]);
                    b_ptr[i] = std::arg(read_ptr[i]);
                    break;
                case Mode::RealImag:
                    a_ptr[i] = read_ptr[i].real();
                    b_ptr[i] = read_ptr[i].imag();
                    break;
            }
        }

        in.commit_read(transferable);
        a_out->commit_write(transferable);
        b_out->commit_write(transferable);

        return cler::Empty{};
    }

private:
    Mode _block_mode;
};
