// Test for proper variadic output handling (should pass validation)

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

int main() {
    SourceCWBlock<std::complex<float>> source("Source", 1.0f, 440.0f, 1000);
    
    // Polyphase channelizer with multiple outputs
    PolyphaseChannelizerBlock channelizer("Channelizer", 4, 60.0f, 13);
    
    // Multiple sinks for each channel
    SinkNullBlock<std::complex<float>> sink0("Sink0");
    SinkNullBlock<std::complex<float>> sink1("Sink1");
    SinkNullBlock<std::complex<float>> sink2("Sink2");
    SinkNullBlock<std::complex<float>> sink3("Sink3");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &channelizer.in),
        // Proper variadic output connection
        cler::BlockRunner(&channelizer, 
            &sink0.in, &sink1.in, &sink2.in, &sink3.in),
        cler::BlockRunner(&sink0),
        cler::BlockRunner(&sink1),
        cler::BlockRunner(&sink2),
        cler::BlockRunner(&sink3)
    );

    flowgraph.run();
    flowgraph.stop();
    return 0;
}