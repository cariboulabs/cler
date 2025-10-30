#pragma once

#include "cler.hpp"
#include <stdexcept>
#include <string>
#include <cstring>
#include <cstdio>

#ifdef __has_include
    #if __has_include(<portaudio.h>)
        #include <portaudio.h>
    #else
        #error "PortAudio header not found. Please install portaudio19-dev package."
    #endif
#else
    #include <portaudio.h>
#endif

// Helper to convert Pa_GetErrorText to C++ exception
inline void pa_check(PaError err) {
    if (err != paNoError) {
        std::string msg = "PortAudio error: ";
        msg += Pa_GetErrorText(err);
        throw std::runtime_error(msg);
    }
}

// Audio sink for PortAudio float32 output
struct SinkAudioBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkAudioBlock(const char* name,
                   double sample_rate = 48000.0,
                   int device_index = paNoDevice,
                   size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size),
          _sample_rate(sample_rate),
          _device_index(device_index),
          _stream(nullptr)
    {
        if (sample_rate <= 0.0 || sample_rate > 1e6) {
            throw std::invalid_argument("Invalid sample rate: must be > 0 and <= 1MHz");
        }

        // Validate buffer size for DBF
        if (buffer_size > 0 && buffer_size * sizeof(float) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Buffer size too small for doubly-mapped buffers. Need at least " +
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) + " elements");
        }

        // Initialize PortAudio (safe to call multiple times)
        PaError err = Pa_Initialize();
        pa_check(err);

        // Validate device index if specified
        if (device_index != paNoDevice) {
            int num_devices = Pa_GetDeviceCount();
            if (num_devices < 0 || device_index >= num_devices) {
                throw std::invalid_argument("Invalid device index: " + std::to_string(device_index));
            }
        }

        // Open the audio stream
        _open_stream();
    }

    ~SinkAudioBlock() {
        _close_stream();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (!_stream) {
            return cler::Error::TERM_IOError;
        }

        // Use zero-copy doubly-mapped buffer path
        auto [read_ptr, read_size] = in.read_dbf();

        if (read_size > 0) {
            // Write directly to PortAudio stream
            PaError err = Pa_WriteStream(_stream, read_ptr, read_size);

            // Handle errors
            if (err == paOutputUnderflowed) {
                // Underflow is expected during startup/low data scenarios
                in.commit_read(read_size);
                return cler::Empty{};
            } else if (err != paNoError) {
                return cler::Error::TERM_IOError;
            }

            in.commit_read(read_size);
        }

        return cler::Empty{};
    }

    // Static method to list available audio devices
    static void print_devices() {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            throw std::runtime_error("PortAudio init failed");
        }

        int num_devices = Pa_GetDeviceCount();
        if (num_devices < 0) {
            throw std::runtime_error("Pa_GetDeviceCount() failed");
        }

        printf("PortAudio Output Devices:\n");
        for (int i = 0; i < num_devices; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (!info) continue;

            if (info->maxOutputChannels > 0) {
                printf("  [%d] %s (outputs: %d, default latency: %.1f ms)\n",
                       i, info->name, info->maxOutputChannels,
                       info->defaultHighOutputLatency * 1000.0);
            }
        }
    }

private:
    double _sample_rate;
    int _device_index;
    PaStream* _stream;

    void _open_stream() {
        PaStreamParameters output_params;
        std::memset(&output_params, 0, sizeof(output_params));

        output_params.device = (_device_index == paNoDevice) ? Pa_GetDefaultOutputDevice() : _device_index;
        if (output_params.device < 0) {
            throw std::runtime_error("No default output device found");
        }

        output_params.channelCount = 1;  // Mono
        output_params.sampleFormat = paFloat32;
        output_params.suggestedLatency = Pa_GetDeviceInfo(output_params.device)->defaultHighOutputLatency;
        output_params.hostApiSpecificStreamInfo = nullptr;

        PaError err = Pa_OpenStream(
            &_stream,
            nullptr,                              // no input
            &output_params,
            _sample_rate,
            paFramesPerBufferUnspecified,         // Let PortAudio choose
            paClipOff,                            // Don't auto-clip
            nullptr,                              // no callback
            nullptr                               // no user data
        );
        pa_check(err);

        err = Pa_StartStream(_stream);
        if (err != paNoError) {
            Pa_CloseStream(_stream);
            _stream = nullptr;
            pa_check(err);
        }
    }

    void _close_stream() {
        if (_stream) {
            PaError err = Pa_StopStream(_stream);
            if (err != paNoError) {
                // Log but don't throw in destructor
                fprintf(stderr, "Warning: Pa_StopStream failed: %s\n", Pa_GetErrorText(err));
            }
            err = Pa_CloseStream(_stream);
            if (err != paNoError) {
                fprintf(stderr, "Warning: Pa_CloseStream failed: %s\n", Pa_GetErrorText(err));
            }
            _stream = nullptr;
        }
    }
};
