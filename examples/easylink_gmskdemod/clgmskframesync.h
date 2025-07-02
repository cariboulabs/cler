#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "liquid.h"

typedef struct clgmskframesync_s * clgmskframesync;

enum {
    CLGMSKFRAMESYNC_STATE_DETECTFRAME=0,
    CLGMSKFRAMESYNC_STATE_RXPREAMBLE,     
    CLGMSKFRAMESYNC_STATE_RXSYNCWORD,              
    CLGMSKFRAMESYNC_STATE_RXHEADER,           
    CLGMSKFRAMESYNC_STATE_RXPAYLOAD,
} typedef clgmskframesync_state_en;

typedef int (*clgmskframesync_callback)(
                                unsigned int  _sample_counter,
                                clgmskframesync_state_en _state,
                                unsigned char *  _header,
                                int              _header_valid,
                                unsigned char *  _payload,
                                unsigned int     _payload_len,
                                int              _payload_valid,
                                framesyncstats_s _stats,
                                void *           _userdata);

// create GMSK frame synchronizer
//  _k          :   samples/symbol
//  _m          :   filter delay (symbols)
//  _BT         :   excess bandwidth factor
//  _callback   :   callback function
//  _userdata   :   user data pointer passed to callback function
clgmskframesync clgmskframesync_create_set(unsigned int   _k,
                                       unsigned int       _m,
                                       float              _BT,
                                       unsigned int       _preamble_len,
                                       const unsigned char*  _syncword,
                                       unsigned int       _syncword_len,
                                       float _detector_threshold,
                                       float _detector_dphi_max,
                                       clgmskframesync_callback _callback,
                                       void *             _userdata);
int clgmskframesync_destroy(clgmskframesync _q);
int clgmskframesync_print(clgmskframesync _q);
int clgmskframesync_set_header_len(clgmskframesync _q, unsigned int _len);
int clgmskframesync_reset(clgmskframesync _q);
int clgmskframesync_is_frame_open(clgmskframesync _q);
int clgmskframesync_execute(clgmskframesync _q,
                          liquid_float_complex * _x,
                          unsigned int _n);
// frame data statistics
int              clgmskframesync_reset_framedatastats(clgmskframesync _q);
framedatastats_s clgmskframesync_get_framedatastats  (clgmskframesync _q);

#ifdef __cplusplus
}
#endif