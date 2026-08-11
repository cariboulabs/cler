#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "polyphase_analyzer.hpp"
#include <complex>
#include <array>
#include <algorithm>
#include <limits>

template <size_t NUM_CHANNELS, size_t FILTER_SEMILENGTH>
struct PolyphaseChannelizerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    PolyphaseChannelizerBlock(std::string name,
                              float kaiser_attenuation,
                              size_t in_buffer_size = 0)
        : cler::BlockBase(std::move(name)),
          in(input_channel_elems(in_buffer_size)),
          _analyzer(kaiser_attenuation)
    {}

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        static_assert(sizeof...(OChannels) == NUM_CHANNELS,
                      "Number of output channels must match the number of polyphase channels");

        auto [read_ptr, read_size] = in.read_dbf();

        if (read_size < NUM_CHANNELS) {
            return cler::Error::NotEnoughSamples;
        }

        const size_t frames_by_contig = read_size / NUM_CHANNELS;

        std::array<std::complex<float>*, NUM_CHANNELS> ports;
        size_t min_write_space = std::numeric_limits<size_t>::max();

        size_t idx = 0;
        auto get_write_ptrs = [&](auto*... chs) {
            ([&] {
                auto [write_ptr, write_space] = chs->write_dbf();
                ports[idx] = write_ptr;
                min_write_space = std::min(min_write_space, write_space);
                idx++;
            }(), ...);
        };
        get_write_ptrs(outs...);

        const size_t num_frames = std::min(frames_by_contig, min_write_space);
        if (num_frames == 0) {
            return cler::Error::NotEnoughSpace;
        }

        _analyzer.execute(read_ptr, num_frames, ports.data());

        auto commit_writes = [&](auto*... chs) {
            ((chs->commit_write(num_frames)), ...);
        };
        commit_writes(outs...);

        in.commit_read(num_frames * NUM_CHANNELS);
        return cler::Empty{};
    }

private:
    static size_t input_channel_elems(size_t requested) {
        const size_t min_elems = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>);
        if (requested == 0) {
            return min_elems * NUM_CHANNELS;
        }
        if (requested < min_elems || (requested % NUM_CHANNELS) != 0) {
            cler::panic("Buffer size must be >= DOUBLY_MAPPED_MIN_SIZE elements and a multiple of num_channels");
        }
        return requested;
    }

    PolyphaseAnalyzer<NUM_CHANNELS, FILTER_SEMILENGTH> _analyzer;
};
