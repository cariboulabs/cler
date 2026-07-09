#pragma once

#include "cler.hpp"
#include "cler_utils.hpp"
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

inline void pa_check(PaError err) {
    if (err != paNoError) {
        std::string msg = "PortAudio error: ";
        msg += Pa_GetErrorText(err);
        cler::panic(msg.c_str());
    }
}

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
            cler::panic("Invalid sample rate: must be > 0 and <= 1MHz");
        }

        if (buffer_size > 0 && buffer_size * sizeof(float) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }

        PaError err = Pa_Initialize();
        pa_check(err);

        if (device_index != paNoDevice) {
            int num_devices = Pa_GetDeviceCount();
            if (num_devices < 0 || device_index >= num_devices) {
                cler::panic("Invalid device index");
            }
        }

        _open_stream();
    }

    ~SinkAudioBlock() {
        _close_stream();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (!_stream) {
            return cler::Error::TERM_IOError;
        }

        auto [read_ptr, read_size] = in.read_dbf();

        if (read_size > 0) {
            PaError err = Pa_WriteStream(_stream, read_ptr, read_size);

            if (err == paOutputUnderflowed) {
                in.commit_read(read_size);
                return cler::Empty{};
            } else if (err != paNoError) {
                return cler::Error::TERM_IOError;
            }

            in.commit_read(read_size);
        }

        return cler::Empty{};
    }

    static void print_devices() {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            cler::panic("PortAudio init failed");
        }

        int num_devices = Pa_GetDeviceCount();
        if (num_devices < 0) {
            cler::panic("Pa_GetDeviceCount() failed");
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
            cler::panic("No default output device found");
        }

        output_params.channelCount = 1;
        output_params.sampleFormat = paFloat32;
        output_params.suggestedLatency = Pa_GetDeviceInfo(output_params.device)->defaultHighOutputLatency;
        output_params.hostApiSpecificStreamInfo = nullptr;

        PaError err = Pa_OpenStream(
            &_stream,
            nullptr,
            &output_params,
            _sample_rate,
            paFramesPerBufferUnspecified,
            paClipOff,
            nullptr,
            nullptr
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
