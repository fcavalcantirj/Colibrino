/*
 * Colibrino v2 - blink-code: the coded click candidate.
 *
 * Consumes blink-dsp events and emits CLICK_CANDIDATE only for the
 * production temporal code: double (300-700 ms) . deliberate pause
 * (800-1400 ms) . double, followed by a 1500 ms click refractory. Reproduces
 * the v1 rule "expected pause but short gap -> keep the newest pair as a
 * possible first double"; any CANCEL resets the partial sequence.
 *
 * The click refractory is owned here (config) and enforced twice: the code
 * stage ignores impulses inside it, and the pipeline asks blink-dsp to hold
 * so those impulses are not even counted (v1 parity).
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

#define CV2_BLINK_CODE_PATTERN_IMPULSES 4u

typedef struct {
  uint8_t sequence_impulses;      /* impulses accepted into the pattern */
  cv2_ms_t previous_impulse_ms;   /* last impulse in the pattern */
  cv2_ms_t first_impulse_ms;      /* first impulse in the pattern */
  bool click_suppressed;
  cv2_ms_t last_click_ms;
  uint32_t completed_sequences;
  uint32_t next_seq;              /* seq of the next emitted gesture */
} cv2_blink_code_state_t;

void cv2_blink_code_config_defaults(cv2_blink_code_config_t *cfg);
void cv2_blink_code_reset(cv2_blink_code_state_t *st);
/* One blink event (NONE / IMPULSE / CANCEL) -> NONE / CLICK_CANDIDATE.
 * Malformed input events are ignored (fail closed). */
cv2_gesture_event_t cv2_blink_code_step(cv2_blink_code_state_t *st,
                                        const cv2_blink_event_t *in,
                                        const cv2_blink_code_config_t *cfg);
/* True while impulses are being ignored after a click (informational). */
bool cv2_blink_code_in_refractory(const cv2_blink_code_state_t *st,
                                  const cv2_blink_code_config_t *cfg,
                                  cv2_ms_t t_ms);

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_BLINK_CODE_H */
