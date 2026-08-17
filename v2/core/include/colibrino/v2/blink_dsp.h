/*
 * Colibrino v2 - blink-dsp event contract.
 *
 * blink-dsp ends at impulse events: it emits IMPULSE (with duration) or
 * CANCEL (head motion / overlong hold / duration gate) or NONE - never a
 * click. The click authority is blink-code. Processing API arrives with the
 * blink-dsp implementation; this header carries the wire contract first so
 * the codec and the arbiter can be built and asserted against it.
 */
#ifndef COLIBRINO_V2_BLINK_DSP_H
#define COLIBRINO_V2_BLINK_DSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "colibrino/v2/common.h"
#include "colibrino/v2/feel_defaults.h"

#ifdef __cplusplus
extern "C" {
#endif

enum cv2_blink_kind {
  CV2_BLINK_NONE = 0,
  CV2_BLINK_IMPULSE = 1,
  CV2_BLINK_CANCEL = 2,
  CV2_BLINK_KIND_MAX = CV2_BLINK_CANCEL
};

/* Why a CANCEL was emitted (blink event .reason). */
enum cv2_blink_cancel_reason {
  CV2_BLINK_REASON_NONE = 0,
  CV2_BLINK_REASON_HEAD_MOTION = 1, /* |gyro| above the head gate */
  CV2_BLINK_REASON_OVERLONG = 2,    /* held above exit longer than maximum */
  CV2_BLINK_REASON_DURATION = 3     /* closed outside [minimum, maximum] */
};

/* 24-byte blink event: header + 8-byte body. */
typedef struct {
  cv2_event_hdr_t hdr;  /* producer CV2_PRODUCER_BLINK_IMU */
  uint16_t ttl_ms;
  uint8_t confidence;
  uint8_t reason;       /* enum cv2_blink_cancel_reason, 0 for IMPULSE */
  uint16_t duration_ms; /* IMPULSE: time above exit threshold */
  uint16_t reserved;
} cv2_blink_event_t;

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_BLINK_DSP_H */
