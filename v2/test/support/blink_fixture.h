/*
 * Blink test helpers: synthetic channel outputs for stage-1 tests, the
 * sticks3 synthetic still/impulse feeders for pipeline-level tests, and
 * hand-built blink events for blink-code tests.
 */
#ifndef COLIBRINO_V2_TEST_BLINK_FIXTURE_H
#define COLIBRINO_V2_TEST_BLINK_FIXTURE_H

#include <string.h>

#include "colibrino/v2/blink_code.h"
#include "colibrino/v2/blink_dsp.h"
#include "colibrino/v2/blink_pipeline.h"

static inline cv2_blink_channel_out_t ch_out(float residual, float head) {
  cv2_blink_channel_out_t o;
  o.valid = true;
  o.residual_dps = residual;
  o.head_rate_dps = head;
  return o;
}

static inline cv2_blink_channel_out_t ch_seed(void) {
  cv2_blink_channel_out_t o;
  o.valid = false;
  o.residual_dps = 0.0f;
  o.head_rate_dps = 0.0f;
  return o;
}

/* Feeds stage 1 with a constant channel output at 5 ms cadence for
 * duration_ms starting at *now_ms; counts IMPULSE / CANCEL(reason) events. */
typedef struct {
  uint32_t impulses;
  uint32_t cancels[8];
  cv2_blink_event_t last_impulse;
  cv2_blink_event_t last_cancel;
} blink_tally_t;

static inline void tally_reset(blink_tally_t *t) { memset(t, 0, sizeof *t); }

static inline void tally_add(blink_tally_t *t, const cv2_blink_event_t *ev) {
  if (ev->hdr.kind == (uint8_t)CV2_BLINK_IMPULSE) {
    ++t->impulses;
    t->last_impulse = *ev;
  } else if (ev->hdr.kind == (uint8_t)CV2_BLINK_CANCEL) {
    ++t->cancels[ev->reason & 7u];
    t->last_cancel = *ev;
  }
}

static inline void feed_stage1(cv2_blink_impulse_state_t *st,
                               const cv2_blink_dsp_config_t *cfg,
                               uint32_t *now_ms, uint32_t duration_ms,
                               float residual, float head, blink_tally_t *t) {
  const cv2_blink_channel_out_t in = ch_out(residual, head);
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += 5u) {
    const cv2_blink_event_t ev = cv2_blink_dsp_step(st, cfg, *now_ms, &in);
    if (t != NULL) {
      tally_add(t, &ev);
    }
    *now_ms += 5u;
  }
}

/* sticks3 synthetic feeders (mirrored from test_imu_blink_detector). */
static inline void feed_still(cv2_blink_pipeline_state_t *p, uint32_t *now_ms,
                              uint32_t duration_ms, blink_tally_t *t,
                              uint32_t *clicks) {
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += 5u) {
    const float noise = (elapsed % 20u == 0u) ? 0.12f : -0.06f;
    cv2_imu_sample_t s;
    memset(&s, 0, sizeof s);
    s.t_ms = *now_ms;
    s.gyro_dps[0] = noise;
    s.gyro_dps[1] = -noise;
    cv2_blink_event_t b;
    const cv2_gesture_event_t g = cv2_blink_pipeline_step(p, &s, &b);
    if (t != NULL) {
      tally_add(t, &b);
    }
    if (clicks != NULL && g.hdr.kind == (uint8_t)CV2_GESTURE_CLICK_CANDIDATE) {
      ++*clicks;
    }
    *now_ms += 5u;
  }
}

static inline void feed_impulse(cv2_blink_pipeline_state_t *p,
                                uint32_t *now_ms, blink_tally_t *t,
                                uint32_t *clicks) {
  for (int index = 0; index < 12; ++index) {
    const float value = index < 4 ? -1.35f : (index < 8 ? 0.95f : 0.10f);
    cv2_imu_sample_t s;
    memset(&s, 0, sizeof s);
    s.t_ms = *now_ms;
    s.gyro_dps[0] = value;
    s.gyro_dps[1] = 0.2f * value;
    s.gyro_dps[2] = -0.1f * value;
    cv2_blink_event_t b;
    const cv2_gesture_event_t g = cv2_blink_pipeline_step(p, &s, &b);
    if (t != NULL) {
      tally_add(t, &b);
    }
    if (clicks != NULL && g.hdr.kind == (uint8_t)CV2_GESTURE_CLICK_CANDIDATE) {
      ++*clicks;
    }
    *now_ms += 5u;
  }
}

/* Hand-built blink events for blink-code tests. */
static inline cv2_blink_event_t blink_impulse_at(uint32_t seq, cv2_ms_t t_ms,
                                                 uint16_t duration_ms) {
  cv2_blink_event_t ev;
  memset(&ev, 0, sizeof ev);
  ev.hdr.magic = CV2_EVENT_MAGIC;
  ev.hdr.version = CV2_EVENT_VERSION;
  ev.hdr.kind = CV2_BLINK_IMPULSE;
  ev.hdr.producer_id = CV2_PRODUCER_BLINK_IMU;
  ev.hdr.size = CV2_EVENT_SIZE;
  ev.hdr.seq = seq;
  ev.hdr.t_ms = t_ms;
  ev.ttl_ms = (uint16_t)CV2_FEEL_TTL_IMPULSE_MS;
  ev.confidence = (uint8_t)CV2_FEEL_BLINK_IMPULSE_CONFIDENCE;
  ev.duration_ms = duration_ms;
  return ev;
}

static inline cv2_blink_event_t blink_cancel_at(uint32_t seq, cv2_ms_t t_ms,
                                                uint8_t reason) {
  cv2_blink_event_t ev = blink_impulse_at(seq, t_ms, 0u);
  ev.hdr.kind = CV2_BLINK_CANCEL;
  ev.reason = reason;
  return ev;
}

#endif /* COLIBRINO_V2_TEST_BLINK_FIXTURE_H */
