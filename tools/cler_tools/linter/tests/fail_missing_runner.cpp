// This file should FAIL validation - missing BlockRunner
#include "cler.hpp"

template<typename T>
struct TestBlock : public cler::BlockBase {
    cler::Channel<T> in;
    
    TestBlock(const char* name) : cler::BlockBase(name), in(256) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }
};

int main() {
    TestBlock<float> block1("Block1");
    TestBlock<float> block2("Block2");  // ERROR: This block has no BlockRunner and is not in flowgraph
    TestBlock<float> block3("Block3");
    
    // block2 is declared but completely forgotten - not in flowgraph at all!
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&block1, &block3.in),
        cler::BlockRunner(&block3)
    );
    
    flowgraph.run();
    
    return 0;
}