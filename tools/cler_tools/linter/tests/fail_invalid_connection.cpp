// This file should FAIL validation - invalid connection
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
    TestBlock<float> block2("Block2");
    // Note: block3 is not declared
    
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&block1, &block2.in),
        cler::BlockRunner(&block2, &block3.in),  // ERROR: block3 doesn't exist!
        cler::BlockRunner(&block2)
    );
    
    flowgraph.run();
    
    return 0;
}