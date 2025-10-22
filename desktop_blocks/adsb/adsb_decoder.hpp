#pragma once

#include "cler.hpp"
#include "libmodes/mode-s.h"
#include <cstring>

/**
 * ADSBDecoderBlock
 *
 * Wraps libmodes to decode Mode S messages from magnitude samples.
 *
 * Input:  Channel<float>            (magnitude samples from SDR)
 * Output: Channel<mode_s_msg>       (decoded Mode S messages)
 *
 * Filters messages by Downlink Format (DF) using a bitmask.
 * Only outputs messages where bit[msgtype] is set.
 */
struct ADSBDecoderBlock : public cler::BlockBase {
    cler::Channel<float> magnitude_in;

    /**
     * Create decoder block
     *
     * @param name Block name
     * @param df_filter Bitmask of DFs to pass through (e.g., 1<<17 for DF17)
     *                  If 0, all messages pass through
     */
    ADSBDecoderBlock(const char* name, uint32_t df_filter = 0)
        : BlockBase(name), magnitude_in(BUFFER_SIZE), _df_filter(df_filter) {
        mode_s_init(&_decoder_state);
    }

    // Procedure: read magnitudes, detect messages, output filtered mode_s_msg
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<mode_s_msg>* out) {
        // Use zero-copy DBF path when available
        auto [read_ptr, read_size] = magnitude_in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();

        size_t to_process = std::min({read_size, write_size, BUFFER_SIZE});

        if (to_process > 0 && read_ptr && write_ptr) {
            // Zero-copy path: convert float magnitudes to uint16_t directly
            uint16_t* mag_buffer = static_cast<uint16_t*>(write_ptr);
            float* float_buffer = static_cast<float*>(read_ptr);

            // Convert float magnitudes to uint16_t for libmodes
            // (65535 is 2^16 - 1, max value for unsigned 16-bit integer)
            for (size_t i = 0; i < to_process; ++i) {
                float val = float_buffer[i];
                val = std::max(0.0f, std::min(65535.0f, val));
                mag_buffer[i] = static_cast<uint16_t>(val);
            }

            // Set up context for libmodes callback
            CallbackContext ctx;
            ctx.out_channel = out;
            ctx.df_filter = _df_filter;

            // Detect Mode S messages in magnitude buffer
            // Callback will filter and push to output channel
            mode_s_detect(&_decoder_state, mag_buffer, to_process, on_message_detected, &ctx);

            magnitude_in.commit_read(to_process);
            out->commit_write(to_process);
        } else {
            // Fallback to readN/writeN if DBF not available
            size_t available = magnitude_in.size();
            if (available == 0) {
                return cler::Error::NotEnoughSamples;
            }

            if (out->space() == 0) {
                return cler::Error::NotEnoughSpace;
            }

            size_t to_read = std::min(available, BUFFER_SIZE);
            uint16_t* mag_buffer = new uint16_t[to_read];
            float* float_buffer = new float[to_read];
            magnitude_in.readN(float_buffer, to_read);

            // Convert float magnitudes to uint16_t for libmodes
            for (size_t i = 0; i < to_read; ++i) {
                float val = float_buffer[i];
                val = std::max(0.0f, std::min(65535.0f, val));
                mag_buffer[i] = static_cast<uint16_t>(val);
            }

            // Set up context for libmodes callback
            CallbackContext ctx;
            ctx.out_channel = out;
            ctx.df_filter = _df_filter;

            // Detect Mode S messages in magnitude buffer
            mode_s_detect(&_decoder_state, mag_buffer, to_read, on_message_detected, &ctx);

            delete[] float_buffer;
            delete[] mag_buffer;
        }

        return cler::Empty{};
    }

    // Set or change DF filter (thread-safe with atomic)
    void set_df_filter(uint32_t df_filter) {
        _df_filter = df_filter;
    }

private:
    mode_s_t _decoder_state;
    uint32_t _df_filter;
    static constexpr size_t BUFFER_SIZE = 65536;

    // Context for callback
    struct CallbackContext {
        cler::ChannelBase<mode_s_msg>* out_channel;
        uint32_t df_filter;
    };

    // Static callback invoked by libmodes for each detected message
    // Must be C-compatible since libmodes expects function pointer
    static void on_message_detected(mode_s_t* self, struct mode_s_msg* mm, void* context) {
        (void)self;  // unused parameter
        CallbackContext* ctx = static_cast<CallbackContext*>(context);

        // Only process messages with valid CRC
        if (!mm->crcok) {
            return;
        }

        // Apply DF filter if set (non-zero filter means selective filtering)
        if (ctx->df_filter != 0) {
            // Check if this DF is enabled in the bitmask
            if ((ctx->df_filter & (1U << mm->msgtype)) == 0) {
                return;  // This DF is not enabled, skip it
            }
        }

        // Check output space before pushing
        if (ctx->out_channel->space() > 0) {
            ctx->out_channel->push(*mm);
        }
    }
};
