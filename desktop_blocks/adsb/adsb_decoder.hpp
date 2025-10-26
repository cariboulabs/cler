#pragma once

#include "cler.hpp"
#include "desktop_blocks/adsb/modes.h"
#include "desktop_blocks/adsb/modes_2400.h"
#include <cstring>
#include <algorithm>

struct ADSBDecoderBlock : public cler::BlockBase {
    cler::Channel<uint16_t> in;

    constexpr static size_t BUFFER_ELEMENTS = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(uint16_t) * 1000;

    // Bitmask of DFs to pass through (e.g., 1<<17 for DF17)
    // Default: 0xFFFFFFFFU accepts all 32 message types
    // Use specific bits to filter to desired message types
    ADSBDecoderBlock(const char* name, uint32_t df_filter = 0xFFFFFFFFU)
        : BlockBase(name),
        in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(uint16_t)),
        _df_filter(df_filter),
        _tmp_buffer(new uint16_t[BUFFER_ELEMENTS]) {
        // Validate filter: 0 is ambiguous, must use 0xFFFFFFFFU for "allow all messages"
        assert(_df_filter != 0 && "df_filter=0 is invalid. Use 0xFFFFFFFFU to allow all message types.");
        mode_s_init(&_decoder_state);
    }

    ~ADSBDecoderBlock() {
        if (_tmp_buffer) {
            delete[] _tmp_buffer;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<mode_s_msg>* out) {
        static size_t call_count = 0;
        static size_t total_samples = 0;

        auto [read_ptr, read_size] = in.read_dbf();

        // Mode S detection requires at least MODES_LONG_MSG_SAMPLES samples
        // (preamble + longest message at 2 samples per bit)
        constexpr size_t MODES_LONG_MSG_SAMPLES = 240;  // 16 preamble + 112*2 bits
        if (read_size < MODES_LONG_MSG_SAMPLES) {
            return cler::Error::NotEnoughSamples;
        }

        size_t write_space = out->space();
        if (write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Limit to buffer size to prevent overflow
        size_t to_process = std::min(read_size, BUFFER_ELEMENTS);

        memcpy(_tmp_buffer, read_ptr, to_process * sizeof(uint16_t));
        in.commit_read(to_process);

        total_samples += to_process;
        call_count++;

        if (call_count % 1000 == 0) {
            printf("[Decoder] Called %zu times, processed %zu samples (%.2f MB), avg mag: %u, max mag: %u\n",
                   call_count, total_samples, total_samples * 2.0 / 1e6,
                   _tmp_buffer[0], *std::max_element(_tmp_buffer, _tmp_buffer + to_process));
            fflush(stdout);
        }

        CallbackContext ctx;
        ctx.out_channel = out;
        ctx.df_filter = _df_filter;
        mode_s_detect_2400(&_decoder_state, _tmp_buffer, to_process, on_message_detected, &ctx);

        return cler::Empty{};
    }

    // Set or change DF filter (thread-safe with atomic)
    void set_df_filter(uint32_t df_filter) {
        _df_filter = df_filter;
    }

private:
    mode_s_t _decoder_state;
    uint32_t _df_filter;
    uint16_t* _tmp_buffer = nullptr;

    // Context for callback
    struct CallbackContext {
        cler::ChannelBase<mode_s_msg>* out_channel;
        uint32_t df_filter;
    };

    static void on_message_detected(mode_s_t* self, struct mode_s_msg* mm, void* context) {
        static size_t total_messages = 0;
        static size_t good_crc_messages = 0;

        total_messages++;
        if (mm->crcok) {
            good_crc_messages++;
        }

        if (total_messages % 100 == 0) {
            printf("[Decoder] Total: %zu, Good CRC: %zu (%.1f%%)\n",
                   total_messages, good_crc_messages,
                   100.0 * good_crc_messages / total_messages);
            fflush(stdout);
        }

        (void)self;
        CallbackContext* ctx = static_cast<CallbackContext*>(context);

        if (!mm->crcok) {
            return;
        }

        if (ctx->df_filter != 0) {
            if ((ctx->df_filter & (1U << mm->msgtype)) == 0) {
                return;
            }
        }

        if (ctx->out_channel->space() > 0) {
            ctx->out_channel->push(*mm);
        }
    }
};
