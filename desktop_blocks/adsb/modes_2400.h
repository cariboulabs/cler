#ifndef __MODE_S_2400_H
#define __MODE_S_2400_H

#include "modes.h"

#ifdef __cplusplus
extern "C" {
#endif

// 2.4 MSPS Mode S detector
// Uses the same callback interface as modes.h but with 2.4MHz sampling
void mode_s_detect_2400(mode_s_t *self, uint16_t *mag, uint32_t maglen, mode_s_callback_t cb, void *cb_context);

#ifdef __cplusplus
}
#endif

#endif
