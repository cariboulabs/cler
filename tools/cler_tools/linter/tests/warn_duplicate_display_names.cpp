// Test for duplicate display names (should WARN, not fail)
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    SourceCWBlock<float> source1("Source", 1.0f, 440.0f, 1000);
    SourceCWBlock<float> source2("Source", 1.0f, 880.0f, 1000);  // Warning: duplicate display name
    SinkNullBlock<float> sink1("Sink");
    SinkNullBlock<float> sink2("Sink");  // Warning: another duplicate

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source1, &sink1.in),
        cler::BlockRunner(&source2, &sink2.in)
    );

    flowgraph.run();
    flowgraph.stop();
    return 0;
}