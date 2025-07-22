#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_cariboulite.hpp"
#include "desktop_blocks/sinks/sink_file.hpp"

#include <csignal>
std::atomic<bool> running = true;

void handle_sigint(int) {
    running = false;
}

int main() {
    const size_t sps = 4'000'000;
    const float freq_hz = 903e6;
    char recording_filename[] = "recorded_stream.bin";

    SourceCaribouliteBlock source_cariboulite(
        "SourceCaribouLite",
        CaribouLiteRadio::RadioType::S1G,
        freq_hz,
        static_cast<float>(sps),
        false,
        40.0f
    );

    // AFTER the cbl source is created, so it doenst steal our handler
    std::signal(SIGINT, handle_sigint);

    SinkFileBlock<std::complex<float>> sink_file(
        "SinkFile",
        recording_filename,
        64 * 1024
    );
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_cariboulite, &sink_file.in),
        cler::BlockRunner(&sink_file)
    );

    flowgraph.run(cler::FlowGraphConfig{
        .adaptive_sleep = false,
    });


    printf("Press Ctrl+C to stop recording...\n");
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    flowgraph.stop();
    printf("Samples saved to %s\n", recording_filename);
    return 0;
}