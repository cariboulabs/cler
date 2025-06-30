#include "cler.hpp"
#include <iostream>
#include <chrono>

const size_t CHANNEL_SIZE = 512;
const size_t BATCH_SIZE = CHANNEL_SIZE / 2;

struct SourceBlock : public cler::BlockBase<SourceBlock> {
    SourceBlock(const char* name)  : BlockBase(name) {} 

    cler::Result<cler::Empty, ClerError> procedure_impl(
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

struct AdderBlock : public cler::BlockBase<AdderBlock> {
    cler::Channel<float> in0;
    cler::Channel<double> in1;

    AdderBlock(const char* name) : BlockBase(name), in0(CHANNEL_SIZE), in1(CHANNEL_SIZE) {}

    cler::Result<cler::Empty, ClerError> procedure_impl(cler::Channel<float>* out) {
        if (in0.size() < BATCH_SIZE || in1.size() < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }
        if (out->space() < BATCH_SIZE) {
            return ClerError::NotEnoughSpace;
        }

        size_t read;
        read = in0.readN(_a_values, BATCH_SIZE);
        if (read != BATCH_SIZE) {
            printf("Failed to read from in0, read: %zu, expected: %zu\n", read, BATCH_SIZE);
        }
        read = in1.readN(_b_values, BATCH_SIZE);
        if (read != BATCH_SIZE) {
            printf("Failed to read from in1, read: %zu, expected: %zu\n", read, BATCH_SIZE);
        }
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            _c_values[i] = _a_values[i] + static_cast<float>(_b_values[i]);
        }
        size_t written = out->writeN(_c_values, BATCH_SIZE);
        if (written != BATCH_SIZE) {
            printf("Failed to write to out, written: %zu, expected: %zu\n", written, BATCH_SIZE);
        }
        
        return cler::Empty{};
    }

    private:
    float _a_values[BATCH_SIZE] = {0.0};
    double _b_values[BATCH_SIZE] = {0.0};
    float _c_values[BATCH_SIZE] = {0.0};
};

struct GainBlock : public cler::BlockBase<GainBlock> {
    cler::Channel<float> in0;
    float gain;

    GainBlock(const char* name, float gain_value) : BlockBase(name), in0(CHANNEL_SIZE), gain(gain_value) {}

    cler::Result<cler::Empty, ClerError> procedure_impl(cler::Channel<float>* out) {
        if (in0.size() < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }
        if (out->space() < BATCH_SIZE) {
            return ClerError::NotEnoughSpace;
        }
        size_t read = in0.readN(_tmp, BATCH_SIZE);
        if (read != BATCH_SIZE) {
            printf("Failed to read from in0, read: %zu, expected: %zu\n", read, BATCH_SIZE);
        }
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            _tmp[i] *= gain;
        }
        size_t written = out->writeN(_tmp, BATCH_SIZE);
        if (written != BATCH_SIZE) {
            printf("Failed to write to out, written: %zu, expected: %zu\n", written, BATCH_SIZE);
        }
        return cler::Empty{};
    }

    private:
        float _tmp[BATCH_SIZE] = {0.0f};

};

struct SinkBlock : public cler::BlockBase<SinkBlock> {
    cler::Channel<float> in0;

    SinkBlock(const char* name) : BlockBase(name), in0(CHANNEL_SIZE) {
        _first_sample_time = std::chrono::steady_clock::now();
    }

    cler::Result<cler::Empty, ClerError> procedure_impl() {
        if (in0.size() < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        size_t read = in0.readN(_tmp, BATCH_SIZE);
        if (read != BATCH_SIZE) {
            printf("Failed to read from in0, read: %zu, expected: %zu\n", read, BATCH_SIZE);
        }
        _samples_processed += BATCH_SIZE;

        if (_samples_processed % (1000000 * BATCH_SIZE) == 0) {
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed_seconds = now - _first_sample_time;
            double sps = (_samples_processed / elapsed_seconds.count());
            std::cout << "Samples Per Second " << sps << std::endl;
        }
        return cler::Empty{};
    }

    private:
        uint64_t _samples_processed = 0;
        float _tmp[BATCH_SIZE] = {0.0f};
        std::chrono::steady_clock::time_point _first_sample_time;
};

int main() {
    SourceBlock source("Source");
    AdderBlock adder("Adder");
    GainBlock gain("Gain",2.0f);
    SinkBlock sink("Sink");

    cler::BlockRunner source_runner{&source, &adder.in0, &adder.in1};
    cler::BlockRunner adder_runner{&adder, &gain.in0};
    cler::BlockRunner gain_runner{&gain, &sink.in0};
    cler::BlockRunner sink_runner{&sink};

    cler::FlowGraph flowgraph(
        source_runner,
        adder_runner,
        gain_runner,
        sink_runner
    );

    flowgraph.run();


    while (true) {
        // Simulate some work in the main thread
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}