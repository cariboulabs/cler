// This file should PASS validation - correct flowgraph usage
#include "cler.hpp"

template<typename T>
struct TestSourceBlock : public cler::BlockBase {
    TestSourceBlock(const char* name) : cler::BlockBase(name) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }
};

template<typename T>
struct TestProcessBlock : public cler::BlockBase {
    cler::Channel<T> in;
    
    TestProcessBlock(const char* name) : cler::BlockBase(name), in(256) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }
};

template<typename T>
struct TestSinkBlock : public cler::BlockBase {
    cler::Channel<T> in;
    
    TestSinkBlock(const char* name) : cler::BlockBase(name), in(256) {}
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Empty{};
    }
};

int main() {
    // Correct usage - all blocks have runners and are in flowgraph
    TestSourceBlock<float> source("Source");
    TestProcessBlock<float> process("Process");
    TestSinkBlock<float> sink("Sink");
    
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &process.in),
        cler::BlockRunner(&process, &sink.in),
        cler::BlockRunner(&sink)
    );
    
    flowgraph.run();
    
    return 0;
}