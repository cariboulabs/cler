#include "cler.hpp"
#include <iostream>
#include <emscripten.h>

const size_t CHANNEL_SIZE = 512;

struct SourceBlock : public cler::BlockBase {
    SourceBlock(const char* name) : BlockBase(name) {
        for (size_t i = 0; i < CHANNEL_SIZE; ++i) {
            _ones[i] = 1.0f;
            _twos[i] = 2.0;
        }
    } 

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::Channel<float>* out0,
        cler::Channel<double>* out1) {

        // Efficient bulk write
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
    cler::Channel<float, CHANNEL_SIZE> in; // Stack buffer for WASM efficiency
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

        // Print throughput every million samples
        if (_samples_processed % 1000000 < CHANNEL_SIZE) {
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed_seconds = now - _first_sample_time;
            double sps = (_samples_processed / elapsed_seconds.count());
            std::cout << "WASM Throughput: " << static_cast<int>(sps) << " samples/sec" << std::endl;
        }
        return cler::Empty{};
    }

private:
    uint64_t _samples_processed = 0;
    std::chrono::steady_clock::time_point _first_sample_time;
};

// Global flowgraph components
SourceBlock source("Source");
AdderBlock adder("Adder");
GainBlock gain("Gain", 2.0f);
SinkBlock sink("Sink");

// Control variables
bool processing_active = false;

// Main loop function called by Emscripten
void main_loop() {
    if (!processing_active) return;
    
    // Execute streamlined flowgraph - no threading
    source.procedure(&adder.in0, &adder.in1);
    adder.procedure(static_cast<cler::ChannelBase<float>*>(&gain.in));
    gain.procedure(&sink.in);
    sink.procedure();
}

// Functions callable from JavaScript
extern "C" {
    void start_processing() {
        processing_active = true;
        std::cout << "Processing started" << std::endl;
    }
    
    void stop_processing() {
        processing_active = false;
        std::cout << "Processing stopped" << std::endl;
    }
}

int main() {
    std::cout << "Cler WASM Streamlined Example Ready" << std::endl;
    std::cout << "Signal chain: Source -> Adder -> Gain -> Sink" << std::endl;
    std::cout << "Click Start to begin processing" << std::endl;
    
    // Set up main loop but don't start processing yet
    emscripten_set_main_loop(main_loop, 60, 1);
    
    return 0;
}