#include "cler.hpp"

struct FrequencyShiftBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    FrequencyShiftBlock(const char* name, const double frequency_shift_hz, const double sample_rate_hz,
        const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size), _frequency_shift(frequency_shift_hz), _sample_rate(sample_rate_hz) {

        // Allocate temporary buffer for readN/writeN operations
        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size;
        _buffer = new std::complex<float>[_buffer_size];
        if (!_buffer) {
            throw std::bad_alloc();
        }

        _dshift = std::exp(std::complex<float>(0.0, 2.0 * M_PI * _frequency_shift / _sample_rate));
    }

    ~FrequencyShiftBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        // Use readN/writeN for simple processing (recommended pattern)
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        
        in.readN(_buffer, transferable);

        // Process buffer
        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] = _buffer[i] * _shifter;
            _shifter *= _dshift;
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