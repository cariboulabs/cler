// This file should FAIL validation - runner not in flowgraph
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
    TestBlock<float> block3("Block3");
    
    // Create runner but don't add it to flowgraph
    auto forgotten_runner = cler::BlockRunner(&block2, &block3.in);
    
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&block1, &block2.in),
        // ERROR: forgotten_runner not added here
        cler::BlockRunner(&block3)
    );
    
    flowgraph.run();
    
    return 0;
}