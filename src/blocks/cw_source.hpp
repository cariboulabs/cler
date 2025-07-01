#include "cler.hpp"
#include <cmath>
#include <complex>

// A source block that generates a complex float continuous wave (CW) signal.
struct CWSourceBlock : public cler::BlockBase {

    CWSourceBlock(const char* name, int frequency_hz, int sample_rate_sps, size_t work_size):
    cler::BlockBase(name), _work_size(work_size), 
    _frequency_hz(frequency_hz), _sample_rate_sps(sample_rate_sps) {
        if (_work_size == 0 || _sample_rate_sps == 0) {
            throw std::invalid_argument("Work size and sample rate must be greater than zero.");
        }
        if (_frequency_hz < 0) {
            throw std::invalid_argument("Frequency must be non-negative.");
        }

        _tmp = new std::complex<float>[_work_size]; // Temporary buffer for work size
    }

    ~CWSourceBlock() {
        delete[] _tmp; // Clean up the temporary buffer
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<std::complex<float>>* out) {
        if (out->space() < _work_size) {
            return cler::Error::NotEnoughSpace;
        }

        // Generate the CW signal
        static float phase = 0.0f;
        const float phase_increment = 2.0f * M_PI * _frequency_hz / _sample_rate_sps;
        for (size_t i = 0; i < _work_size; ++i) {
            _tmp[i] = std::polar(1.0f, phase); // Generate a complex number with magnitude 1 and phase
            phase += phase_increment; // Increment the phase
            if (phase >= 2.0f * M_PI) {
                phase -= 2.0f * M_PI; // Wrap around the phase so it doesnt explode to infinity
            }
        }
        out->writeN(_tmp, _work_size);

        return cler::Empty{};
    }

    private:
        size_t _work_size;
        std::complex<float>* _tmp; 
        int _frequency_hz;
        int _sample_rate_sps;
};