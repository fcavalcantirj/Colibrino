/*
 * Colibrino v2 - blink-dsp: IMU residual channel + impulse detector.
 *
 * blink-dsp ends at impulse events: it emits IMPULSE (with duration) or
 * CANCEL (head motion / overlong hold / duration gate / quiet gate at reset)
 * or NONE - never a click. The click authority is blink-code.
 *
 * Two pure stages with explicit state, mirroring the field-proven sticks3
 * ImuBlinkDetector sample for sample:
 *   stage 0  cv2_blink_channel_imu_update  per-axis EMA baseline (tau 350 ms,
 *            dt clamped to 1..50 ms), residual |gyro - baseline| and head
 *            rate |gyro|; the first sample seeds the baseline (valid = false)
 *   stage 1  cv2_blink_dsp_step  head gate (2.5 dps) + 2 s quiet gate,
 *            optional external hold, 300 ms impulse refractory, enter 1.1 /
 *            exit 0.6 dps, 20..300 ms duration gate
 * Stage 0 is input-source specific (BMI270 today); stage 1 is not - an IR /
 * proximity channel can replace stage 0 and keep stage 1 untouched.
 *
 * Pure: no globals, no clock reads (t_ms is a parameter), no allocation.
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

/* Why a CANCEL was emitted (blink event .reason). Any CANCEL resets a
 * partial blink-code sequence. */
enum cv2_blink_cancel_reason {
  CV2_BLINK_REASON_NONE = 0,
  CV2_BLINK_REASON_HEAD_MOTION = 1, /* |gyro| above the head gate */
  CV2_BLINK_REASON_OVERLONG = 2,    /* held above exit longer than maximum */
  CV2_BLINK_REASON_DURATION = 3,    /* closed outside [minimum, maximum] */
  CV2_BLINK_REASON_QUIET_GATE = 4   /* first sample after reset opened the
                                       2 s quiet gate */
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

/* ---- stage 0: IMU residual channel ------------------------------------- */

typedef struct {
  bool initialized;
  float baseline_dps[3];
  cv2_ms_t previous_ms;
} cv2_blink_channel_imu_state_t;

typedef struct {
  bool valid;          /* false for the seed sample (baseline just set) */
  float residual_dps;  /* |gyro - baseline| */
  float head_rate_dps; /* |gyro| */
} cv2_blink_channel_out_t;

void cv2_blink_channel_imu_reset(cv2_blink_channel_imu_state_t *st);
cv2_blink_channel_out_t cv2_blink_channel_imu_update(
    cv2_blink_channel_imu_state_t *st, const cv2_blink_dsp_config_t *cfg,
    cv2_ms_t t_ms, const float gyro_dps[3]);

/* ---- stage 1: impulse detector ------------------------------------------ */

typedef struct {
  bool impulse_active;
  cv2_ms_t impulse_started_ms;
  bool have_last_impulse;
  cv2_ms_t last_impulse_ms;
  bool head_suppressed;
  cv2_ms_t last_head_motion_ms;
  bool hold_active;         /* external hold requested by the consumer */
  cv2_ms_t hold_started_ms;
  uint32_t hold_ms;
  uint32_t accepted_impulses;
  uint32_t next_seq;        /* seq of the next emitted event */
} cv2_blink_impulse_state_t;

void cv2_blink_impulse_reset(cv2_blink_impulse_state_t *st);
/* Ignore impulses for hold_ms starting at t_ms. blink-code uses it to apply
 * its click refractory at the exact point of the v1 evaluation order (after
 * the head gate and quiet gate, before the impulse refractory), so impulses
 * inside the refractory are neither counted nor emitted. */
void cv2_blink_impulse_hold(cv2_blink_impulse_state_t *st, cv2_ms_t t_ms,
                            uint32_t hold_ms);
/* One sample of the channel output at t_ms -> NONE / IMPULSE / CANCEL. */
cv2_blink_event_t cv2_blink_dsp_step(cv2_blink_impulse_state_t *st,
                                     const cv2_blink_dsp_config_t *cfg,
                                     cv2_ms_t t_ms,
                                     const cv2_blink_channel_out_t *in);

/* ---- combined dsp (stage 0 -> stage 1) ---------------------------------- */

typedef struct {
  cv2_blink_channel_imu_state_t channel;
  cv2_blink_impulse_state_t impulse;
} cv2_blink_dsp_state_t;

void cv2_blink_dsp_config_defaults(cv2_blink_dsp_config_t *cfg);
void cv2_blink_dsp_reset(cv2_blink_dsp_state_t *st);
void cv2_blink_dsp_hold(cv2_blink_dsp_state_t *st, cv2_ms_t t_ms,
                        uint32_t hold_ms);
cv2_blink_event_t cv2_blink_dsp_update(cv2_blink_dsp_state_t *st,
                                       const cv2_blink_dsp_config_t *cfg,
                                       cv2_ms_t t_ms, const float gyro_dps[3]);

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_BLINK_DSP_H */
