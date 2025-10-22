#pragma once

#include "cler.hpp"
#include "libmodes/mode-s.h"
#include <cstring>

struct ADSBDecoderBlock : public cler::BlockBase {
    cler::Channel<uint16_t> magnitude_in;

    uint16_t* tmp_buffer = new uint16_t[cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(uint16_t)];
    
    // Bitmask of DFs to pass through (e.g., 1<<17 for DF17)
    //                   If 0, all messages pass through
    ADSBDecoderBlock(const char* name, uint32_t df_filter = 0)
        : BlockBase(name),
        magnitude_in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(uint16_t)),
        _df_filter(df_filter) {
        mode_s_init(&_decoder_state);
    }

    ~ADSBDecoderBlock() {
        if (tmp_buffer) {
            delete[] tmp_buffer;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<mode_s_msg>* out) {
        auto [read_ptr, read_size] = magnitude_in.read_dbf();
        if (read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t write_space = out->space();
        if (write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        memcpy(_tmp_buffer, read_ptr, read_size * sizeof(uint16_t));
        magnitude_in.commit_read(read_size);

        CallbackContext ctx;
        ctx.out_channel = out;
        ctx.df_filter = _df_filter;
        mode_s_detect(&_decoder_state, _tmp_buffer, read_size, on_message_detected, &ctx);

        return cler::Empty{};
    }

    // Set or change DF filter (thread-safe with atomic)
    void set_df_filter(uint32_t df_filter) {
        _df_filter = df_filter;
    }

private:
    mode_s_t _decoder_state;
    uint32_t _df_filter;
    uint16_t* _tmp_buffer;

    // Context for callback
    struct CallbackContext {
        cler::ChannelBase<mode_s_msg>* out_channel;
        uint32_t df_filter;
    };

    static void on_message_detected(mode_s_t* self, struct mode_s_msg* mm, void* context) {
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
