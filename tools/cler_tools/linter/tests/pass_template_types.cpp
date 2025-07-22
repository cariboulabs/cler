// Test for proper template type matching (should pass validation)

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    // All blocks use consistent float template parameter
    SourceCWBlock<float> source("Source", 1.0f, 440.0f, 1000);
    GainBlock<float> gain("Gain", 2.0f);
    AddBlock<float> adder("Adder", 2);
    ThrottleBlock<float> throttle("Throttle", 1000);
    SinkNullBlock<float> sink("Sink");

    // Complex signal chain with consistent types
    SourceCWBlock<std::complex<float>> complex_source("ComplexSource", 1.0f, 440.0f, 1000);
    NoiseAWGNBlock<std::complex<float>> noise("Noise", 0.1f);
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    SinkNullBlock<std::complex<float>> complex_sink1("ComplexSink1");
    SinkNullBlock<std::complex<float>> complex_sink2("ComplexSink2");

    auto flowgraph = cler::make_desktop_flowgraph(
        // Float signal chain - all types match
        cler::BlockRunner(&source, &gain.in),
        cler::BlockRunner(&gain, &adder.in[0]),
        cler::BlockRunner(&source, &adder.in[1]),    // Source connected to both adder inputs
        cler::BlockRunner(&adder, &throttle.in),
        cler::BlockRunner(&throttle, &sink.in),
        
        // Complex signal chain - all types match
        cler::BlockRunner(&complex_source, &noise.in),
        cler::BlockRunner(&noise, &fanout.in),
        cler::BlockRunner(&fanout, &complex_sink1.in, &complex_sink2.in),
        
        // Sinks
        cler::BlockRunner(&sink),
        cler::BlockRunner(&complex_sink1),
        cler::BlockRunner(&complex_sink2)
    );

    flowgraph.run();
    flowgraph.stop();
    return 0;
}