#include "cler.hpp"
#include "cler_utils.hpp"
#include "liquid.h"
#include <memory>
#include <complex>
#include <cassert>
#include <vector>
#include <algorithm>
#include <array>
#include <limits>

struct PolyphaseChannelizerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    PolyphaseChannelizerBlock(std::string name,
                              size_t num_channels,
                              float kaiser_attenuation,
                              size_t kaiser_filter_semilength,
                              size_t in_buffer_size = 0)
        : cler::BlockBase(std::move(name)),
          in(in_buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) * num_channels
                                 : in_buffer_size),
          _num_channels(num_channels)
    {
        if (in_buffer_size > 0) {
            const size_t min_elems = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>);
            if (in_buffer_size < min_elems || (in_buffer_size % _num_channels) != 0) {
                cler::panic("Buffer size must be >= DOUBLY_MAPPED_MIN_SIZE elements and a multiple of num_channels");
            }
        }

        assert(_num_channels > 0 && "Number of channels must be positive");
        assert(kaiser_filter_semilength > 0 && "Filter semilength must be positive");

        _pfch = firpfbch_crcf_create_kaiser(
            LIQUID_ANALYZER,
            _num_channels,
            kaiser_filter_semilength,
            kaiser_attenuation);

        if (!_pfch) {
            cler::panic("Failed to create polyphase channelizer");
        }

        _tmp_out.resize(_num_channels);
    }

    ~PolyphaseChannelizerBlock() {
        if (_pfch) {
            firpfbch_crcf_destroy(_pfch);
        }
    }

    // Helper: Convert std::complex<float>* to liquid_float_complex* 
    static inline liquid_float_complex* as_liq(std::complex<float>* p) {
        return reinterpret_cast<liquid_float_complex*>(p);
    }
    
    // Const cast version - Liquid's API doesn't use const even for read-only params
    static inline liquid_float_complex* as_liq_nonconst(const std::complex<float>* p) {
        return reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(p));
    }

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        assert(num_outs == _num_channels &&
               "Number of output channels must match the number of polyphase channels");

        auto [read_ptr, read_size] = in.read_dbf();

        if (read_size < _num_channels) {
            return cler::Error::NotEnoughSamples;
        }

        const size_t frames_by_contig = read_size / _num_channels;

        std::array<std::pair<std::complex<float>*, size_t>, num_outs> write_ptrs;
        size_t min_write_space = std::numeric_limits<size_t>::max();

        size_t idx = 0;
        auto get_write_ptrs = [&](auto*... chs) {
            ((write_ptrs[idx] = chs->write_dbf(),
              min_write_space = std::min(min_write_space, write_ptrs[idx].second),
              idx++), ...);
        };
        get_write_ptrs(outs...);

        size_t num_frames = std::min(frames_by_contig, min_write_space);
        if (num_frames == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < num_frames; ++i) {
            const std::complex<float>* frame = read_ptr + i * _num_channels;

            firpfbch_crcf_analyzer_execute(
                _pfch,
                as_liq_nonconst(frame),
                as_liq(_tmp_out.data())
            );

            // Each output channel is a separate contiguous write span; scatter one sample per channel.
            for (size_t ch = 0; ch < _num_channels; ++ch) {
                write_ptrs[ch].first[i] = _tmp_out[ch];
            }
        }

        idx = 0;
        auto commit_writes = [&](auto*... chs) {
            ((chs->commit_write(num_frames)), ...);
        };
        commit_writes(outs...);

        in.commit_read(num_frames * _num_channels);
        return cler::Empty{};
    }

private:
    size_t _num_channels = 0;
    std::vector<std::complex<float>> _tmp_out;  // Temporary for single frame output
    firpfbch_crcf _pfch = nullptr;
};