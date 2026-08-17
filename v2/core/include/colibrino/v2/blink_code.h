/*
 * Colibrino v2 - blink-code event contract.
 *
 * blink-code consumes blink-dsp events and emits CLICK_CANDIDATE only for the
 * production temporal code: double (300-700 ms) . deliberate pause
 * (800-1400 ms) . double, followed by a click refractory. Processing API
 * arrives with the blink-code implementation; the wire contract comes first.
 */
#ifndef COLIBRINO_V2_BLINK_CODE_H
#define COLIBRINO_V2_BLINK_CODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "colibrino/v2/blink_dsp.h"
#include "colibrino/v2/common.h"
#include "colibrino/v2/feel_defaults.h"

#ifdef __cplusplus
extern "C" {
#endif

enum cv2_gesture_kind {
  CV2_GESTURE_NONE = 0,
  CV2_GESTURE_CLICK_CANDIDATE = 1,
  CV2_GESTURE_KIND_MAX = CV2_GESTURE_CLICK_CANDIDATE
};

/* 24-byte gesture event: header + 8-byte body. */
typedef struct {
  cv2_event_hdr_t hdr; /* producer CV2_PRODUCER_BLINK_CODE */
  uint16_t ttl_ms;
  uint8_t confidence;
  uint8_t impulses; /* impulses that formed the candidate (4) */
  uint16_t span_ms; /* first impulse to last impulse */
  uint16_t reserved;
} cv2_gesture_event_t;

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_BLINK_CODE_H */
