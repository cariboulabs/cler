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

// ezgmsk_demod.c

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include <assert.h>
#include <stdbool.h>

#include "_ezgmsk_demod.h"
#include "liquid.internal.h"

// enable pre-demodulation filter (remove out-of-band noise)
#define EZGMSK_DEMOD_PREFILTER         (0)

// execute a single, post-filtered sample
int ezgmsk_demod_execute_sample(ezgmsk_demod _q,
                                 float complex _x);

// push buffered p/n sequence through synchronizer
int ezgmsk_demod_finalize_preamble_detection(ezgmsk_demod _q);

// ...
int ezgmsk_demod_syncpn(ezgmsk_demod _q);

// update instantaneous frequency estimate
int ezgmsk_demod_update_fi(ezgmsk_demod _q,
                            float complex _x);

// update symbol synchronizer internal state (filtered error, index, etc.)
//  _q      :   frame synchronizer
//  _x      :   input sample
//  _y      :   output symbol
int ezgmsk_demod_update_symsync(ezgmsk_demod _q,
                                float         _x,
                                float *       _y);

// execute stages
int ezgmsk_demod_execute_detectframe   (ezgmsk_demod _q, float complex _x);
int ezgmsk_demod_execute_rxsyncword    (ezgmsk_demod _q, float complex _x);
int ezgmsk_demod_execute_rxheader      (ezgmsk_demod _q, float complex _x);
int ezgmsk_demod_execute_rxpayload     (ezgmsk_demod _q, float complex _x);

// ezgmsk_demod object structure
struct ezgmsk_demod_s {
#if EZGMSK_DEMOD_PREFILTER
    iirfilt_crcf prefilter;         // pre-demodulation filter
#endif
    unsigned int k;                 // filter samples/symbol
    unsigned int m;                 // filter semi-length (symbols)
    float BT;                       // filter bandwidth-time product
    ezgmsk_demod_callback callback;    // user-defined callback function
    void * userdata;                // user-defined data structure

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
    size_t syncword_lookup_symbols_len;
    unsigned char * syncword_symbols_expected;  
    unsigned char * syncword_symbols_est; //has double the length of expected so we can decode easier

    // header
    unsigned int header_bytes_len;     // length of header (bytes)
    unsigned char * header_symbols;
    unsigned char * header_bytes;

    // payload
    unsigned int payload_max_bytes_len; // max length of payload (bytes)
    unsigned int payload_bytes_len; // length of payload (bytes)
    unsigned char * payload_symbols; // payload symbols (received)
    unsigned char * payload_bytes;  // payload bytes
    
    // status variables
    ezgmsk_demod_state_en state;
    unsigned int sample_counter;    //counter: num of samples received in general
    unsigned int preamble_counter;  // counter: num of p/n syms received
    unsigned int syncword_counter; // counter: num of syncword syms received
    unsigned int header_counter;    // counter: num of header syms received
    unsigned int payload_counter;   // counter: num of payload syms received

    float rssi_db;                  // received signal strength indicator (dB)
    float snr_db;                   // signal-to-noise ratio (dB)
};

