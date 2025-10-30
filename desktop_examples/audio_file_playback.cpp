#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_audio_file.hpp"
#include "desktop_blocks/sinks/sink_audio.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <audio_file>\n", argv[0]);
        fprintf(stderr, "  Supported formats: MP3, WAV, FLAC, OGG, M4A, etc.\n");
        fprintf(stderr, "  Example: %s song.mp3\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const uint32_t sample_rate = 48000;

    // Create audio file source and audio sink
    SourceAudioFileBlock<float> audio_source("AudioFileSource", input_file, sample_rate, true);
    SinkAudioBlock audio_sink("AudioSink", static_cast<double>(sample_rate), -1);  // default device

    // Create flowgraph: audio source -> audio sink
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&audio_source, &audio_sink.in),
        cler::BlockRunner(&audio_sink)
    );

    printf("Playing audio file: %s\n", input_file);
    printf("Sample rate: %u Hz (resampled)\n", sample_rate);
    printf("Press Ctrl+C to stop\n");

    flowgraph.run();

    return 0;
}
