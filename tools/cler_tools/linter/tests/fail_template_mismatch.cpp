// Test for template parameter mismatches

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    // Source generates std::complex<float>, but gain is templated for double
    SourceCWBlock<std::complex<float>> source("Source", 1.0f, 440.0f, 1000);
    GainBlock<double> gain("Gain", 2.0);               // Template mismatch
    SinkNullBlock<std::complex<float>> sink("Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &gain.in),         // ERROR: std::complex<float> → double
        cler::BlockRunner(&gain, &sink.in)            // ERROR: double → std::complex<float> 
    );

    flowgraph.run();
    flowgraph.stop();
    return 0;
}