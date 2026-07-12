#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <string>
#include <cstring>

#ifdef __has_include
    #if __has_include(<libavformat/avformat.h>)
        extern "C" {
            #include <libavformat/avformat.h>
            #include <libavcodec/avcodec.h>
            #include <libswresample/swresample.h>
            #include <libavutil/error.h>
        }
    #else
        #error "FFmpeg headers not found. Please install libavformat-dev, libavcodec-dev, libswresample-dev packages."
    #endif
#else
    extern "C" {
        #include <libavformat/avformat.h>
        #include <libavcodec/avcodec.h>
        #include <libswresample/swresample.h>
        #include <libavutil/error.h>
    }
#endif

inline void ffmpeg_check(int err, const char* context) {
    if (err < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(err, errbuf, sizeof(errbuf));
        char msg[AV_ERROR_MAX_STRING_SIZE + 128];
        std::snprintf(msg, sizeof(msg), "%s: %s", context, errbuf);
        cler::panic(msg);
    }
}

template <typename T = float>
struct SourceAudioFileBlock : public cler::BlockBase {
    typedef void (*on_eof)(const char* filename);

    SourceAudioFileBlock(const char* name,
                        const char* filename,
                        uint32_t output_sample_rate = 48000,
                        bool repeat = true,
                        on_eof callback = nullptr)
        : cler::BlockBase(name),
          _filename(filename),
          _output_sample_rate(output_sample_rate),
          _repeat(repeat),
          _callback(callback),
          _format_ctx(nullptr),
          _codec_ctx(nullptr),
          _resampler(nullptr),
          _frame(nullptr),
          _audio_stream_idx(-1),
          _eof_reached(false)
    {
        _open_audio_file();
    }

    ~SourceAudioFileBlock() {
        _close_audio_file();
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (_format_ctx == nullptr || _codec_ctx == nullptr) {
            return cler::Error::TERM_IOError;
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t samples_written = 0;
        int ret = 0;

        while (samples_written < write_size) {
            ret = av_read_frame(_format_ctx, &_packet);

            if (ret == AVERROR_EOF) {
                if (_repeat) {
                    av_seek_frame(_format_ctx, _audio_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(_codec_ctx);
                    continue;
                } else {
                    _eof_reached = true;
                    if (_callback) {
                        _callback(_filename.c_str());
                    }
                    break;
                }
            } else if (ret < 0) {
                return cler::Error::TERM_IOError;
            }

            if (_packet.stream_index != _audio_stream_idx) {
                av_packet_unref(&_packet);
                continue;
            }

            ret = avcodec_send_packet(_codec_ctx, &_packet);
            av_packet_unref(&_packet);

            if (ret < 0) {
                return cler::Error::TERM_IOError;
            }

            while (avcodec_receive_frame(_codec_ctx, _frame) == 0) {
                uint8_t* out_buf = reinterpret_cast<uint8_t*>(write_ptr) + samples_written * sizeof(T);
                uint8_t* out_bufs[] = {out_buf};

                int frame_count = swr_convert(
                    _resampler,
                    out_bufs,
                    write_size - samples_written,
                    (const uint8_t**)_frame->data,
                    _frame->nb_samples
                );

                if (frame_count < 0) {
                    return cler::Error::TERM_IOError;
                }

                samples_written += frame_count;

                if (samples_written >= write_size) {
                    break;
                }
            }
        }

        if (samples_written > 0) {
            out->commit_write(samples_written);
        }

        if (_eof_reached && samples_written == 0) {
            return cler::Empty{};
        }

        return cler::Empty{};
    }

private:
    std::string _filename;
    uint32_t _output_sample_rate;
    bool _repeat;
    on_eof _callback;
    AVFormatContext* _format_ctx;
    AVCodecContext* _codec_ctx;
    SwrContext* _resampler;
    AVFrame* _frame;
    AVPacket _packet = {};  // Zero-init (no av_init_packet needed)
    int _audio_stream_idx;
    bool _eof_reached;

    void _open_audio_file() {
        int ret = avformat_open_input(&_format_ctx, _filename.c_str(), nullptr, nullptr);
        ffmpeg_check(ret, "Failed to open audio file");

        ret = avformat_find_stream_info(_format_ctx, nullptr);
        ffmpeg_check(ret, "Failed to find stream info");

        _audio_stream_idx = av_find_best_stream(_format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (_audio_stream_idx < 0) {
            cler::panic("No audio stream found in file");
        }

        AVStream* stream = _format_ctx->streams[_audio_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            cler::panic("Unsupported audio codec");
        }

        _codec_ctx = avcodec_alloc_context3(codec);
        if (!_codec_ctx) {
            cler::panic("Failed to allocate codec context");
        }

        avcodec_parameters_to_context(_codec_ctx, stream->codecpar);
        ret = avcodec_open2(_codec_ctx, codec, nullptr);
        ffmpeg_check(ret, "Failed to open codec");

        // FFmpeg 5.0+ (libavcodec 59+) uses AVChannelLayout; 4.x uses channel_layout/channels
        #if LIBAVCODEC_VERSION_MAJOR >= 59
            int64_t input_ch_layout = _codec_ctx->ch_layout.nb_channels > 1
                ? AV_CH_LAYOUT_STEREO
                : AV_CH_LAYOUT_MONO;
        #else
            int64_t input_ch_layout = _codec_ctx->channel_layout;
            if (input_ch_layout == 0) {
                input_ch_layout = _codec_ctx->channels > 1
                    ? AV_CH_LAYOUT_STEREO
                    : AV_CH_LAYOUT_MONO;
            }
        #endif

        _resampler = swr_alloc_set_opts(
            nullptr,
            AV_CH_LAYOUT_MONO,
            AV_SAMPLE_FMT_FLT,
            _output_sample_rate,
            input_ch_layout,
            _codec_ctx->sample_fmt,
            _codec_ctx->sample_rate,
            0,
            nullptr
        );

        if (!_resampler) {
            cler::panic("Failed to allocate resampler");
        }

        ret = swr_init(_resampler);
        ffmpeg_check(ret, "Failed to initialize resampler");

        _frame = av_frame_alloc();
        if (!_frame) {
            cler::panic("Failed to allocate frame");
        }
    }

    void _close_audio_file() {
        if (_frame) {
            av_frame_free(&_frame);
            _frame = nullptr;
        }

        if (_resampler) {
            swr_free(&_resampler);
            _resampler = nullptr;
        }

        if (_codec_ctx) {
            avcodec_free_context(&_codec_ctx);
            _codec_ctx = nullptr;
        }

        if (_format_ctx) {
            avformat_close_input(&_format_ctx);
            _format_ctx = nullptr;
        }
    }
};
