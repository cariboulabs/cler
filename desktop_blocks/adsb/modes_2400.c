#include "modes_2400.h"
#include <string.h>
#include <stdlib.h>

// Phase correlation functions for 2.4 MSPS
// At 2.4MHz we have 6 samples per 5 symbols (each symbol is 500ns, each sample is 416.7ns)
static inline int slice_phase0(uint16_t *m) {
    return 5 * m[0] - 3 * m[1] - 2 * m[2];
}
static inline int slice_phase1(uint16_t *m) {
    return 4 * m[0] - m[1] - 3 * m[2];
}
static inline int slice_phase2(uint16_t *m) {
    return 3 * m[0] + m[1] - 4 * m[2];
}
static inline int slice_phase3(uint16_t *m) {
    return 2 * m[0] + 3 * m[1] - 5 * m[2];
}
static inline int slice_phase4(uint16_t *m) {
    return m[0] + 5 * m[1] - 5 * m[2] - m[3];
}

void mode_s_detect_2400(mode_s_t *self, uint16_t *mag, uint32_t maglen, mode_s_callback_t cb, void *cb_context) {
    uint32_t j;
    unsigned char msg1[14], msg2[14], *msg;
    unsigned char *bestmsg;
    int bestscore;

    msg = msg1;

    for (j = 0; j < maglen - 300; j++) {
        uint16_t *preamble = &mag[j];
        int high;
        uint32_t base_signal, base_noise;
        int try_phase;

        // Quick check: must have rising edge 0->1 and falling edge 12->13
        if (!(preamble[0] < preamble[1] && preamble[12] > preamble[13]))
            continue;

        // Detect phase based on preamble peak patterns
        if (preamble[1] > preamble[2] &&
            preamble[2] < preamble[3] && preamble[3] > preamble[4] &&
            preamble[8] < preamble[9] && preamble[9] > preamble[10] &&
            preamble[10] < preamble[11]) {
            // Phase 3: peaks at 1,3,9,11-12
            high = (preamble[1] + preamble[3] + preamble[9] + preamble[11] + preamble[12]) / 4;
            base_signal = preamble[1] + preamble[3] + preamble[9];
            base_noise = preamble[5] + preamble[6] + preamble[7];
        } else if (preamble[1] > preamble[2] &&
                   preamble[2] < preamble[3] && preamble[3] > preamble[4] &&
                   preamble[8] < preamble[9] && preamble[9] > preamble[10] &&
                   preamble[11] < preamble[12]) {
            // Phase 4: peaks at 1,3,9,12
            high = (preamble[1] + preamble[3] + preamble[9] + preamble[12]) / 4;
            base_signal = preamble[1] + preamble[3] + preamble[9] + preamble[12];
            base_noise = preamble[5] + preamble[6] + preamble[7] + preamble[8];
        } else if (preamble[1] > preamble[2] &&
                   preamble[2] < preamble[3] && preamble[4] > preamble[5] &&
                   preamble[8] < preamble[9] && preamble[10] > preamble[11] &&
                   preamble[11] < preamble[12]) {
            // Phase 5: peaks at 1,3-4,9-10,12
            high = (preamble[1] + preamble[3] + preamble[4] + preamble[9] + preamble[10] + preamble[12]) / 4;
            base_signal = preamble[1] + preamble[12];
            base_noise = preamble[6] + preamble[7];
        } else if (preamble[1] > preamble[2] &&
                   preamble[3] < preamble[4] && preamble[4] > preamble[5] &&
                   preamble[9] < preamble[10] && preamble[10] > preamble[11] &&
                   preamble[11] < preamble[12]) {
            // Phase 6: peaks at 1,4,10,12
            high = (preamble[1] + preamble[4] + preamble[10] + preamble[12]) / 4;
            base_signal = preamble[1] + preamble[4] + preamble[10] + preamble[12];
            base_noise = preamble[5] + preamble[6] + preamble[7] + preamble[8];
        } else if (preamble[2] > preamble[3] &&
                   preamble[3] < preamble[4] && preamble[4] > preamble[5] &&
                   preamble[9] < preamble[10] && preamble[10] > preamble[11] &&
                   preamble[11] < preamble[12]) {
            // Phase 7: peaks at 1-2,4,10,12
            high = (preamble[1] + preamble[2] + preamble[4] + preamble[10] + preamble[12]) / 4;
            base_signal = preamble[4] + preamble[10] + preamble[12];
            base_noise = preamble[6] + preamble[7] + preamble[8];
        } else {
            continue; // No suitable peaks
        }

        // Check SNR (~3.5 dB minimum)
        if (base_signal * 2 < 3 * base_noise)
            continue;

        // Check quiet bits
        if (preamble[5] >= high || preamble[6] >= high || preamble[7] >= high ||
            preamble[8] >= high || preamble[14] >= high || preamble[15] >= high ||
            preamble[16] >= high || preamble[17] >= high || preamble[18] >= high)
            continue;

        // Try all phases and find best
        bestmsg = NULL;
        bestscore = -2;

        for (try_phase = 4; try_phase <= 8; ++try_phase) {
            uint16_t *pPtr;
            int phase, i, bytelen;
            int errors = 0;

            pPtr = &mag[j + 19] + (try_phase / 5);
            phase = try_phase % 5;
            bytelen = 14; // MODE_S_LONG_MSG_BYTES

            for (i = 0; i < bytelen; ++i) {
                uint8_t theByte = 0;

                // Decode 8 bits using phase-specific correlations
                switch (phase) {
                case 0:
                    theByte =
                        (slice_phase0(pPtr) > 0 ? 0x80 : 0) |
                        (slice_phase2(pPtr + 2) > 0 ? 0x40 : 0) |
                        (slice_phase4(pPtr + 4) > 0 ? 0x20 : 0) |
                        (slice_phase1(pPtr + 7) > 0 ? 0x10 : 0) |
                        (slice_phase3(pPtr + 9) > 0 ? 0x08 : 0) |
                        (slice_phase0(pPtr + 12) > 0 ? 0x04 : 0) |
                        (slice_phase2(pPtr + 14) > 0 ? 0x02 : 0) |
                        (slice_phase4(pPtr + 16) > 0 ? 0x01 : 0);
                    phase = 1;
                    pPtr += 19;
                    break;
                case 1:
                    theByte =
                        (slice_phase1(pPtr) > 0 ? 0x80 : 0) |
                        (slice_phase3(pPtr + 2) > 0 ? 0x40 : 0) |
                        (slice_phase0(pPtr + 5) > 0 ? 0x20 : 0) |
                        (slice_phase2(pPtr + 7) > 0 ? 0x10 : 0) |
                        (slice_phase4(pPtr + 9) > 0 ? 0x08 : 0) |
                        (slice_phase1(pPtr + 12) > 0 ? 0x04 : 0) |
                        (slice_phase3(pPtr + 14) > 0 ? 0x02 : 0) |
                        (slice_phase0(pPtr + 17) > 0 ? 0x01 : 0);
                    phase = 2;
                    pPtr += 19;
                    break;
                case 2:
                    theByte =
                        (slice_phase2(pPtr) > 0 ? 0x80 : 0) |
                        (slice_phase4(pPtr + 2) > 0 ? 0x40 : 0) |
                        (slice_phase1(pPtr + 5) > 0 ? 0x20 : 0) |
                        (slice_phase3(pPtr + 7) > 0 ? 0x10 : 0) |
                        (slice_phase0(pPtr + 10) > 0 ? 0x08 : 0) |
                        (slice_phase2(pPtr + 12) > 0 ? 0x04 : 0) |
                        (slice_phase4(pPtr + 14) > 0 ? 0x02 : 0) |
                        (slice_phase1(pPtr + 17) > 0 ? 0x01 : 0);
                    phase = 3;
                    pPtr += 19;
                    break;
                case 3:
                    theByte =
                        (slice_phase3(pPtr) > 0 ? 0x80 : 0) |
                        (slice_phase0(pPtr + 3) > 0 ? 0x40 : 0) |
                        (slice_phase2(pPtr + 5) > 0 ? 0x20 : 0) |
                        (slice_phase4(pPtr + 7) > 0 ? 0x10 : 0) |
                        (slice_phase1(pPtr + 10) > 0 ? 0x08 : 0) |
                        (slice_phase3(pPtr + 12) > 0 ? 0x04 : 0) |
                        (slice_phase0(pPtr + 15) > 0 ? 0x02 : 0) |
                        (slice_phase2(pPtr + 17) > 0 ? 0x01 : 0);
                    phase = 4;
                    pPtr += 19;
                    break;
                case 4:
                    theByte =
                        (slice_phase4(pPtr) > 0 ? 0x80 : 0) |
                        (slice_phase1(pPtr + 3) > 0 ? 0x40 : 0) |
                        (slice_phase3(pPtr + 5) > 0 ? 0x20 : 0) |
                        (slice_phase0(pPtr + 8) > 0 ? 0x10 : 0) |
                        (slice_phase2(pPtr + 10) > 0 ? 0x08 : 0) |
                        (slice_phase4(pPtr + 12) > 0 ? 0x04 : 0) |
                        (slice_phase1(pPtr + 15) > 0 ? 0x02 : 0) |
                        (slice_phase3(pPtr + 17) > 0 ? 0x01 : 0);
                    phase = 0;
                    pPtr += 20;
                    break;
                }

                msg[i] = theByte;

                // Adjust message length based on DF
                if (i == 0) {
                    switch (msg[0] >> 3) {
                    case 0: case 4: case 5: case 11:
                        bytelen = 7; // Short message
                        break;
                    }
                }
            }

            // Decode and validate using existing modes.h function
            struct mode_s_msg mm;
            mode_s_decode(self, &mm, msg);

            if (mm.crcok && mm.errorbit == -1) {
                bestmsg = msg;
                bestscore = 1;
                msg = (msg == msg1) ? msg2 : msg1;
                break; // Found good message
            }
        }

        // If we found a valid message, call the callback
        if (bestscore > 0 && bestmsg) {
            struct mode_s_msg mm;
            mode_s_decode(self, &mm, bestmsg);
            if (mm.crcok) {
                cb(self, &mm, cb_context);
            }
        }
    }
}
