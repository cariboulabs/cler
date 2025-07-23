// Test for blocks added to flowgraph but not properly connected
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    SourceCWBlock<float> source("Source", 1.0f, 440.0f, 1000);
    AddBlock<float> adder("Adder", 2);
    SinkNullBlock<float> sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &adder.in[0]),
        cler::BlockRunner(&adder),        // ERROR: adder has no outputs connected
        cler::BlockRunner(&sink)          // ERROR: sink has no inputs connected
    );

    flowgraph.run();
    flowgraph.stop();
    return 0;
}