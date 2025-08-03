#pragma once

#ifdef __cplusplus
namespace ezgmsk {
extern "C" {
#endif

#include "liquid.h"

typedef struct ezgmsk_mod_s * ezgmsk_mod;

typedef enum {
    EZGMSK_MOD_STATE_UNASSEMBLED=0,   
    EZGMSK_MOD_STATE_PREAMBLE,   
    EZGMSK_MOD_STATE_DATA,
    EZGMSK_MOD_STATE_TAIL,
} ezgmsk_mod_state_en;

// create GMSK frame modulator
//  _k                      : samples/symbol
//  _m                      : filter delay (symbols)
//  _BT                     : excess bandwidth factor
//  _preamble_symbols_len   : preamble length in symbols (will generate alternating 0,1,0,1...)
ezgmsk_mod ezgmsk_mod_create_set(unsigned int _k,
                                 unsigned int _m,
                                 float        _BT,
                                 unsigned int _preamble_symbols_len);

// destroy GMSK frame modulator
int ezgmsk_mod_destroy(ezgmsk_mod _q);

// print GMSK frame modulator internals
int ezgmsk_mod_print(ezgmsk_mod _q);

// reset GMSK frame modulator
int ezgmsk_mod_reset(ezgmsk_mod _q);

// check if frame is assembled
int ezgmsk_mod_is_assembled(ezgmsk_mod _q);

// assemble frame
//  _q              : frame modulator object
//  _data           : raw frame data [size: _data_len x 1]
//  _data_len       : raw data length (bytes)
int ezgmsk_mod_assemble(ezgmsk_mod            _q,
                        const unsigned char * _data,
                        unsigned int          _data_len);

// get frame length (number of samples)
unsigned int ezgmsk_mod_get_frame_len(ezgmsk_mod _q);

// generate frame samples
//  _q              : frame modulator object
//  _buf            : output buffer [size: _buf_len x 1]
//  _buf_len        : output buffer length
//  returns: 1 if frame complete, 0 otherwise
int ezgmsk_mod_execute(ezgmsk_mod     _q,
                       liquid_float_complex * _buf,
                       unsigned int           _buf_len);


#ifdef __cplusplus
}
} // namespace ezgmsk
#endif