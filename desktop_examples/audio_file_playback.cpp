#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_audio_file.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/sinks/sink_audio.hpp"

#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <variant>
#include <string>
#include <cstring>

// Signal handler for graceful shutdown
static std::atomic<bool> should_exit(false);

void signal_handler(int signal) {
    if (signal == SIGINT) {
        should_exit = true;
        printf("\nShutting down...\n");
    }
}

// Helper to check if file ends with extension
bool has_extension(const char* filename, const char* ext) {
    const char* dot = strrchr(filename, '.');
    if (!dot) return false;
    return strcasecmp(dot, ext) == 0;
}

// Wrapper block that switches between raw file and FFmpeg-decoded audio
struct AudioSourceBlock : public cler::BlockBase {
    std::variant<SourceFileBlock<float>, SourceAudioFileBlock<float>> source;

    AudioSourceBlock(const char* name, const char* filename, uint32_t sample_rate, bool is_raw)
        : BlockBase(name),
          source(is_raw ?
                 std::variant<SourceFileBlock<float>, SourceAudioFileBlock<float>>(
                     std::in_place_type<SourceFileBlock<float>>, "RawFileSource", filename, true) :
                 std::variant<SourceFileBlock<float>, SourceAudioFileBlock<float>>(
                     std::in_place_type<SourceAudioFileBlock<float>>, "AudioFileSource", filename, sample_rate, true))
    {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        return std::visit([&](auto& src) {
            return src.procedure(out);
        }, source);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <audio_file>\n", argv[0]);
        fprintf(stderr, "  Supported formats:\n");
        fprintf(stderr, "    - Encoded: MP3, WAV, FLAC, OGG, M4A, etc.\n");
        fprintf(stderr, "    - Raw: .raw (32-bit float samples at 48 kHz)\n");
        fprintf(stderr, "  Examples:\n");
        fprintf(stderr, "    %s song.mp3\n", argv[0]);
        fprintf(stderr, "    %s audio.raw\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const uint32_t sample_rate = 48000;

    signal(SIGINT, signal_handler);

    // Detect if this is a raw file
    bool is_raw = has_extension(input_file, ".raw");

    if (is_raw) {
        printf("Playing raw audio file: %s\n", input_file);
        printf("Format: 32-bit float, mono\n");
        printf("Sample rate: %u Hz\n", sample_rate);
    } else {
        printf("Playing audio file: %s\n", input_file);
        printf("Sample rate: %u Hz (resampled)\n", sample_rate);
    }
    printf("Press Ctrl+C to stop\n\n");

    AudioSourceBlock audio_source("AudioSource", input_file, sample_rate, is_raw);
    SinkAudioBlock audio_sink("AudioSink", static_cast<double>(sample_rate), -1);  // default device

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&audio_source, &audio_sink.in),
        cler::BlockRunner(&audio_sink)
    );

    flowgraph.run();

    while (!should_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Graceful shutdown
    flowgraph.stop();

    return 0;
}
