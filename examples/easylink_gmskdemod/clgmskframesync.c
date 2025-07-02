/*
 * Copyright (c) 2007 - 2023 Joseph Gaeddert
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// clgmskframesync.c

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include <assert.h>
#include <stdbool.h>

#include "clgmskframesync.h"
#include "liquid.internal.h"

// enable pre-demodulation filter (remove out-of-band noise)
#define CLGMSKFRAMESYNC_PREFILTER         (0)

// execute a single, post-filtered sample
int clgmskframesync_execute_sample(clgmskframesync _q,
                                 float complex _x);

// push buffered p/n sequence through synchronizer
int clgmskframesync_pushpn(clgmskframesync _q);

// ...
int clgmskframesync_syncpn(clgmskframesync _q);

// update instantaneous frequency estimate
int clgmskframesync_update_fi(clgmskframesync _q,
                            float complex _x);

// update symbol synchronizer internal state (filtered error, index, etc.)
//  _q      :   frame synchronizer
//  _x      :   input sample
//  _y      :   output symbol
int clgmskframesync_update_symsync(clgmskframesync _q,
                                float         _x,
                                float *       _y);

// execute stages
int clgmskframesync_execute_detectframe(clgmskframesync _q, float complex _x);
int clgmskframesync_execute_rxpreamble( clgmskframesync _q, float complex _x);
int clgmskframesync_execute_syncword( clgmskframesync _q, float complex _x);
int clgmskframesync_execute_rxheader(   clgmskframesync _q, float complex _x);
int clgmskframesync_execute_rxpayload(  clgmskframesync _q, float complex _x);

// decode header
int clgmskframesync_decode_header(clgmskframesync _q);

// clgmskframesync object structure
struct clgmskframesync_s {
#if CLGMSKFRAMESYNC_PREFILTER
    iirfilt_crcf prefilter;         // pre-demodulation filter
#endif
    unsigned int k;                 // filter samples/symbol
    unsigned int m;                 // filter semi-length (symbols)
    float BT;                       // filter bandwidth-time product
    framesync_callback callback;    // user-defined callback function
    void * userdata;                // user-defined data structure
    framesyncstats_s framesyncstats;// frame statistic object
    framedatastats_s framedatastats;// frame statistic object (packet statistics)

    //
    float complex x_prime;          // received sample state
    float fi_hat;                   // instantaneous frequency estimate
    
    // timing recovery objects, states
    firpfb_rrrf mf;                 // matched filter decimator
    firpfb_rrrf dmf;                // derivative matched filter decimator
    unsigned int npfb;              // number of filters in symsync
    float pfb_q;                    // filtered timing error
    float pfb_soft;                 // soft filterbank index
    int pfb_index;                  // hard filterbank index
    int pfb_timer;                  // filterbank output flag
    float symsync_out;              // symbol synchronizer output

    // synchronizer objects
    detector_cccf frame_detector;   // pre-demod detector
    float tau_hat;                  // fractional timing offset estimate
    float dphi_hat;                 // carrier frequency offset estimate
    float gamma_hat;                // channel gain estimate
    windowcf buffer;                // pre-demod buffered samples, size: k*(pn_len+m)
    nco_crcf nco_coarse;            // coarse carrier frequency recovery
    
    // preamble
    unsigned int preamble_len;      // number of symbols in preamble
    float * preamble_pn;            // preamble p/n sequence (known)
    float * preamble_rx;            // preamble p/n sequence (received)

    //syncword
    unsigned int syncword_symbols_len;          // length of syncword
    size_t syncword_lookup_multiplier;
    unsigned char * syncword_symbols_expected;  
    unsigned char * syncword_symbols_est; //has double the length of expected so we can decode easier

    // header
    unsigned int header_user_len;
    unsigned int header_enc_len;
    unsigned int header_mod_len;
    unsigned char * header_mod;
    unsigned char * header_enc;
    unsigned char * header_dec;
    packetizer p_header;
    int header_valid;

    // payload
    char payload_byte;              // received byte
    crc_scheme check;               // payload validity check
    fec_scheme fec0;                // payload FEC (inner)
    fec_scheme fec1;                // payload FEC (outer)
    unsigned int payload_enc_len;   // length of encoded payload
    unsigned int payload_dec_len;   // payload length (num un-encoded bytes)
    unsigned char * payload_enc;    // payload data (encoded bytes)
    unsigned char * payload_dec;    // payload data (encoded bytes)
    packetizer p_payload;           // payload packetizer
    int payload_valid;              // did payload pass crc?
    
    // status variables
    enum {
        STATE_DETECTFRAME=0,
        STATE_RXSYNCWORD,        
        STATE_RXPREAMBLE,           
        STATE_RXHEADER,           
        STATE_RXPAYLOAD,
    } state;
    unsigned int preamble_counter;  // counter: num of p/n syms received
    unsigned int syncword_counter; // counter: num of syncword syms received
    unsigned int header_counter;    // counter: num of header syms received
    unsigned int payload_counter;   // counter: num of payload syms received
};

// create GMSK frame synchronizer
//  _k          :   samples/symbol
//  _m          :   filter delay (symbols)
//  _BT         :   excess bandwidth factor
//  _callback   :   callback function
//  _userdata   :   user data pointer passed to callback function
clgmskframesync clgmskframesync_create_set(unsigned int       _k,
                                        unsigned int       _m,
                                        float              _BT,
                                        const unsigned char*  _syncword_symbols,
                                        unsigned int       _syncword_symbols_len,
                                        framesync_callback _callback,
                                        void *             _userdata)
{
    clgmskframesync q = (clgmskframesync) malloc(sizeof(struct clgmskframesync_s));
    q->callback = _callback;
    q->userdata = _userdata;
    q->k        = _k;        // samples/symbol
    q->m        = _m;        // filter delay (symbols)
    q->BT       = _BT;      // filter bandwidth-time product

#if CLGMSKFRAMESYNC_PREFILTER
    // create default low-pass Butterworth filter
    q->prefilter = iirfilt_crcf_create_lowpass(3, 0.5f*(1 + q->BT) / (float)(q->k));
#endif

    unsigned int i;

    // frame detector
    q->preamble_len = 63;
    q->preamble_pn = (float*)malloc(q->preamble_len*sizeof(float));
    q->preamble_rx = (float*)malloc(q->preamble_len*sizeof(float));
    float complex preamble_samples[q->preamble_len*q->k];
    msequence ms = msequence_create(6, 0x6d, 1);
    gmskmod mod = gmskmod_create(q->k, q->m, q->BT);

    for (i=0; i<q->preamble_len + q->m; i++) {
        unsigned char bit = msequence_advance(ms);

        // save p/n sequence
        if (i < q->preamble_len)
            q->preamble_pn[i] = bit ? 1.0f : -1.0f;
        
        // modulate/interpolate
        if (i < q->m) gmskmod_modulate(mod, bit, &preamble_samples[0]);
        else          gmskmod_modulate(mod, bit, &preamble_samples[(i-q->m)*q->k]);
    }

    gmskmod_destroy(mod);
    msequence_destroy(ms);

#if 0
    // print sequence
    for (i=0; i<q->preamble_len*q->k; i++)
        printf("preamble(%3u) = %12.8f + j*%12.8f;\n", i+1, crealf(preamble_samples[i]), cimagf(preamble_samples[i]));
#endif
    // create frame detector
    float threshold = 0.5f;     // detection threshold
    float dphi_max  = 0.2f;    // maximum carrier offset allowable
    q->frame_detector = detector_cccf_create(preamble_samples, q->preamble_len*q->k, threshold, dphi_max);
    q->buffer = windowcf_create(q->k*(q->preamble_len+q->m));

    // create symbol timing recovery filters
    q->npfb = 32;   // number of filters in the bank
    q->mf   = firpfb_rrrf_create_rnyquist( LIQUID_FIRFILT_GMSKRX,q->npfb,q->k,q->m,q->BT);
    q->dmf  = firpfb_rrrf_create_drnyquist(LIQUID_FIRFILT_GMSKRX,q->npfb,q->k,q->m,q->BT);

    // create down-coverters for carrier phase tracking
    q->nco_coarse = nco_crcf_create(LIQUID_NCO);

    //create/allocate syncword objects/arrays
    q->syncword_lookup_multiplier = 8;
    q->syncword_symbols_len = _syncword_symbols_len;
    q->syncword_symbols_expected = (unsigned char*) malloc(q->syncword_lookup_multiplier *
        q->syncword_symbols_len*sizeof(unsigned char));
    q->syncword_symbols_est = (unsigned char*) malloc(q->syncword_symbols_len*sizeof(unsigned char));
    memcpy(q->syncword_symbols_expected, _syncword_symbols, q->syncword_symbols_len*sizeof(unsigned char));

    // create/allocate header objects/arrays
    q->header_mod = NULL;
    q->header_enc = NULL;
    q->header_dec = NULL;
    q->p_header = NULL;
    clgmskframesync_set_header_len(q, GMSKFRAME_H_USER_DEFAULT);

    // create/allocate payload objects/arrays
    q->payload_dec_len = 1;
    q->check           = LIQUID_CRC_32;
    q->fec0            = LIQUID_FEC_NONE;
    q->fec1            = LIQUID_FEC_NONE;
    q->p_payload = packetizer_create(q->payload_dec_len,
                                     q->check,
                                     q->fec0,
                                     q->fec1);
    q->payload_enc_len = packetizer_get_enc_msg_len(q->p_payload);
    q->payload_dec = (unsigned char*) malloc(q->payload_dec_len*sizeof(unsigned char));
    q->payload_enc = (unsigned char*) malloc(q->payload_enc_len*sizeof(unsigned char));

    // reset synchronizer
    clgmskframesync_reset(q);

    // reset global data counters
    clgmskframesync_reset_framedatastats(q);

    // return synchronizer object
    return q;
}

// destroy frame synchronizer object, freeing all internal memory
int clgmskframesync_destroy(clgmskframesync _q)
{
    // destroy synchronizer objects
#if CLGMSKFRAMESYNC_PREFILTER
    iirfilt_crcf_destroy(_q->prefilter);// pre-demodulator filter
#endif
    firpfb_rrrf_destroy(_q->mf);                // matched filter
    firpfb_rrrf_destroy(_q->dmf);               // derivative matched filter
    nco_crcf_destroy(_q->nco_coarse);           // coarse NCO

    // preamble
    detector_cccf_destroy(_q->frame_detector);
    windowcf_destroy(_q->buffer);
    free(_q->preamble_pn);
    free(_q->preamble_rx);
    
    // header
    packetizer_destroy(_q->p_header);
    free(_q->header_mod);
    free(_q->header_enc);
    free(_q->header_dec);

    // payload
    packetizer_destroy(_q->p_payload);
    free(_q->payload_enc);
    free(_q->payload_dec);

    // free main object memory
    free(_q);
    return LIQUID_OK;
}

// print frame synchronizer object internals
int clgmskframesync_print(clgmskframesync _q)
{
    printf("<liquid.clgmskframesync>\n");
    return LIQUID_OK;
}

int clgmskframesync_set_header_len(clgmskframesync _q,
                                 unsigned int _len)
{
    _q->header_user_len = _len;
    unsigned int header_dec_len = GMSKFRAME_H_DEC + _q->header_user_len;
    _q->header_dec = (unsigned char*)realloc(_q->header_dec, header_dec_len*sizeof(unsigned char));

    if (_q->p_header) {
        packetizer_destroy(_q->p_header);
    }

    _q->p_header = packetizer_create(header_dec_len,
                                     GMSKFRAME_H_CRC,
                                     GMSKFRAME_H_FEC,
                                     LIQUID_FEC_NONE);

    _q->header_enc_len = packetizer_get_enc_msg_len(_q->p_header);
    _q->header_enc = (unsigned char*)realloc(_q->header_enc, _q->header_enc_len*sizeof(unsigned char));

    _q->header_mod_len = _q->header_enc_len * 8;
    _q->header_mod = (unsigned char*)realloc(_q->header_mod, _q->header_mod_len*sizeof(unsigned char));
    return LIQUID_OK;
}

// reset frame synchronizer object
int clgmskframesync_reset(clgmskframesync _q)
{
    // reset state and counters
    _q->state = STATE_DETECTFRAME;
    _q->preamble_counter = 0;
    _q->syncword_counter = 0;
    _q->header_counter   = 0;
    _q->payload_counter  = 0;
    
    // clear pre-demod buffer
    windowcf_reset(_q->buffer);

    // reset internal objects
    detector_cccf_reset(_q->frame_detector);
    
    // reset carrier recovery objects
    nco_crcf_reset(_q->nco_coarse);

    // reset sample state
    _q->x_prime = 0.0f;
    _q->fi_hat  = 0.0f;
    
    // reset symbol timing recovery state
    firpfb_rrrf_reset(_q->mf);
    firpfb_rrrf_reset(_q->dmf);
    _q->pfb_q = 0.0f;   // filtered error signal
    return LIQUID_OK;
}

int clgmskframesync_is_frame_open(clgmskframesync _q)
{
    return (_q->state == STATE_DETECTFRAME) ? 0 : 1;
}

int clgmskframesync_execute_sample(clgmskframesync _q,
                                 float complex _x)
{
    switch (_q->state) {
    case STATE_DETECTFRAME: return clgmskframesync_execute_detectframe(_q, _x);
    case STATE_RXPREAMBLE:  return clgmskframesync_execute_rxpreamble (_q, _x);
    case STATE_RXSYNCWORD:  return clgmskframesync_execute_syncword   (_q, _x);
    case STATE_RXHEADER:    return clgmskframesync_execute_rxheader   (_q, _x);
    case STATE_RXPAYLOAD:   return clgmskframesync_execute_rxpayload  (_q, _x);
    default:;
    }

    return liquid_error(LIQUID_EINT,"clgmskframesync_execute_sample(), invalid internal state");
}

// execute frame synchronizer
//  _q      :   frame synchronizer object
//  _x      :   input sample array [size: _n x 1]
//  _n      :   number of input samples
int clgmskframesync_execute(clgmskframesync   _q,
                          float complex * _x,
                          unsigned int    _n)
{
    // push through synchronizer
    unsigned int i;
    for (i=0; i<_n; i++) {
        float complex xf;   // input sample
#if CLGMSKFRAMESYNC_PREFILTER
        iirfilt_crcf_execute(_q->prefilter, _x[i], &xf);
#else
        xf = _x[i];
#endif

        clgmskframesync_execute_sample(_q, xf);
    }
    return LIQUID_OK;
}

// reset frame data statistics
int clgmskframesync_reset_framedatastats(clgmskframesync _q)
{
    return framedatastats_reset(&_q->framedatastats);
}

// retrieve frame data statistics
framedatastats_s clgmskframesync_get_framedatastats(clgmskframesync _q)
{
    return _q->framedatastats;
}

// 
// internal methods
//

// update symbol synchronizer internal state (filtered error, index, etc.)
//  _q      :   frame synchronizer
//  _x      :   input sample
//  _y      :   output symbol
int clgmskframesync_update_symsync(clgmskframesync _q,
                                 float         _x,
                                 float *       _y)
{
    // push sample into filterbanks
    firpfb_rrrf_push(_q->mf,  _x);
    firpfb_rrrf_push(_q->dmf, _x);

    //
    float mf_out  = 0.0f;    // matched-filter output
    float dmf_out = 0.0f;    // derivatived matched-filter output
    int sample_available = 0;

    // compute output if timeout
    if (_q->pfb_timer <= 0) {
        sample_available = 1;

        // reset timer
        _q->pfb_timer = _q->k;  // k samples/symbol

        firpfb_rrrf_execute(_q->mf,  _q->pfb_index, &mf_out);
        firpfb_rrrf_execute(_q->dmf, _q->pfb_index, &dmf_out);

        // update filtered timing error
        // lo  bandwidth parameters: {0.92, 1.20}, about 100 symbols settling time
        // med bandwidth parameters: {0.98, 0.20}, about 200 symbols settling time
        // hi  bandwidth parameters: {0.99, 0.05}, about 500 symbols settling time
        _q->pfb_q = 0.99f*_q->pfb_q + 0.05f*crealf( conjf(mf_out)*dmf_out );

        // accumulate error into soft filterbank value
        _q->pfb_soft += _q->pfb_q;

        // compute actual filterbank index
        _q->pfb_index = roundf(_q->pfb_soft);

        // constrain index to be in [0, npfb-1]
        while (_q->pfb_index < 0) {
            _q->pfb_index += _q->npfb;
            _q->pfb_soft  += _q->npfb;

            // adjust pfb output timer
            _q->pfb_timer--;
        }
        while (_q->pfb_index > _q->npfb-1) {
            _q->pfb_index -= _q->npfb;
            _q->pfb_soft  -= _q->npfb;

            // adjust pfb output timer
            _q->pfb_timer++;
        }
        //printf("  b/soft    :   %12.8f\n", _q->pfb_soft);
    }

    // decrement symbol timer
    _q->pfb_timer--;

    // set output and return
    *_y = mf_out / (float)(_q->k);
    
    return sample_available;
}

// push buffered p/n sequence through synchronizer
int clgmskframesync_pushpn(clgmskframesync _q)
{
    unsigned int i;

    // reset filterbanks
    firpfb_rrrf_reset(_q->mf);
    firpfb_rrrf_reset(_q->dmf);

    // read buffer
    float complex * rc;
    windowcf_read(_q->buffer, &rc);

    // compute delay and filterbank index
    //  tau_hat < 0 :   delay = 2*k*m-1, index = round(   tau_hat *npfb), flag = 0
    //  tau_hat > 0 :   delay = 2*k*m-2, index = round((1-tau_hat)*npfb), flag = 0
    assert(_q->tau_hat < 0.5f && _q->tau_hat > -0.5f);
    unsigned int delay = 2*_q->k*_q->m - 1; // samples to buffer before computing output
    _q->pfb_soft       = -_q->tau_hat*_q->npfb;
    _q->pfb_index      = (int) roundf(_q->pfb_soft);
    while (_q->pfb_index < 0) {
        delay         -= 1;
        _q->pfb_index += _q->npfb;
        _q->pfb_soft  += _q->npfb;
    }
    _q->pfb_timer = 0;

    // set coarse carrier frequency offset
    nco_crcf_set_frequency(_q->nco_coarse, _q->dphi_hat);
    
    unsigned int buffer_len = (_q->preamble_len + _q->m) * _q->k;
    for (i=0; i<delay; i++) {
        float complex y;
        nco_crcf_mix_down(_q->nco_coarse, rc[i], &y);
        nco_crcf_step(_q->nco_coarse);

        // update instantanenous frequency estimate
        clgmskframesync_update_fi(_q, y);

        // push initial samples into filterbanks
        firpfb_rrrf_push(_q->mf,  _q->fi_hat);
        firpfb_rrrf_push(_q->dmf, _q->fi_hat);
    }

    // set state (still need a few more samples before entire p/n
    // sequence has been received)
    _q->state = STATE_RXPREAMBLE;

    for (i=delay; i<buffer_len; i++) {
        // run remaining samples through sample state machine
        clgmskframesync_execute_sample(_q, rc[i]);
    }
    return LIQUID_OK;
}

// 
int clgmskframesync_syncpn(clgmskframesync _q)
{
#if 0
    // compare expected p/n sequence with received
    unsigned int i;
    for (i=0; i<_q->preamble_len; i++)
        printf("  %3u : %12.8f : %12.8f\n", i, _q->preamble_pn[i], _q->preamble_rx[i]);
#endif
    return LIQUID_OK;
}

// update instantaneous frequency estimate
int clgmskframesync_update_fi(clgmskframesync _q,
                            float complex _x)
{
    // compute differential phase
    _q->fi_hat = cargf(conjf(_q->x_prime)*_x) * _q->k;

    // update internal state
    _q->x_prime = _x;
    return LIQUID_OK;
}

int clgmskframesync_execute_detectframe(clgmskframesync _q,
                                      float complex _x)
{
    // push sample into pre-demod p/n sequence buffer
    windowcf_push(_q->buffer, _x);

    // push through pre-demod synchronizer
    int detected = detector_cccf_correlate(_q->frame_detector,
                                           _x,
                                           &_q->tau_hat,
                                           &_q->dphi_hat,
                                           &_q->gamma_hat);

    // check if frame has been detected
    if (detected) {
        //printf("***** frame detected! tau-hat:%8.4f, dphi-hat:%8.4f, gamma:%8.2f dB\n",
        //        _q->tau_hat, _q->dphi_hat, 20*log10f(_q->gamma_hat));

        // push buffered samples through synchronizer
        // NOTE: state will be updated to STATE_RXPREAMBLE internally
        clgmskframesync_pushpn(_q);
    }
    return LIQUID_OK;
}

int clgmskframesync_execute_rxpreamble(clgmskframesync _q,
                                     float complex _x)
{
    // validate input
    if (_q->preamble_counter == _q->preamble_len)
        return liquid_error(LIQUID_EINT,"clgmskframesync_execute_rxpn(), p/n buffer already full!\n");

    // mix signal down
    float complex y;
    nco_crcf_mix_down(_q->nco_coarse, _x, &y);
    nco_crcf_step(_q->nco_coarse);

    // update instantanenous frequency estimate
    clgmskframesync_update_fi(_q, y);

    // update symbol synchronizer
    float mf_out = 0.0f;
    int sample_available = clgmskframesync_update_symsync(_q, _q->fi_hat, &mf_out);

    // compute output if timeout
    if (sample_available) {
        // save output in p/n symbols buffer
        _q->preamble_rx[ _q->preamble_counter ] = mf_out / (float)(_q->k);

        // update counter
        _q->preamble_counter++;

        if (_q->preamble_counter == _q->preamble_len) {
            clgmskframesync_syncpn(_q);
            _q->state = STATE_RXSYNCWORD;
            printf("preamble received, switching to STATE_RXSYNCWORD\n");
        }
    }
    return LIQUID_OK;
}

int clgmskframesync_execute_syncword(clgmskframesync _q,
                                   float complex _x)
{
    // mix signal down
    float complex y;
    nco_crcf_mix_down(_q->nco_coarse, _x, &y);
    nco_crcf_step(_q->nco_coarse);

    // update instantanenous frequency estimate
    clgmskframesync_update_fi(_q, y);

    // update symbol synchronizer
    float mf_out = 0.0f;
    int sample_available = clgmskframesync_update_symsync(_q, _q->fi_hat, &mf_out);

    // compute output if timeout
    if (sample_available) {
        // demodulate
        unsigned char s = mf_out > 0.0f ? 1 : 0;
        _q->syncword_symbols_est[_q->syncword_counter] = s;

        _q->syncword_counter++;
        if (_q->syncword_counter >= _q->syncword_symbols_len) {
            clgmskframesync_decode_syncword(_q);
        }
    }
}

int clgmskframesync_execute_rxheader(clgmskframesync _q,
                                   float complex _x)
{
    // mix signal down
    float complex y;
    nco_crcf_mix_down(_q->nco_coarse, _x, &y);
    nco_crcf_step(_q->nco_coarse);

    // update instantanenous frequency estimate
    clgmskframesync_update_fi(_q, y);

    // update symbol synchronizer
    float mf_out = 0.0f;
    int sample_available = clgmskframesync_update_symsync(_q, _q->fi_hat, &mf_out);

    // compute output if timeout
    if (sample_available) {
        // demodulate
        unsigned char s = mf_out > 0.0f ? 1 : 0;

        // TODO: update evm

        // save bit in buffer
        _q->header_mod[_q->header_counter] = s;

        // increment header counter
        _q->header_counter++;
        if (_q->header_counter == _q->header_mod_len) {
            // decode header
            clgmskframesync_decode_header(_q);

            // invoke callback if header is invalid
            _q->framedatastats.num_frames_detected++;
            if (!_q->header_valid && _q->callback != NULL) {
                // set framesyncstats internals
                _q->framesyncstats.rssi          = 20*log10f(_q->gamma_hat);
                _q->framesyncstats.evm           = 0.0f;
                _q->framesyncstats.framesyms     = NULL;
                _q->framesyncstats.num_framesyms = 0;
                _q->framesyncstats.mod_scheme    = LIQUID_MODEM_UNKNOWN;
                _q->framesyncstats.mod_bps       = 1;
                _q->framesyncstats.check         = LIQUID_CRC_UNKNOWN;
                _q->framesyncstats.fec0          = LIQUID_FEC_UNKNOWN;
                _q->framesyncstats.fec1          = LIQUID_FEC_UNKNOWN;

                // invoke callback method
                _q->callback(_q->header_dec,
                             _q->header_valid,
                             NULL,
                             0,
                             0,
                             _q->framesyncstats,
                             _q->userdata);

                clgmskframesync_reset(_q);
            }

            // reset if invalid
            if (!_q->header_valid)
                return clgmskframesync_reset(_q);

            // update state
            _q->state = STATE_RXPAYLOAD;
        }
    }
    return LIQUID_OK;
}

int clgmskframesync_execute_rxpayload(clgmskframesync _q,
                                    float complex _x)
{
    // mix signal down
    float complex y;
    nco_crcf_mix_down(_q->nco_coarse, _x, &y);
    nco_crcf_step(_q->nco_coarse);

    // update instantanenous frequency estimate
    clgmskframesync_update_fi(_q, y);

    // update symbol synchronizer
    float mf_out = 0.0f;
    int sample_available = clgmskframesync_update_symsync(_q, _q->fi_hat, &mf_out);

    // compute output if timeout
    if (sample_available) {
        // demodulate
        unsigned char s = mf_out > 0.0f ? 1 : 0;

        // TODO: update evm

        // save payload
        _q->payload_byte <<= 1;
        _q->payload_byte |= s ? 0x01 : 0x00;
        _q->payload_enc[_q->payload_counter/8] = _q->payload_byte;

        // increment counter
        _q->payload_counter++;

        if (_q->payload_counter == 8*_q->payload_enc_len) {
            // decode payload
            _q->payload_valid = packetizer_decode(_q->p_payload,
                                                  _q->payload_enc,
                                                  _q->payload_dec);

            // update statistics
            _q->framedatastats.num_headers_valid++;
            _q->framedatastats.num_payloads_valid += _q->payload_valid;
            _q->framedatastats.num_bytes_received += _q->payload_dec_len;

            // invoke callback
            if (_q->callback != NULL) {
                // set framesyncstats internals
                _q->framesyncstats.rssi          = 20*log10f(_q->gamma_hat);
                _q->framesyncstats.evm           = 0.0f;
                _q->framesyncstats.framesyms     = NULL;
                _q->framesyncstats.num_framesyms = 0;
                _q->framesyncstats.mod_scheme    = LIQUID_MODEM_UNKNOWN;
                _q->framesyncstats.mod_bps       = 1;
                _q->framesyncstats.check         = _q->check;
                _q->framesyncstats.fec0          = _q->fec0;
                _q->framesyncstats.fec1          = _q->fec1;

                // invoke callback method
                _q->callback(_q->header_dec,
                             _q->header_valid,
                             _q->payload_dec,
                             _q->payload_dec_len,
                             _q->payload_valid,
                             _q->framesyncstats,
                             _q->userdata);
            }

            // reset frame synchronizer
            clgmskframesync_reset(_q);
        }
    }
    return LIQUID_OK;
}

int clgmskframesync_decode_syncword(clgmskframesync _q)
{
    // check if syncword matches
    unsigned int i;
    unsigned int start = _q->syncword_counter - _q->syncword_symbols_len;
    
    for (i = 0; i<_q->syncword_symbols_len; i++) 
    {
        if (_q->syncword_symbols_est[start + i] != _q->syncword_symbols_expected[i]) {
            printf("syncword does not match");
            for (i=0; i<_q->syncword_symbols_len; i++) {
                printf("%d ", _q->syncword_symbols_expected[i]);
            }
            printf("\n");
            for (i=0; i<_q->syncword_symbols_len; i++) {
                printf("%d ", _q->syncword_symbols_est[start + i]);
            }
            printf("\n");
            //print hamming distance
            unsigned int hamming_distance = 0;
            for (i=0; i<_q->syncword_symbols_len; i++) {
                if (_q->syncword_symbols_expected[i] != _q->syncword_symbols_est[start + i]) {
                    hamming_distance++;
                }
            }
            printf("hamming distance: %u\n", hamming_distance);

            if (start + i == _q->syncword_lookup_multiplier * _q->syncword_symbols_len) {
                printf("syncword lookup multiplier reached, resetting synchronizer\n");
                return clgmskframesync_reset(_q);
            } else {
                return 0;
            }
        }
    }
    printf("syncword matches, resetting synchronizer\n");
    clgmskframesync_reset(_q);
}

// decode header and re-configure payload decoder
int clgmskframesync_decode_header(clgmskframesync _q)
{
    // pack each 1-bit header symbols into 8-bit bytes
    unsigned int num_written;
    liquid_pack_bytes(_q->header_mod, _q->header_mod_len,
                      _q->header_enc, _q->header_enc_len,
                      &num_written);
    assert(num_written==_q->header_enc_len);

    // run packet decoder
    _q->header_valid = true; // do the thing here

    if (!_q->header_valid) {
        return LIQUID_OK;
    }

    unsigned int n = _q->header_user_len;

    // configure payload receiver
    if (_q->header_valid) {
        
        // re-compute payload encoded message length
        _q->payload_enc_len = 5;// <<====================++WRITE THIS 

        // re-allocate buffers accordingly
        _q->payload_enc = (unsigned char*) realloc(_q->payload_enc, _q->payload_enc_len*sizeof(unsigned char));
        _q->payload_dec = (unsigned char*) realloc(_q->payload_dec, _q->payload_dec_len*sizeof(unsigned char));
    }
    //
    return LIQUID_OK;
}

