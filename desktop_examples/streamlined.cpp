#include "cler.hpp"
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

        // Try zero-copy path first
        auto [write_ptr0, write_size0] = out0->write_dbf();
        auto [write_ptr1, write_size1] = out1->write_dbf();
        
        if (write_ptr0 && write_size0 > 0 && write_ptr1 && write_size1 > 0) {
            // Direct write to both outputs
            size_t to_write0 = std::min(write_size0, CHANNEL_SIZE);
            size_t to_write1 = std::min(write_size1, CHANNEL_SIZE);
            
            std::memcpy(write_ptr0, _ones, to_write0 * sizeof(float));
            std::memcpy(write_ptr1, _twos, to_write1 * sizeof(double));
            
            out0->commit_write(to_write0);
            out1->commit_write(to_write1);
            return cler::Empty{};
        }
        
        // Fallback to standard approach
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
        // Try zero-copy path with write_dbf
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr && write_size > 0) {
            // Check if both inputs have data
            size_t in_size = std::min({in0.size(), in1.size()});
            if (in_size == 0) {
                return cler::Error::NotEnoughSamples;
            }
            
            size_t to_process = std::min(write_size, in_size);
            
            // Try read_dbf for inputs
            auto [read_ptr0, read_size0] = in0.read_dbf();
            auto [read_ptr1, read_size1] = in1.read_dbf();
            
            if (read_ptr0 && read_size0 >= to_process && read_ptr1 && read_size1 >= to_process) {
                // ULTIMATE FAST PATH: All doubly-mapped
                for (size_t i = 0; i < to_process; ++i) {
                    write_ptr[i] = read_ptr0[i] + static_cast<float>(read_ptr1[i]);
                }
                in0.commit_read(to_process);
                in1.commit_read(to_process);
                out->commit_write(to_process);
                return cler::Empty{};
            }
        }
        
        // Fall back to standard approach
        size_t in_size = std::min({in0.size(), in1.size()});
        if (in_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        size_t out_size = out->space();
        if (out_size == 0) {
            return cler::Error::NotEnoughSpace;
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
        // Try zero-copy path first
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr && read_size > 0) {
            auto [write_ptr, write_size] = out->write_dbf();
            if (write_ptr && write_size > 0) {
                // ULTIMATE FAST PATH: Process directly between buffers
                size_t to_process = std::min(read_size, write_size);
                for (size_t i = 0; i < to_process; ++i) {
                    write_ptr[i] = read_ptr[i] * gain;
                }
                in.commit_read(to_process);
                out->commit_write(to_process);
                return cler::Empty{};
            }
        }
        
        // Fall back to standard approach
        size_t in_size = in.size();
        if (in_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        size_t out_size = out->space();
        if (out_size == 0) {
            return cler::Error::NotEnoughSpace;
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

    cler::Result<cler::Empty, cler::Error> res = cler::Empty{};

    while (true) {
        res = source.procedure(&adder.in0, &adder.in1);
        res = adder.procedure(&gain.in);
        res = gain.procedure(&sink.in);
        res = sink.procedure();
    }
}
