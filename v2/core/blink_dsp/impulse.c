/*
 * blink-dsp stage 1: impulse detector.
 *
 * Evaluation order per sample is the sticks3 ImuBlinkDetector order, which is
 * the safety boundary that was physically validated:
 *   seed sample -> open the 2 s quiet gate (CANCEL)
 *   head rate above the gate -> CANCEL + quiet gate restart
 *   quiet gate running -> NONE
 *   external hold running (blink-code click refractory) -> NONE
 *   impulse refractory running -> NONE
 *   idle: residual >= enter opens an impulse -> NONE
 *   open: residual > exit and held past maximum -> CANCEL (overlong)
 *   open: residual > exit -> NONE
 *   closed: duration outside [minimum, maximum] -> CANCEL (duration)
 *   closed: IMPULSE with duration
 * The click refractory itself is not a dsp concept: it arrives through
 * cv2_blink_impulse_hold from the consumer.
 */
#include "colibrino/v2/blink_dsp.h"

void cv2_blink_dsp_config_defaults(cv2_blink_dsp_config_t *cfg) {
  if (cfg == NULL) {
    return;
  }
  cfg->baseline_time_constant_ms = CV2_FEEL_BLINK_BASELINE_TIME_CONSTANT_MS;
  cfg->impulse_enter_dps = CV2_FEEL_BLINK_IMPULSE_ENTER_DPS;
  cfg->impulse_exit_dps = CV2_FEEL_BLINK_IMPULSE_EXIT_DPS;
  cfg->maximum_head_rate_dps = CV2_FEEL_BLINK_MAXIMUM_HEAD_RATE_DPS;
  cfg->head_motion_suppression_ms = CV2_FEEL_BLINK_HEAD_MOTION_SUPPRESSION_MS;
  cfg->impulse_minimum_ms = CV2_FEEL_BLINK_IMPULSE_MINIMUM_MS;
  cfg->impulse_maximum_ms = CV2_FEEL_BLINK_IMPULSE_MAXIMUM_MS;
  cfg->impulse_refractory_ms = CV2_FEEL_BLINK_IMPULSE_REFRACTORY_MS;
}

void cv2_blink_impulse_reset(cv2_blink_impulse_state_t *st) {
  if (st == NULL) {
    return;
  }
  st->impulse_active = false;
  st->impulse_started_ms = 0u;
  st->have_last_impulse = false;
  st->last_impulse_ms = 0u;
  st->head_suppressed = false;
  st->last_head_motion_ms = 0u;
  st->hold_active = false;
  st->hold_started_ms = 0u;
  st->hold_ms = 0u;
  st->accepted_impulses = 0u;
  st->next_seq = 1u;
}

void cv2_blink_impulse_hold(cv2_blink_impulse_state_t *st, cv2_ms_t t_ms,
                            uint32_t hold_ms) {
  if (st == NULL) {
    return;
  }
  st->hold_active = true;
  st->hold_started_ms = t_ms;
  st->hold_ms = hold_ms;
}

static cv2_blink_event_t none_event(void) {
  cv2_blink_event_t ev;
  ev.hdr.magic = 0u;
  ev.hdr.version = 0u;
  ev.hdr.kind = (uint8_t)CV2_BLINK_NONE;
  ev.hdr.producer_id = 0u;
  ev.hdr.size = 0u;
  ev.hdr.seq = 0u;
  ev.hdr.t_ms = 0u;
  ev.ttl_ms = 0u;
  ev.confidence = 0u;
  ev.reason = 0u;
  ev.duration_ms = 0u;
  ev.reserved = 0u;
  return ev;
}

static cv2_blink_event_t emit(cv2_blink_impulse_state_t *st, uint8_t kind,
                              cv2_ms_t t_ms, uint8_t reason,
                              uint32_t duration_ms) {
  cv2_blink_event_t ev;
  ev.hdr.magic = (uint16_t)CV2_EVENT_MAGIC;
  ev.hdr.version = (uint8_t)CV2_EVENT_VERSION;
  ev.hdr.kind = kind;
  ev.hdr.producer_id = (uint16_t)CV2_PRODUCER_BLINK_IMU;
  ev.hdr.size = (uint16_t)CV2_EVENT_SIZE;
  ev.hdr.seq = st->next_seq++;
  ev.hdr.t_ms = t_ms;
  ev.ttl_ms = (uint16_t)CV2_FEEL_TTL_IMPULSE_MS;
  ev.confidence = (uint8_t)CV2_FEEL_BLINK_IMPULSE_CONFIDENCE;
  ev.reason = reason;
  ev.duration_ms = duration_ms > 0xFFFFu ? (uint16_t)0xFFFFu
                                         : (uint16_t)duration_ms;
  ev.reserved = 0u;
  return ev;
}

