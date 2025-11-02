#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_audio_file.hpp"
#include "desktop_blocks/sinks/sink_audio.hpp"

#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>

// Signal handler for graceful shutdown
static std::atomic<bool> should_exit(false);

void signal_handler(int signal) {
    if (signal == SIGINT) {
        should_exit = true;
        printf("\nShutting down...\n");
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <audio_file>\n", argv[0]);
        fprintf(stderr, "  Supported formats: MP3, WAV, FLAC, OGG, M4A, etc.\n");
        fprintf(stderr, "  Example: %s song.mp3\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const uint32_t sample_rate = 48000;

    signal(SIGINT, signal_handler);

    SourceAudioFileBlock<float> audio_source("AudioFileSource", input_file, sample_rate, true);
    SinkAudioBlock audio_sink("AudioSink", static_cast<double>(sample_rate), -1);  // default device

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&audio_source, &audio_sink.in),
        cler::BlockRunner(&audio_sink)
    );

    printf("Playing audio file: %s\n", input_file);
    printf("Sample rate: %u Hz (resampled)\n", sample_rate);
    printf("Press Ctrl+C to stop\n\n");

    flowgraph.run();

    while (!should_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Graceful shutdown
    flowgraph.stop();

    return 0;
}
