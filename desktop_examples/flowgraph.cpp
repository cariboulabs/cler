#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>

const size_t CHANNEL_SIZE = 512;

struct SourceBlock : public cler::BlockBase {
    SourceBlock(const char* name)  : BlockBase(name) {
        for (size_t i = 0; i < CHANNEL_SIZE; ++i) {
            _ones[i] = 1.0;
            _twos[i] = 2.0f;
        }
    } 

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* out0,
        cler::ChannelBase<double>* out1) {

        //this is faster than pushing one by one
        out0->writeN(_ones, out0->space());
        out1->writeN(_twos, out1->space());
        return cler::Empty{};
    }

    private:
        float _ones[CHANNEL_SIZE];
        double _twos[CHANNEL_SIZE];
};
struct AdderBlock : public cler::BlockBase {
    cler::Channel<float> in0;
    cler::Channel<double> in1;

    AdderBlock(const char* name) : BlockBase(name), in0(CHANNEL_SIZE), in1(CHANNEL_SIZE) {}

    //                                             Adderblock pushes to gain block which has a stack buffer!
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t in_size = std::min({in0.size(), in1.size()});
        if (in_size == 0) {
            return cler::Error::NotEnoughSamples; // No samples to process
        }
        size_t out_size = out->space();
        if (out_size == 0) {
            return cler::Error::NotEnoughSpace; // No space to write
        }
        size_t transferable = std::min(in_size, out_size);

        in0.readN(_tmp1, transferable);
        in1.readN(_tmp2, transferable);
        for (size_t i = 0; i < transferable; ++i) {
            _tmp1[i] += static_cast<float>(_tmp2[i]);
        }
        out->writeN(_tmp1, transferable);
        return cler::Empty{};
    }

    private:
        float _tmp1[CHANNEL_SIZE]; // Temporary buffer for processing
        double _tmp2[CHANNEL_SIZE]; // Temporary buffer for processing
};

struct GainBlock : public cler::BlockBase {
    cler::Channel<float, CHANNEL_SIZE> in; //this is a stack buffer!
    float gain;

    GainBlock(const char* name, float gain_value) : BlockBase(name), gain(gain_value) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t in_size = in.size();
        if (in_size == 0) {
            return cler::Error::NotEnoughSamples; // No samples to process
        }
        size_t out_size = out->space();
        if (out_size == 0) {
            return cler::Error::NotEnoughSpace; // No space to write
        }
        size_t transferable = std::min(in_size, out_size);

        in.readN(_tmp, transferable);
        for (size_t i = 0; i < transferable; ++i) {
            _tmp[i] *= gain;
        }
        out->writeN(_tmp, transferable);
        return cler::Empty{};
    }

    private:
        float _tmp[CHANNEL_SIZE];
};

struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(const char* name) : BlockBase(name), in(CHANNEL_SIZE) {
        _first_sample_time = std::chrono::steady_clock::now();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t transferable = in.size();
        _samples_processed += transferable;
        in.commit_read(transferable);

        if (_samples_processed % 1000000 < CHANNEL_SIZE) {
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed_seconds = now - _first_sample_time;
            double sps = (_samples_processed / elapsed_seconds.count());
            std::cout << "Samples Per Second " << sps << std::endl;
        }
        return cler::Empty{};
    }

    private:
        uint64_t _samples_processed = 0;
        std::chrono::steady_clock::time_point _first_sample_time;
};

int main() {
    SourceBlock source("Source");
    AdderBlock adder("Adder");
    GainBlock gain("Gain", 2.0f);
    SinkBlock sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &adder.in0, &adder.in1),
        cler::BlockRunner(&adder, &gain.in),
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );

    flowgraph.run();


    while (true) {
        // Simulate some work in the main thread
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}