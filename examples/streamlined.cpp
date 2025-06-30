#include "cler.hpp"
#include "utils.hpp"
#include <chrono>

const size_t CHANNEL_SIZE = 512;
const size_t BATCH_SIZE = CHANNEL_SIZE / 2;

struct SourceBlock : public cler::BlockBase {
    SourceBlock(const char* name)  : BlockBase(name) {} 

    cler::Result<cler::Empty, ClerError> procedure(
        cler::Channel<float>* out0,
        cler::Channel<double>* out1) {
        if (out0->space() < BATCH_SIZE || out1->space() < BATCH_SIZE) {
            return ClerError::NotEnoughSpace;
        }
        size_t written;
        written = out0->writeN(_ones, BATCH_SIZE);
        if (written != BATCH_SIZE) {
            printf("Failed to write to out0, written: %zu, expected: %zu\n", written, BATCH_SIZE);
        }
        written = out1->writeN(_twos, BATCH_SIZE);
        if (written != BATCH_SIZE) {
            printf("Failed to write to out1, written: %zu, expected: %zu\n", written, BATCH_SIZE);
        }
        return cler::Empty{};
    }

    private:
        const float _ones[BATCH_SIZE] = {1.0f};
        const double _twos[BATCH_SIZE] = {2.0};
};

struct AdderBlock : public cler::BlockBase {
    cler::Channel<float> in0;
    cler::Channel<double> in1;

    AdderBlock(const char* name) : BlockBase(name), in0(CHANNEL_SIZE), in1(CHANNEL_SIZE) {}

    //                                             Adderblock pushes to gain block which has a stack buffer!
    cler::Result<cler::Empty, ClerError> procedure(cler::Channel<float, CHANNEL_SIZE>* out) {
        const float* a_values;
        size_t a_available;
        size_t a_readable = in0.peek_read(a_values, BATCH_SIZE, &a_available);
        if (a_available < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        const double* b_values;
        size_t b_available;
        size_t b_readable = in1.peek_read(b_values, BATCH_SIZE, &b_available);
        if (b_available < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        size_t c_available;
        float* c_values;
        size_t c_space = out->peek_write(c_values, BATCH_SIZE, &c_available);
        if (c_available < BATCH_SIZE) {
            return ClerError::NotEnoughSpace;
        }

        size_t n_work_samples = std::min(a_readable, b_readable);
        for (size_t i = 0; i < n_work_samples; ++i) {
            c_values[i] = a_values[i] + static_cast<float>(b_values[i]);
        }

        in0.commit_read(n_work_samples);
        in1.commit_read(n_work_samples);
        out->commit_write(n_work_samples);
        
        return cler::Empty{};
    }
};

struct GainBlock : public cler::BlockBase {
    cler::Channel<float, CHANNEL_SIZE> in; //this is a stack buffer!
    float gain;

    GainBlock(const char* name, float gain_value) : BlockBase(name), gain(gain_value) {}

    cler::Result<cler::Empty, ClerError> procedure(cler::Channel<float>* out) {
        const float * in_values;
        size_t in_available;
        size_t in_readable = in.peek_read(in_values, BATCH_SIZE, &in_available);
        if (in_available < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        size_t out_available;
        float* out_values;
        size_t out_space = out->peek_write(out_values, BATCH_SIZE, &out_available);
        if (out_available < BATCH_SIZE) {
            return ClerError::NotEnoughSpace;
        }

        size_t n_work_samples = std::min(in_readable, out_space);
        for (size_t i = 0; i < n_work_samples; ++i) {
            out_values[i] = in_values[i] * gain;
        }

        in.commit_read(n_work_samples);
        out->commit_write(n_work_samples);

        return cler::Empty{};
    }
};

struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(const char* name) : BlockBase(name), in(CHANNEL_SIZE) {
        _first_sample_time = std::chrono::steady_clock::now();
    }

    cler::Result<cler::Empty, ClerError> procedure() {
        if (in.size() < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        const float* values;
        size_t available;
        size_t read = in.peek_read(values, BATCH_SIZE, &available);
        if (available < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        _samples_processed += read;

        if (_samples_processed % (1000000 * BATCH_SIZE) < BATCH_SIZE) {
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed_seconds = now - _first_sample_time;
            double sps = (_samples_processed / elapsed_seconds.count());
            std::cout << "Samples Per Second " << sps << std::endl;
        }
        
        in.commit_read(read);
        return cler::Empty{};
    }

    private:
        uint64_t _samples_processed = 0;
        std::chrono::steady_clock::time_point _first_sample_time;
};

int main() {
    SourceBlock source("Source");
    AdderBlock adder("Adder");
    GainBlock gain("Gain",2.0f);
    SinkBlock sink("Sink");

    cler::Result<cler::Empty, ClerError> res = cler::Empty{};

    while (true) {
        res = source.procedure(&adder.in0, &adder.in1);
        res = adder.procedure(&gain.in);
        res = gain.procedure(&sink.in);
        res = sink.procedure();
    }
}
