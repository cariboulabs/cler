// Test for channel type mismatches between connected blocks

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    // Source outputs float, but adder expects std::complex<float>
    SourceCWBlock<float> source("Source", 1.0f, 440.0f, 1000);
    AddBlock<std::complex<float>> adder("Adder", 2);
    SinkNullBlock<std::complex<float>> sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &adder.in[0]),     // Type mismatch: float → std::complex<float>
        cler::BlockRunner(&adder, &sink.in)
    );

    flowgraph.run();
    flowgraph.stop();
    return 0;
}