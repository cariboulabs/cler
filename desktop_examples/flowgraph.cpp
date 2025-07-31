#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <cstring> // for memcpy

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

        // Use zero-copy path
        auto [write_ptr0, write_size0] = out0->write_dbf();
        auto [write_ptr1, write_size1] = out1->write_dbf();
        
        // Direct write to both outputs
        size_t to_write0 = std::min(write_size0, CHANNEL_SIZE);
        size_t to_write1 = std::min(write_size1, CHANNEL_SIZE);
        
        if (to_write0 > 0) {
            std::memcpy(write_ptr0, _ones, to_write0 * sizeof(float));
            out0->commit_write(to_write0);
        }
        
        if (to_write1 > 0) {
            std::memcpy(write_ptr1, _twos, to_write1 * sizeof(double));
            out1->commit_write(to_write1);
        }
        
        return cler::Empty{};
    }

    private:
        float _ones[CHANNEL_SIZE];
        double _twos[CHANNEL_SIZE];
};
struct AdderBlock : public cler::BlockBase {
    cler::Channel<float> in0;
    cler::Channel<double> in1;

    AdderBlock(const char* name) : BlockBase(name), in0(1024), in1(512) {} // 1024*4=4KB, 512*8=4KB

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Use zero-copy path
        auto [write_ptr, write_size] = out->write_dbf();
        auto [read_ptr0, read_size0] = in0.read_dbf();
        auto [read_ptr1, read_size1] = in1.read_dbf();
        
        size_t to_process = std::min({write_size, read_size0, read_size1});
        if (to_process > 0) {
            for (size_t i = 0; i < to_process; ++i) {
                write_ptr[i] = read_ptr0[i] + static_cast<float>(read_ptr1[i]);
            }
            in0.commit_read(to_process);
            in1.commit_read(to_process);
            out->commit_write(to_process);
        }
        return cler::Empty{};
    }

};

struct GainBlock : public cler::BlockBase {
    cler::Channel<float> in; // Heap allocated for dbf support
    float gain;

    GainBlock(const char* name, float gain_value) : BlockBase(name), in(1024), gain(gain_value) {} // 1024 * 4 = 4KB

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        size_t to_process = std::min(read_size, write_size);
        if (to_process > 0) {
            for (size_t i = 0; i < to_process; ++i) {
                write_ptr[i] = read_ptr[i] * gain;
            }
            in.commit_read(to_process);
            out->commit_write(to_process);
        }
        return cler::Empty{};
    }
};

struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkBlock(const char* name) : BlockBase(name), in(1024) { // 1024 * 4 = 4KB
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