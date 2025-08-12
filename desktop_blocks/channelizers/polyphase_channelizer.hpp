#include "cler.hpp"
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
          in(in_buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>)
                                 : in_buffer_size),
          _num_channels(num_channels)
    {
        // Validate buffer size if provided
        if (in_buffer_size > 0) {
            const size_t min_elems = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>);
            if (in_buffer_size < min_elems || (in_buffer_size % _num_channels) != 0) {
                throw std::invalid_argument("Buffer size must be ≥ " + 
                    std::to_string(min_elems) + 
                    " and a multiple of " + std::to_string(_num_channels));
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
            throw std::runtime_error("Failed to create polyphase channelizer");
        }

        // Pre-allocate temporary output buffer
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

        // Get the contiguous region from doubly-mapped input buffer
        auto [read_ptr, read_size] = in.read_dbf();
        
        if (read_size < _num_channels) {
            return cler::Error::NotEnoughSamples;
        }

        // Frames available contiguously
        const size_t frames_by_contig = read_size / _num_channels;
        
        // Get write_dbf pointers for all outputs
        std::array<std::pair<std::complex<float>*, size_t>, num_outs> write_ptrs;
        size_t min_write_space = std::numeric_limits<size_t>::max();
        
        size_t idx = 0;
        auto get_write_ptrs = [&](auto*... chs) {
            ((write_ptrs[idx] = chs->write_dbf(),
              min_write_space = std::min(min_write_space, write_ptrs[idx].second),
              idx++), ...);
        };
        get_write_ptrs(outs...);
        
        // How many frames can we process?
        size_t num_frames = std::min(frames_by_contig, min_write_space);
        
        if (num_frames == 0) {
            // Return NotEnoughSpace if CLER handles backpressure cleanly
            // Otherwise use: return cler::Empty{}; to avoid spin
            return cler::Error::NotEnoughSpace;
        }
        
        // Process frames and write DIRECTLY to output buffers - no intermediate storage!
        for (size_t i = 0; i < num_frames; ++i) {
            const std::complex<float>* frame = read_ptr + i * _num_channels;
            
            // Zero-copy input processing
            firpfbch_crcf_analyzer_execute(
                _pfch,
                as_liq_nonconst(frame),
                as_liq(_tmp_out.data())
            );
            
            // Scatter one sample per channel directly into its contiguous write span
            for (size_t ch = 0; ch < _num_channels; ++ch) {
                write_ptrs[ch].first[i] = _tmp_out[ch];
            }
        }
        
        // Commit all writes
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