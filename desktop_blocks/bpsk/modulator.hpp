#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/bpsk/bpsk_common.hpp"
#include <algorithm>
#include <complex>
#include <vector>

// +-1 BPSK symbols in (0 = silence), RRC-shaped complex baseband out at `sps`
// samples per symbol. Taps have unit energy, so unit symbols give Es = 1 and the
// mean output power is Es / sps, the same convention as LinearModulatorBlock: the
// AWGN level from awgn_stddev_for_esn0_db() means the same Es/N0 for both.
// Polyphase interpolator, group delay span_symbols / 2 symbols.
struct BPSKModulatorBlock : public cler::BlockBase {
    cler::Channel<float> in;

    BPSKModulatorBlock(const char* name, unsigned int sps, float beta = 0.35f,
        unsigned int span_symbols = 10, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _sps(sps), _ntaps_per_phase(span_symbols + 1) {
        if (sps < 2) cler::panic("BPSKModulatorBlock requires samples/symbol >= 2");
        if (span_symbols == 0 || span_symbols % 2 != 0) cler::panic("BPSKModulatorBlock span must be even and > 0");
        if (buffer_size * sizeof(float) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        const std::vector<float> h = bpsk::rrc_taps(sps, span_symbols, beta);
        // Phase p uses taps h[p + j*sps], j = 0..span: newest symbol first.
        _phases.assign(static_cast<size_t>(sps) * _ntaps_per_phase, 0.0f);
        for (size_t p = 0; p < sps; ++p) {
            for (size_t j = 0; j < _ntaps_per_phase; ++j) {
                const size_t idx = p + j * sps;
                _phases[p * _ntaps_per_phase + j] = idx < h.size() ? h[idx] : 0.0f;
            }
        }
        _hist.assign(2 * _ntaps_per_phase, 0.0f);
    }

    unsigned int samples_per_symbol() const { return _sps; }
    unsigned int delay_symbols() const { return static_cast<unsigned int>(_ntaps_per_phase - 1) / 2; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        auto [wptr, wsize] = out->write_dbf();
        const size_t n = std::min(rsize, wsize / _sps);
        if (n == 0) {
            out->commit_write(0);
            return cler::Error::NotEnoughSpace;
        }
        const size_t L = _ntaps_per_phase;
        for (size_t i = 0; i < n; ++i) {
            // Doubled history ring: the newest L symbols are always contiguous at _head.
            _head = (_head == 0) ? L - 1 : _head - 1;
            _hist[_head] = _hist[_head + L] = rptr[i];
            const float* s = _hist.data() + _head;
            for (size_t p = 0; p < _sps; ++p) {
                const float* h = _phases.data() + p * L;
                float acc = 0.0f;
                for (size_t j = 0; j < L; ++j) acc += h[j] * s[j];
                wptr[i * _sps + p] = std::complex<float>(acc, 0.0f);
            }
        }
        in.commit_read(n);
        out->commit_write(n * _sps);
        return cler::Empty{};
    }

private:
    size_t _sps;
    size_t _ntaps_per_phase;
    std::vector<float> _phases;
    std::vector<float> _hist;
    size_t _head = 0;
};
