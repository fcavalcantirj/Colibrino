/*
 * blink pipeline facade: dsp -> code, with the click refractory wired back
 * into the dsp hold. Sample for sample this reproduces the sticks3
 * ImuBlinkDetector (differential test: test/blink_differential.cpp).
 */
#include "colibrino/v2/blink_pipeline.h"

void cv2_blink_pipeline_init(cv2_blink_pipeline_state_t *st,
                             const cv2_blink_dsp_config_t *dsp_cfg,
                             const cv2_blink_code_config_t *code_cfg) {
  if (st == NULL) {
    return;
  }
  if (dsp_cfg != NULL) {
    st->dsp_cfg = *dsp_cfg;
  } else {
    cv2_blink_dsp_config_defaults(&st->dsp_cfg);
  }
  if (code_cfg != NULL) {
    st->code_cfg = *code_cfg;
  } else {
    cv2_blink_code_config_defaults(&st->code_cfg);
  }
  cv2_blink_pipeline_reset(st);
}

void cv2_blink_pipeline_reset(cv2_blink_pipeline_state_t *st) {
  if (st == NULL) {
    return;
  }
  cv2_blink_dsp_reset(&st->dsp);
  cv2_blink_code_reset(&st->code);
  st->impulses = 0u;
  st->clicks = 0u;
}

static cv2_gesture_event_t none_gesture(void) {
  cv2_gesture_event_t ev;
  ev.hdr.magic = 0u;
  ev.hdr.version = 0u;
  ev.hdr.kind = (uint8_t)CV2_GESTURE_NONE;
  ev.hdr.producer_id = 0u;
  ev.hdr.size = 0u;
  ev.hdr.seq = 0u;
  ev.hdr.t_ms = 0u;
  ev.ttl_ms = 0u;
  ev.confidence = 0u;
  ev.impulses = 0u;
  ev.span_ms = 0u;
  ev.reserved = 0u;
  return ev;
}

cv2_gesture_event_t cv2_blink_pipeline_step(cv2_blink_pipeline_state_t *st,
                                            const cv2_imu_sample_t *sample,
                                            cv2_blink_event_t *blink_out) {
  if (st == NULL || sample == NULL) {
    return none_gesture();
  }
  const cv2_blink_event_t blink = cv2_blink_dsp_update(
      &st->dsp, &st->dsp_cfg, sample->t_ms, sample->gyro_dps);
  if (blink_out != NULL) {
    *blink_out = blink;
  }
  const cv2_gesture_event_t gesture =
      cv2_blink_code_step(&st->code, &blink, &st->code_cfg);
  if (gesture.hdr.kind == (uint8_t)CV2_GESTURE_CLICK_CANDIDATE) {
    /* v1 parity: while the click refractory runs, no impulse is detected
     * or counted at all. */
    cv2_blink_dsp_hold(&st->dsp, sample->t_ms, st->code_cfg.click_refractory_ms);
  }
  st->impulses = st->dsp.impulse.accepted_impulses;
  st->clicks = st->code.completed_sequences;
  return gesture;
}

cv2_gesture_event_t blink_detect(const cv2_imu_sample_t *window, size_t n,
                                 cv2_blink_pipeline_state_t *st) {
  cv2_gesture_event_t last = none_gesture();
  if (window == NULL || st == NULL) {
    return last;
  }
  for (size_t i = 0; i < n; ++i) {
    const cv2_gesture_event_t g = cv2_blink_pipeline_step(st, &window[i], NULL);
    if (g.hdr.kind != (uint8_t)CV2_GESTURE_NONE) {
      last = g;
    }
  }
  return last;
}
