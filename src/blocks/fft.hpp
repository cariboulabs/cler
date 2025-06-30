#pragma once

struct FFTBlock : public cler::BlockBase<FFTBlock> {
    cler::Channel<float> in0;
    cler::Channel<std::vector<float>> output_fft; // new output for GUI thread

    FFTBlock(size_t fft_size)
        : BlockBase("FFTBlock"), in0(fft_size * 2), output_fft(4) {}

    cler::Result<cler::Empty, ClerError> procedure_impl() {
        if (in0.size() < fft_size) {
            return ClerError::NotEnoughSamples;
        }

        std::vector<float> samples(fft_size);
        for (size_t i = 0; i < fft_size; ++i) {
            samples[i] = *in0.front();
            in0.pop();
        }

        std::vector<float> spectrum = compute_fft(samples);
        output_fft.push(spectrum);

        return cler::Empty{};
    }

    std::vector<float> compute_fft(const std::vector<float>& samples) {
        // Your actual FFT implementation
        std::vector<float> result(fft_size / 2, 0.0f);
        return result;
    }
};