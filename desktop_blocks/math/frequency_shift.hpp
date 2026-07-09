#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <new>

struct FrequencyShiftBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    FrequencyShiftBlock(const char* name, const double frequency_shift_hz, const double sample_rate_hz,
        const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size), _frequency_shift(frequency_shift_hz), _sample_rate(sample_rate_hz) {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size;
        _buffer = new (std::nothrow) std::complex<float>[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }

        _dshift = std::exp(std::complex<float>(0.0, 2.0 * M_PI * _frequency_shift / _sample_rate));
    }

    ~FrequencyShiftBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        // in's buffer_size isn't validated to be >=4KB (custom sizes allowed), so dbf isn't
        // guaranteed available here; readN/writeN into a temp buffer stays correct for any size.
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        
        in.readN(_buffer, transferable);

        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] = _buffer[i] * _shifter;
            _shifter *= _dshift;
            // renormalize each sample: repeated multiplication drifts the phasor's magnitude from 1 via fp error
            _shifter /= std::abs(_shifter);
        }
        
        out->writeN(_buffer, transferable);
        return cler::Empty{};
    }

    private:
        double _frequency_shift;
        double _sample_rate;
        std::complex<float>* _buffer;
        size_t _buffer_size;
        std::complex<float> _shifter{1.0 ,0.0};
        std::complex<float> _dshift;
};