cv2_blink_event_t cv2_blink_dsp_step(cv2_blink_impulse_state_t *st,
                                     const cv2_blink_dsp_config_t *cfg,
                                     cv2_ms_t t_ms,
                                     const cv2_blink_channel_out_t *in) {
  if (st == NULL || cfg == NULL || in == NULL) {
    return none_event();
  }
  if (!in->valid) {
    /* A reset commonly follows boot, a validation-stage transition, or
     * mouse arming. Require a complete quiet interval before accepting the
     * first pulse so motion immediately before the reset cannot seed a
     * click. */
    st->head_suppressed = true;
    st->last_head_motion_ms = t_ms;
    return emit(st, (uint8_t)CV2_BLINK_CANCEL, t_ms,
                (uint8_t)CV2_BLINK_REASON_QUIET_GATE, 0u);
  }

  /* Any pointer-scale motion cancels an incomplete sequence and starts a
   * quiet-time gate. This is the primary false-click safety boundary. */
  if (in->head_rate_dps > cfg->maximum_head_rate_dps) {
    st->impulse_active = false;
    st->head_suppressed = true;
    st->last_head_motion_ms = t_ms;
    return emit(st, (uint8_t)CV2_BLINK_CANCEL, t_ms,
                (uint8_t)CV2_BLINK_REASON_HEAD_MOTION, 0u);
  }
  if (st->head_suppressed) {
    if (t_ms - st->last_head_motion_ms < cfg->head_motion_suppression_ms) {
      return none_event();
    }
    st->head_suppressed = false;
  }

  if (st->hold_active) {
    if (t_ms - st->hold_started_ms < st->hold_ms) {
      return none_event();
    }
    st->hold_active = false;
  }

  if (st->have_last_impulse &&
      t_ms - st->last_impulse_ms < cfg->impulse_refractory_ms) {
    return none_event();
  }
  st->have_last_impulse = false;

  if (!st->impulse_active) {
    if (in->residual_dps >= cfg->impulse_enter_dps) {
      st->impulse_active = true;
      st->impulse_started_ms = t_ms;
    }
    return none_event();
  }

  if (in->residual_dps > cfg->impulse_exit_dps) {
    if (t_ms - st->impulse_started_ms > cfg->impulse_maximum_ms) {
      st->impulse_active = false;
      return emit(st, (uint8_t)CV2_BLINK_CANCEL, t_ms,
                  (uint8_t)CV2_BLINK_REASON_OVERLONG, 0u);
    }
    return none_event();
  }

  const uint32_t impulse_duration_ms = t_ms - st->impulse_started_ms;
  st->impulse_active = false;
  if (impulse_duration_ms < cfg->impulse_minimum_ms ||
      impulse_duration_ms > cfg->impulse_maximum_ms) {
    return emit(st, (uint8_t)CV2_BLINK_CANCEL, t_ms,
                (uint8_t)CV2_BLINK_REASON_DURATION, 0u);
  }

  ++st->accepted_impulses;
  st->have_last_impulse = true;
  st->last_impulse_ms = t_ms;
  return emit(st, (uint8_t)CV2_BLINK_IMPULSE, t_ms,
              (uint8_t)CV2_BLINK_REASON_NONE, impulse_duration_ms);
}

/* ---- combined dsp ------------------------------------------------------- */

void cv2_blink_dsp_reset(cv2_blink_dsp_state_t *st) {
  if (st == NULL) {
    return;
  }
  cv2_blink_channel_imu_reset(&st->channel);
  cv2_blink_impulse_reset(&st->impulse);
}

void cv2_blink_dsp_hold(cv2_blink_dsp_state_t *st, cv2_ms_t t_ms,
                        uint32_t hold_ms) {
  if (st == NULL) {
    return;
  }
  cv2_blink_impulse_hold(&st->impulse, t_ms, hold_ms);
}

cv2_blink_event_t cv2_blink_dsp_update(cv2_blink_dsp_state_t *st,
                                       const cv2_blink_dsp_config_t *cfg,
                                       cv2_ms_t t_ms, const float gyro_dps[3]) {
  if (st == NULL || cfg == NULL || gyro_dps == NULL) {
    return none_event();
  }
  const cv2_blink_channel_out_t ch =
      cv2_blink_channel_imu_update(&st->channel, cfg, t_ms, gyro_dps);
  return cv2_blink_dsp_step(&st->impulse, cfg, t_ms, &ch);
}
