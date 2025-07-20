// Test file for basic flowgraph visualization
#include "cler.hpp"

struct SourceBlock : public cler::BlockBase {
    SourceBlock(const char* name) : BlockBase(name) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* out) {
        return cler::Empty{};
    }
};

struct ProcessBlock : public cler::BlockBase {
    cler::Channel<float> in;
    
    ProcessBlock(const char* name) : BlockBase(name), in(256) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* out) {
        return cler::Empty{};
    }
};

struct SinkBlock : public cler::BlockBase {
    cler::Channel<float> in;
    
    SinkBlock(const char* name) : BlockBase(name), in(256) {}
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Empty{};
    }
};

int main() {
    SourceBlock source("Source");
    ProcessBlock process("Process");
    SinkBlock sink("Sink");
    
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &process.in),
        cler::BlockRunner(&process, &sink.in),
        cler::BlockRunner(&sink)
    );
    
    flowgraph.run();
    return 0;
}