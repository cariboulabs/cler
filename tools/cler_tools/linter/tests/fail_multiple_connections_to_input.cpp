// Test for multiple connections to same input channel
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    SourceCWBlock<float> source1("Source1", 1.0f, 440.0f, 1000);
    SourceCWBlock<float> source2("Source2", 1.0f, 880.0f, 1000);
    SinkNullBlock<float> sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source1, &sink.in),
        cler::BlockRunner(&source2, &sink.in),  // ERROR: sink.in already connected
        cler::BlockRunner(&sink)
    );

    flowgraph.run();
    flowgraph.stop();
    return 0;
}