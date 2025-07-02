#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "liquid.h"

typedef struct clgmskframesync_s * clgmskframesync;

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
                                       framesync_callback _callback,
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