// create GMSK frame synchronizer
//  _k          :   samples/symbol
//  _m          :   filter delay (symbols)
//  _BT         :   excess bandwidth factor
//  _callback   :   callback function
//  _userdata   :   user data pointer passed to callback function
ezgmsk_demod ezgmsk_demod_create_set(unsigned int          _k,
                                        unsigned int             _m,
                                        float                    _BT,
                                        unsigned int             _preamble_symbols_len,
                                        const unsigned char*     _syncword_symbols,
                                        unsigned int             _syncword_symbols_len,
                                        unsigned int             _header_byte_len,
                                        unsigned int             _payload_max_bytes_len,
                                        float                    _detector_threshold,
                                        float                    _detector_dphi_max,
                                        ezgmsk_demod_callback    _callback,
                                        void *                   _userdata)
{
    assert(_callback != NULL);

    ezgmsk_demod q = (ezgmsk_demod) malloc(sizeof(struct ezgmsk_demod_s));
    q->sample_counter = 0;
    q->callback = _callback;
    q->userdata = _userdata;
    q->k        = _k;        // samples/symbol
    q->m        = _m;        // filter delay (symbols)
    q->BT       = _BT;      // filter bandwidth-time product

#if EZGMSK_DEMOD_PREFILTER
    // create default low-pass Butterworth filter
    q->prefilter = iirfilt_crcf_create_lowpass(3, 0.5f*(1 + q->BT) / (float)(q->k));
#endif

    unsigned int i;

    // frame detector
    q->preamble_len = _preamble_symbols_len;
    q->preamble_pn = (float*)malloc(q->preamble_len*sizeof(float));
    q->preamble_rx = (float*)malloc(q->preamble_len*sizeof(float));
    float complex preamble_samples[q->preamble_len*q->k];

    gmskmod mod = gmskmod_create(q->k, q->m, q->BT);

    unsigned int sample_idx = 0;
    for (i = 0; i < q->preamble_len + q->m; i++) {
        unsigned int bit = i % 2;
        liquid_float_complex samples[q->k];  // buffer for k samples
        gmskmod_modulate(mod, bit, samples);
        if (i >= q->m) {
            // Store these k samples in the output
            for (unsigned int j = 0; j < q->k; j++) {
                preamble_samples[sample_idx++] = samples[j];
            }
        }
    }

    gmskmod_destroy(mod);
    q->frame_detector = detector_cccf_create(preamble_samples, q->preamble_len*q->k,
        _detector_threshold, _detector_dphi_max);
    q->buffer = windowcf_create(q->k*(q->preamble_len+q->m));

    // create symbol timing recovery filters
    q->npfb = 32;   // number of filters in the bank
    q->mf   = firpfb_rrrf_create_rnyquist( LIQUID_FIRFILT_GMSKRX,q->npfb,q->k,q->m,q->BT);
    q->dmf  = firpfb_rrrf_create_drnyquist(LIQUID_FIRFILT_GMSKRX,q->npfb,q->k,q->m,q->BT);

    // create down-coverters for carrier phase tracking
    q->nco_coarse = nco_crcf_create(LIQUID_NCO);

    //create/allocate syncword objects/arrays
    q->syncword_lookup_symbols_len = 2*_syncword_symbols_len + _preamble_symbols_len;
    q->syncword_symbols_len = _syncword_symbols_len;
    q->syncword_symbols_expected = (unsigned char*) malloc(q->syncword_lookup_symbols_len*sizeof(unsigned char));
    q->syncword_symbols_est = (unsigned char*) malloc(q->syncword_symbols_len*sizeof(unsigned char));
    memcpy(q->syncword_symbols_expected, _syncword_symbols, q->syncword_symbols_len*sizeof(unsigned char));

    // create/allocate header objects/arrays
    q->header_bytes_len = _header_byte_len;
    q->header_symbols = (unsigned char*) malloc(q->header_bytes_len*8*sizeof(unsigned char));
    q->header_bytes = (unsigned char*) malloc(q->header_bytes_len*sizeof(unsigned char));

    // create/allocate payload objects/arrays
    q->payload_max_bytes_len = _payload_max_bytes_len;
    q->payload_bytes_len = 0; // payload length is set later
    q->payload_symbols = (unsigned char*) malloc(q->payload_max_bytes_len*8*sizeof(unsigned char));
    q->payload_bytes = (unsigned char*) malloc(q->payload_max_bytes_len*sizeof(unsigned char));

    // reset synchronizer
    ezgmsk_demod_reset(q);

    // return synchronizer object
    return q;
}

// destroy frame synchronizer object, freeing all internal memory
int ezgmsk_demod_destroy(ezgmsk_demod _q)
{
    // destroy synchronizer objects
#if EZGMSK_DEMOD_PREFILTER
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
    free(_q->header_symbols);
    free(_q->header_bytes);

    // payload
    free(_q->payload_symbols);
    free(_q->payload_bytes);

    // free main object memory
    free(_q);
    return LIQUID_OK;
}

// print frame synchronizer object internals
int ezgmsk_demod_print(ezgmsk_demod _q)
{
    (void)_q;
    printf("<liquid.ezgmsk_demod>\n");
    return LIQUID_OK;
}

// reset frame synchronizer object
int ezgmsk_demod_reset(ezgmsk_demod _q)
{
    // reset state and counters
    _q->state = EZGMSK_DEMOD_STATE_DETECTFRAME;
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

    //reset quality measures
    _q->rssi_db = 0.0f;
    _q->snr_db  = 0.0f;

    //reset payload data
    _q->payload_bytes_len = 0;
    
    // reset symbol timing recovery state
    firpfb_rrrf_reset(_q->mf);
    firpfb_rrrf_reset(_q->dmf);
    _q->pfb_q = 0.0f;   // filtered error signal
    return LIQUID_OK;
}

