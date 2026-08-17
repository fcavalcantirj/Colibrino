/*
 * Colibrino v2 - blink pipeline facade: blink-dsp -> blink-code.
 *
 * Composes the two pure units per IMU sample and wires the click refractory
 * back into the dsp hold so the whole pipeline reproduces the sticks3
 * ImuBlinkDetector sample for sample (click decision, accepted impulses,
 * completed sequences). blink_detect() is the SPEC facade; the canonical use
 * is per-sample streaming (n = 1).
 */
#ifndef COLIBRINO_V2_BLINK_PIPELINE_H
#define COLIBRINO_V2_BLINK_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "colibrino/v2/blink_code.h"
#include "colibrino/v2/blink_dsp.h"
#include "colibrino/v2/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  cv2_blink_dsp_config_t dsp_cfg;
  cv2_blink_code_config_t code_cfg;
  cv2_blink_dsp_state_t dsp;
  cv2_blink_code_state_t code;
  uint32_t impulses; /* accepted impulses since reset (== v1 acceptedImpulses) */
  uint32_t clicks;   /* click candidates since reset (== v1 completedSequences) */
} cv2_blink_pipeline_state_t;

/* NULL config = feel defaults. */
void cv2_blink_pipeline_init(cv2_blink_pipeline_state_t *st,
                             const cv2_blink_dsp_config_t *dsp_cfg,
                             const cv2_blink_code_config_t *code_cfg);
/* Clears state and counters, keeps the configs. */
void cv2_blink_pipeline_reset(cv2_blink_pipeline_state_t *st);
/* One sample. blink_out (nullable) receives the dsp event of this sample. */
cv2_gesture_event_t cv2_blink_pipeline_step(cv2_blink_pipeline_state_t *st,
                                            const cv2_imu_sample_t *sample,
                                            cv2_blink_event_t *blink_out);
/* SPEC facade: streams the window and returns the last non-NONE gesture
 * event (kind NONE when the window produced none). */
cv2_gesture_event_t blink_detect(const cv2_imu_sample_t *window, size_t n,
                                 cv2_blink_pipeline_state_t *st);

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_BLINK_PIPELINE_H */
