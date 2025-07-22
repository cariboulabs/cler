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

    SourceCaribouliteBlock source_cariboulite(
        "SourceCaribouLite",
        CaribouLiteRadio::RadioType::S1G,
        freq_hz,
        static_cast<float>(sps),
        false,
        40.0f,
        512  // 640 KB buffer size
    );

    // AFTER the cbl source is created, so it doenst steal our handler
    std::signal(SIGINT, handle_sigint);

    SinkFileBlock<std::complex<float>> sink_file(
        "SinkFile",
        "recorded_stream.bin",
        512  // 640 KB buffer
    );
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_cariboulite, &sink_file.in),
        cler::BlockRunner(&sink_file)
    );

    flowgraph.run(cler::FlowGraphConfig{
        .adaptive_sleep = false,
    });


    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    flowgraph.stop();

    return 0;
}