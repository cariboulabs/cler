/*
 * EZGMSK Modulator Implementation
 * Based on liquid-dsp gmskframegen structure
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <complex.h>

#include "_ezgmsk_mod.h"
#include "liquid.internal.h"

// internal methods
int ezgmsk_mod_write_zeros(ezgmsk_mod _q);
int ezgmsk_mod_write_preamble(ezgmsk_mod _q);
int ezgmsk_mod_write_data(ezgmsk_mod _q);
int ezgmsk_mod_write_tail(ezgmsk_mod _q);
int ezgmsk_mod_gen_symbol(ezgmsk_mod _q);

// ezgmsk_mod object structure
struct ezgmsk_mod_s {
    gmskmod mod;                // GMSK modulator
    unsigned int k;             // filter samples/symbol
    unsigned int m;             // filter semi-length (symbols)
    float BT;                   // filter bandwidth-time product

    // framing lengths (symbols)
    unsigned int preamble_len;  // preamble length in symbols
    unsigned int data_len;      // data length in symbols (8 * data_bytes)
    unsigned int tail_len;      // tail length (2*m)

    // preamble pattern
    unsigned char * preamble;   // preamble pattern (symbols)

    // data buffer
    unsigned int data_bytes;    // data length in bytes
    unsigned char * data;       // frame data

    // framing state
    ezgmsk_mod_state_en state;
    int frame_assembled;        // frame assembled flag
    int frame_complete;         // frame completed flag
    unsigned int symbol_counter;//

    // output sample buffer (one symbol's worth of data)
    complex float * buf_sym;    // size: k x 1
    unsigned int    buf_idx;    // output sample buffer index
};

// create GMSK frame modulator
ezgmsk_mod ezgmsk_mod_create_set(unsigned int _k,
                                 unsigned int _m,
                                 float        _BT,
                                 unsigned int _preamble_symbols_len)
{
    ezgmsk_mod q = (ezgmsk_mod) malloc(sizeof(struct ezgmsk_mod_s));

    // set internal properties
    q->k  = _k;     // samples/symbol
    q->m  = _m;     // filter delay (symbols)
    q->BT = _BT;    // filter bandwidth-time product

    // create modulator
    q->mod = gmskmod_create(q->k, q->m, q->BT);

    // set frame parameters
    q->preamble_len = _preamble_symbols_len;
    q->tail_len = 2 * q->m;

    // allocate and generate alternating preamble pattern (like demodulator expects)
    q->preamble = (unsigned char*) malloc(q->preamble_len * sizeof(unsigned char));
    unsigned int i;
    for (i = 0; i < q->preamble_len; i++) {
        q->preamble[i] = i % 2;  // alternating 0,1,0,1...
    }

    // initialize data to zero
    q->data_bytes = 0;
    q->data_len = 0;
    q->data = NULL;

    // allocate memory for output symbol buffer
    q->buf_sym = (float complex*)malloc(q->k*sizeof(float complex));

    // reset object
    ezgmsk_mod_reset(q);
    
    return q;
}

// destroy GMSK frame modulator
int ezgmsk_mod_destroy(ezgmsk_mod _q)
{
    // destroy gmsk modulator
    gmskmod_destroy(_q->mod);

    // free preamble
    free(_q->preamble);

    // free data buffer
    if (_q->data) free(_q->data);

    // free symbol buffer
    free(_q->buf_sym);

    // free main object memory
    free(_q);
    return LIQUID_OK;
}

// print GMSK frame modulator internals
int ezgmsk_mod_print(ezgmsk_mod _q)
{
    printf("ezgmsk_mod:\n");
    printf("  physical properties\n");
    printf("    samples/symbol  :   %u\n", _q->k);
    printf("    filter delay    :   %u symbols\n", _q->m);
    printf("    bandwidth-time  :   %-8.3f\n", _q->BT);
    printf("  framing properties\n");
    printf("    preamble        :   %-4u symbols\n", _q->preamble_len);
    printf("    data            :   %-4u symbols (%u bytes)\n", _q->data_len, _q->data_bytes);
    printf("    tail            :   %-4u symbols\n", _q->tail_len);
    printf("  total samples     :   %-4u samples\n", ezgmsk_mod_get_frame_len(_q));
    return LIQUID_OK;
}

// reset GMSK frame modulator
int ezgmsk_mod_reset(ezgmsk_mod _q)
{
    // reset GMSK modulator
    gmskmod_reset(_q->mod);

    // reset states
    _q->state = EZGMSK_MOD_STATE_UNASSEMBLED;
    _q->frame_assembled = 0;
    _q->frame_complete  = 0;
    _q->symbol_counter  = 0;
    _q->buf_idx         = _q->k; // indicate buffer is empty
    return LIQUID_OK;
}

// check if frame is assembled
int ezgmsk_mod_is_assembled(ezgmsk_mod _q)
{
    return _q->frame_assembled;
}

// assemble frame
int ezgmsk_mod_assemble(ezgmsk_mod            _q,
                        const unsigned char * _data,
                        unsigned int          _data_len)
{
    // reset frame generator state
    ezgmsk_mod_reset(_q);

    // update data
    _q->data_bytes = _data_len;
    _q->data_len = 8 * _data_len;  // convert to symbols (bits)
    _q->data = (unsigned char*) realloc(_q->data, _data_len * sizeof(unsigned char));
    if (_data && _data_len > 0) {
        memcpy(_q->data, _data, _data_len * sizeof(unsigned char));
    }

    // set assembled flag and initial state
    _q->frame_assembled = 1;
    _q->state = EZGMSK_MOD_STATE_PREAMBLE;
    
    return LIQUID_OK;
}

// get frame length (number of samples)
unsigned int ezgmsk_mod_get_frame_len(ezgmsk_mod _q)
{
    if (!_q->frame_assembled) {
        return 0;
    }

    unsigned int num_frame_symbols = 
        _q->preamble_len +      // preamble symbols
        _q->data_len +          // data symbols (bits)
        _q->tail_len;           // tail symbols

    return num_frame_symbols * _q->k;  // k samples/symbol
}

// generate frame samples
int ezgmsk_mod_execute(ezgmsk_mod     _q,
                       liquid_float_complex * _buf,
                       unsigned int           _buf_len)
{
    unsigned int i;
    for (i = 0; i < _buf_len; i++) {
        // fill buffer if needed
        if (_q->buf_idx == _q->k)
            ezgmsk_mod_gen_symbol(_q);

        // save output sample
        _buf[i] = _q->buf_sym[_q->buf_idx++];
    }
    return _q->frame_complete;
}

// generate a symbol's worth of samples to internal buffer
int ezgmsk_mod_gen_symbol(ezgmsk_mod _q)
{
    _q->buf_idx = 0;

    switch (_q->state) {
    case EZGMSK_MOD_STATE_UNASSEMBLED: 
        ezgmsk_mod_write_zeros(_q); 
        break;
    case EZGMSK_MOD_STATE_PREAMBLE:    
        ezgmsk_mod_write_preamble(_q); 
        break;
    case EZGMSK_MOD_STATE_DATA:      
        ezgmsk_mod_write_data(_q); 
        break;
    case EZGMSK_MOD_STATE_TAIL:        
        ezgmsk_mod_write_tail(_q); 
        break;
    default:
        return liquid_error(LIQUID_EINT,"ezgmsk_mod_gen_symbol(), invalid internal state");
    }

    return LIQUID_OK;
}

// write zeros (unassembled state)
int ezgmsk_mod_write_zeros(ezgmsk_mod _q)
{
    memset(_q->buf_sym, 0x0, _q->k*sizeof(float complex));
    return LIQUID_OK;
}

// write preamble symbols
int ezgmsk_mod_write_preamble(ezgmsk_mod _q)
{
    // get current preamble symbol
    unsigned char bit = _q->preamble[_q->symbol_counter] & 0x01;
    gmskmod_modulate(_q->mod, bit, _q->buf_sym);

    // apply ramping window to first 'm' symbols
    if (_q->symbol_counter < _q->m) {
        unsigned int i;
        for (i = 0; i < _q->k; i++)
            _q->buf_sym[i] *= liquid_hamming(_q->symbol_counter*_q->k + i, 2*_q->m*_q->k);
    }

    _q->symbol_counter++;

    if (_q->symbol_counter == _q->preamble_len) {
        _q->symbol_counter = 0;
        _q->state = EZGMSK_MOD_STATE_DATA;
    }
    return LIQUID_OK;
}

// write data bits
int ezgmsk_mod_write_data(ezgmsk_mod _q)
{
    if (_q->data_len == 0) {
        // skip data if length is zero
        _q->state = EZGMSK_MOD_STATE_TAIL;
        return LIQUID_OK;
    }

    // determine byte and bit indices
    div_t d = div(_q->symbol_counter, 8);
    unsigned int byte_index = d.quot;
    unsigned int bit_index  = d.rem;
    unsigned char byte = _q->data[byte_index];
    unsigned char bit  = (byte >> (7 - bit_index)) & 0x01;

    gmskmod_modulate(_q->mod, bit, _q->buf_sym);

    _q->symbol_counter++;
    
    if (_q->symbol_counter == _q->data_len) {
        _q->symbol_counter = 0;
        _q->state = EZGMSK_MOD_STATE_TAIL;
    }
    return LIQUID_OK;
}

// write tail symbols
int ezgmsk_mod_write_tail(ezgmsk_mod _q)
{
    // generate random tail bits
    unsigned char bit = rand() % 2;
    gmskmod_modulate(_q->mod, bit, _q->buf_sym);

    // apply ramping window to last 'm' symbols
    if (_q->symbol_counter >= _q->m) {
        unsigned int i;
        for (i = 0; i < _q->k; i++)
            _q->buf_sym[i] *= liquid_hamming(_q->m*_q->k + (_q->symbol_counter-_q->m)*_q->k + i, 2*_q->m*_q->k);
    }

    _q->symbol_counter++;

    if (_q->symbol_counter == _q->tail_len) {
        _q->symbol_counter = 0;
        _q->frame_complete = 1;
        _q->state = EZGMSK_MOD_STATE_UNASSEMBLED;
    }
    return LIQUID_OK;
}

