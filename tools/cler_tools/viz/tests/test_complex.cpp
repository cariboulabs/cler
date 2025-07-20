// Test file for complex flowgraph with multiple paths
#include "cler.hpp"

struct SourceBlock : public cler::BlockBase {
    SourceBlock(const char* name) : BlockBase(name) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* out1,
        cler::ChannelBase<float>* out2) {
        return cler::Empty{};
    }
};

struct SplitterBlock : public cler::BlockBase {
    cler::Channel<float> in;
    
    SplitterBlock(const char* name) : BlockBase(name), in(256) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* out1,
        cler::ChannelBase<float>* out2) {
        return cler::Empty{};
    }
};

struct CombinerBlock : public cler::BlockBase {
    cler::Channel<float> in1;
    cler::Channel<float> in2;
    
    CombinerBlock(const char* name) : BlockBase(name), in1(256), in2(256) {}
    
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
    SplitterBlock splitter("Splitter");
    CombinerBlock combiner("Combiner");
    SinkBlock sink1("Sink1");
    SinkBlock sink2("Sink2");
    
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &splitter.in, &combiner.in1),
        cler::BlockRunner(&splitter, &combiner.in2, &sink1.in),
        cler::BlockRunner(&combiner, &sink2.in),
        cler::BlockRunner(&sink1),
        cler::BlockRunner(&sink2)
    );
    
    flowgraph.run();
    return 0;
}