int ezgmsk_demod_is_frame_open(ezgmsk_demod _q)
{
    return (_q->state == EZGMSK_DEMOD_STATE_DETECTFRAME) ? 0 : 1;
}

int ezgmsk_demod_execute_sample(ezgmsk_demod _q,
                                 float complex _x)
{
    switch (_q->state) {
    case EZGMSK_DEMOD_STATE_DETECTFRAME: return ezgmsk_demod_execute_detectframe  (_q, _x);
    case EZGMSK_DEMOD_STATE_RXSYNCWORD:  return ezgmsk_demod_execute_rxsyncword   (_q, _x);
    case EZGMSK_DEMOD_STATE_RXHEADER:    return ezgmsk_demod_execute_rxheader     (_q, _x);
    case EZGMSK_DEMOD_STATE_RXPAYLOAD:   return ezgmsk_demod_execute_rxpayload    (_q, _x);
    default:;
    }

    return liquid_error(LIQUID_EINT,"ezgmsk_demod_execute_sample(), invalid internal state");
}

// execute frame synchronizer
//  _q      :   frame synchronizer object
//  _x      :   input sample array [size: _n x 1]
//  _n      :   number of input samples
int ezgmsk_demod_execute(ezgmsk_demod   _q,
                          float complex * _x,
                          unsigned int    _n)
{
    // push through synchronizer
    unsigned int i;
    for (i=0; i<_n; i++) {
        float complex xf;   // input sample
#if EZGMSK_DEMOD_PREFILTER
        iirfilt_crcf_execute(_q->prefilter, _x[i], &xf);
#else
        xf = _x[i];
#endif
        _q->sample_counter++;
        ezgmsk_demod_execute_sample(_q, xf);
    }
    return LIQUID_OK;
}

// internal methods
//

