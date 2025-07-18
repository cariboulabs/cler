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
        cler::Channel<float>* out0,
        cler::Channel<double>* out1) {

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
        size_t transferable = std::min({in0.size(), in1.size(), out->space()});
        for (size_t i = 0; i < transferable; ++i) {
            float value0;
            double value1;
            in0.pop(value0);
            in1.pop(value1);
            out->push(value0 + static_cast<float>(value1));
        }
        return cler::Empty{};
    }
};

struct GainBlock : public cler::BlockBase {
    cler::Channel<float, CHANNEL_SIZE> in; //this is a stack buffer!
    float gain;

    GainBlock(const char* name, float gain_value) : BlockBase(name), gain(gain_value) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t transferable = std::min(in.size(), out->space());
        for (size_t i = 0; i < transferable; ++i) {
            float value;
            in.pop(value);
            out->push(value * gain);
        }
        return cler::Empty{};
    }
};


struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(const char* name) : BlockBase(name), in(CHANNEL_SIZE) {
        _first_sample_time = std::chrono::steady_clock::now();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        for (size_t i = 0; i < in.size(); ++i) {
            float sample;
            in.pop(sample);
            _samples_processed++;
        }

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
        cler::BlockRunner(&adder, static_cast<cler::ChannelBase<float>*>(&gain.in)),
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );

    flowgraph.run();


    while (true) {
        // Simulate some work in the main thread
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}