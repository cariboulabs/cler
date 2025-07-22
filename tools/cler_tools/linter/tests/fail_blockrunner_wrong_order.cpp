// Test for incorrect BlockRunner construction order

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    SourceCWBlock<float> source("Source", 1.0f, 440.0f, 1000);
    AddBlock<float> adder("Adder", 2);
    SinkNullBlock<float> sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &adder.in[0]),
        // Wrong: channels should come after the block pointer
        cler::BlockRunner(&sink.in, &adder),          // ERROR: channels before block
        cler::BlockRunner(&sink)
    );

    flowgraph.run();
    flowgraph.stop();
    return 0;
}