// update symbol synchronizer internal state (filtered error, index, etc.)
//  _q      :   frame synchronizer
//  _x      :   input sample
//  _y      :   output symbol
int ezgmsk_demod_update_symsync(ezgmsk_demod _q,
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
        while ((unsigned int)_q->pfb_index > _q->npfb-1) {
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

int ezgmsk_demod_finalize_preamble_detection(ezgmsk_demod _q)
{
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
    for (unsigned int i=0; i<delay; i++) {
        float complex y;
        nco_crcf_mix_down(_q->nco_coarse, rc[i], &y);
        nco_crcf_step(_q->nco_coarse);

        // update instantanenous frequency estimate
        ezgmsk_demod_update_fi(_q, y);

        // push initial samples into filterbanks
        firpfb_rrrf_push(_q->mf,  _q->fi_hat);
        firpfb_rrrf_push(_q->dmf, _q->fi_hat);
    }

    // set state (still need a few more samples before entire p/n
    // sequence has been received)
    _q->state = EZGMSK_DEMOD_STATE_RXSYNCWORD;
    for (unsigned int i=delay; i<buffer_len; i++) {
        //rerun entire preamble syncword to ensure that we are not missing any samples there
        ezgmsk_demod_execute_sample(_q, rc[i]);
    }
    return LIQUID_OK;
}

// update instantaneous frequency estimate
int ezgmsk_demod_update_fi(ezgmsk_demod _q,
                            float complex _x)
{
    // compute differential phase
    _q->fi_hat = cargf(conjf(_q->x_prime)*_x) * _q->k;

    // update internal state
    _q->x_prime = _x;
    return LIQUID_OK;
}

int ezgmsk_demod_execute_detectframe(ezgmsk_demod _q,
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
        _q->rssi_db = 10.0*log10f(_q->gamma_hat);
        _q->snr_db = 10.0*log10f(_q->gamma_hat / (1.0f - _q->gamma_hat));
        _q->callback(_q->sample_counter,
                        _q->state,
                        NULL,
                        NULL,
                        0,
                        _q->rssi_db,
                        _q->snr_db,
                        _q->userdata);
        ezgmsk_demod_finalize_preamble_detection(_q);
    }
    return LIQUID_OK;
}

int ezgmsk_demod_execute_rxsyncword(ezgmsk_demod _q,
                                   float complex _x)
{
    // mix signal down
    float complex y;
    nco_crcf_mix_down(_q->nco_coarse, _x, &y);
    nco_crcf_step(_q->nco_coarse);

    // update instantanenous frequency estimate
    ezgmsk_demod_update_fi(_q, y);

    // update symbol synchronizer
    float mf_out = 0.0f;
    int sample_available = ezgmsk_demod_update_symsync(_q, _q->fi_hat, &mf_out);

    // compute output if timeout
    if (sample_available) {
        // demodulate
        unsigned char s = mf_out > 0.0f ? 1 : 0;
        _q->syncword_symbols_est[_q->syncword_counter] = s;

        _q->syncword_counter++;
        if (_q->syncword_counter >= _q->syncword_symbols_len) {
            bool detected = true;
            unsigned int start = _q->syncword_counter - _q->syncword_symbols_len;
            for (unsigned int i = 0; i<_q->syncword_symbols_len; i++) 
            {
                if (_q->syncword_symbols_est[start + i] != _q->syncword_symbols_expected[i]) {
                    detected = false;
                    break;
                }
            }
            if (detected) {
                _q->callback(_q->sample_counter,
                _q->state,
                NULL,
                NULL,
                0,
                _q->rssi_db,
                _q->snr_db,
                _q->userdata);
                _q->state = EZGMSK_DEMOD_STATE_RXHEADER;
            } else if (_q->syncword_counter > _q->syncword_lookup_symbols_len) {
                ezgmsk_demod_reset(_q);
            }
        }
    }
    return LIQUID_OK;
}

int ezgmsk_demod_execute_rxheader(ezgmsk_demod _q,
                                   float complex _x)
{
    // mix signal down
    float complex y;
    nco_crcf_mix_down(_q->nco_coarse, _x, &y);
    nco_crcf_step(_q->nco_coarse);

    // update instantanenous frequency estimate
    ezgmsk_demod_update_fi(_q, y);

    // update symbol synchronizer
    float mf_out = 0.0f;
    int sample_available = ezgmsk_demod_update_symsync(_q, _q->fi_hat, &mf_out);

    if (sample_available) {
        unsigned char s = mf_out > 0.0f ? 1 : 0;

        _q->header_symbols[_q->header_counter] = s;
        _q->header_counter++;

        if (_q->header_counter == _q->header_bytes_len * 8) {
            unsigned int num_written = 0;
            liquid_pack_bytes(_q->header_symbols,
                            _q->header_bytes_len * 8,
                            _q->header_bytes,
                            _q->header_bytes_len,
                            &num_written);
            assert(num_written == _q->header_bytes_len);
            int payload_len = _q->callback(_q->sample_counter,
                        _q->state,
                        _q->header_bytes,
                        NULL,
                        0,
                        _q->rssi_db,
                        _q->snr_db,
                        _q->userdata);
            if (payload_len <= 0 || (unsigned int)payload_len > _q->payload_max_bytes_len) {
                return ezgmsk_demod_reset(_q);
            } else {
                _q->payload_bytes_len = payload_len;
                _q->state = EZGMSK_DEMOD_STATE_RXPAYLOAD;
            }
        }
    }
    return LIQUID_OK;    
}

int ezgmsk_demod_execute_rxpayload(ezgmsk_demod _q,
                                    float complex _x)
{
    // mix signal down
    float complex y;
    nco_crcf_mix_down(_q->nco_coarse, _x, &y);
    nco_crcf_step(_q->nco_coarse);

    // update instantanenous frequency estimate
    ezgmsk_demod_update_fi(_q, y);

    // update symbol synchronizer
    float mf_out = 0.0f;
    int sample_available = ezgmsk_demod_update_symsync(_q, _q->fi_hat, &mf_out);

    // compute output if timeout
    if (sample_available) {
        // demodulate
        unsigned char s = mf_out > 0.0f ? 1 : 0;

        _q->payload_symbols[_q->payload_counter] = s;
        _q->payload_counter++;

        if (_q->payload_counter == _q->payload_bytes_len * 8) { //symbol len
            unsigned int num_written = 0;
            liquid_pack_bytes(_q->payload_symbols,
                            _q->payload_counter,
                            _q->payload_bytes,
                            _q->payload_bytes_len,
                            &num_written);
            assert(num_written == _q->payload_bytes_len);
            // invoke callback method
            _q->callback(
                        _q->sample_counter,
                        _q->state,
                        _q->header_bytes,
                        _q->payload_bytes,
                        _q->payload_bytes_len,
                        _q->rssi_db,
                        _q->snr_db,
                        _q->userdata);
        
            ezgmsk_demod_reset(_q);
        }
    }

    return LIQUID_OK;
}