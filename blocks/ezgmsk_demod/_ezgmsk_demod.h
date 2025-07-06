#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "liquid.h"

typedef struct ezgmsk_demod_s * ezgmsk_demod;

typedef enum {
    EZGMSK_DEMOD_STATE_DETECTFRAME=0,   
    EZGMSK_DEMOD_STATE_RXSYNCWORD,   
    EZGMSK_DEMOD_STATE_RXHEADER,           
    EZGMSK_DEMOD_STATE_RXPAYLOAD,
} ezgmsk_demod_state_en;

typedef int (*ezgmsk_demod_callback)(
                                unsigned int  _sample_counter,
                                ezgmsk_demod_state_en _state,
                                unsigned char *  _header,
                                unsigned char *  _payload,
                                unsigned int     _payload_len,
                                float            _rssi,
                                float            _snr,
                                void *           _userdata);

// create GMSK frame synchronizer
//  _k          :   samples/symbol
//  _m          :   filter delay (symbols)
//  _BT         :   excess bandwidth factor
//  _callback   :   callback function
//  _userdata   :   user data pointer passed to callback function
ezgmsk_demod ezgmsk_demod_create_set(unsigned int         _k,
                                    unsigned int                _m,
                                    float                       _BT,
                                    unsigned int                _preamble_len,
                                    const unsigned char*        _syncword,
                                    unsigned int                _syncword_len,
                                    unsigned int                _header_symbols_len,
                                    unsigned int                _payload_max_bytes_len,
                                    float                       _detector_threshold,
                                    float                       _detector_dphi_max,
                                    ezgmsk_demod_callback        _callback,
                                    void *                      _userdata);
int ezgmsk_demod_destroy(ezgmsk_demod _q);
int ezgmsk_demod_print(ezgmsk_demod _q);
int ezgmsk_demod_set_header_len(ezgmsk_demod _q, unsigned int _len);
int ezgmsk_demod_reset(ezgmsk_demod _q);
int ezgmsk_demod_is_frame_open(ezgmsk_demod _q);
int ezgmsk_demod_execute(ezgmsk_demod _q,
                          liquid_float_complex * _x,
                          unsigned int _n);

#ifdef __cplusplus
}
#endif