/*
 * blink-code: double . deliberate pause . double -> CLICK_CANDIDATE.
 *
 * Sequence logic is the sticks3 ImuBlinkDetector logic verbatim: the gaps
 * after impulses 1 and 3 must fall in the double-blink window, the gap after
 * impulse 2 in the deliberate-pause window; a mismatching gap restarts the
 * pattern at the current impulse, except when a pause was expected but a
 * short gap arrived - then the two newest impulses stay as a possible first
 * double. Four ordinary blinks at a uniform cadence never match. After a
 * click the code stage ignores impulses for the click refractory (the
 * pipeline additionally holds blink-dsp so they are not counted either).
 */
#include "colibrino/v2/blink_code.h"

void cv2_blink_code_config_defaults(cv2_blink_code_config_t *cfg) {
  if (cfg == NULL) {
    return;
  }
  cfg->double_blink_minimum_ms = CV2_FEEL_BLINK_DOUBLE_MINIMUM_MS;
  cfg->double_blink_maximum_ms = CV2_FEEL_BLINK_DOUBLE_MAXIMUM_MS;
  cfg->deliberate_pause_minimum_ms = CV2_FEEL_BLINK_PAUSE_MINIMUM_MS;
  cfg->deliberate_pause_maximum_ms = CV2_FEEL_BLINK_PAUSE_MAXIMUM_MS;
  cfg->click_refractory_ms = CV2_FEEL_BLINK_CLICK_REFRACTORY_MS;
}

void cv2_blink_code_reset(cv2_blink_code_state_t *st) {
  if (st == NULL) {
    return;
  }
  st->sequence_impulses = 0u;
  st->previous_impulse_ms = 0u;
  st->first_impulse_ms = 0u;
  st->click_suppressed = false;
  st->last_click_ms = 0u;
  st->completed_sequences = 0u;
  st->next_seq = 1u;
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

static bool interval_matches(const cv2_blink_code_config_t *cfg,
                             uint32_t interval_ms, uint8_t completed_impulses) {
  /* Gaps after the first and third impulses complete each double blink. The
   * gap after the second impulse is intentionally longer, making the gesture
   * a temporal code rather than four ordinary blinks at a uniform cadence. */
  if (completed_impulses == 1u || completed_impulses == 3u) {
    return interval_ms >= cfg->double_blink_minimum_ms &&
           interval_ms <= cfg->double_blink_maximum_ms;
  }
  if (completed_impulses == 2u) {
    return interval_ms >= cfg->deliberate_pause_minimum_ms &&
           interval_ms <= cfg->deliberate_pause_maximum_ms;
  }
  return false;
}

static void cancel_sequence(cv2_blink_code_state_t *st) {
  st->sequence_impulses = 0u;
  st->previous_impulse_ms = 0u;
  st->first_impulse_ms = 0u;
}

bool cv2_blink_code_in_refractory(const cv2_blink_code_state_t *st,
                                  const cv2_blink_code_config_t *cfg,
                                  cv2_ms_t t_ms) {
  if (st == NULL || cfg == NULL || !st->click_suppressed) {
    return false;
  }
  return t_ms - st->last_click_ms < cfg->click_refractory_ms;
}

cv2_gesture_event_t cv2_blink_code_step(cv2_blink_code_state_t *st,
                                        const cv2_blink_event_t *in,
                                        const cv2_blink_code_config_t *cfg) {
  if (st == NULL || in == NULL || cfg == NULL) {
    return none_gesture();
  }
  if (in->hdr.kind == (uint8_t)CV2_BLINK_NONE) {
    return none_gesture();
  }
  if (cv2_header_validate(&in->hdr, (uint16_t)CV2_EVENT_SIZE,
                          (uint8_t)CV2_BLINK_KIND_MAX, in->ttl_ms) != CV2_HDR_OK) {
    return none_gesture(); /* malformed input never advances the pattern */
  }
  if (in->hdr.producer_id != (uint16_t)CV2_PRODUCER_BLINK_IMU &&
      in->hdr.producer_id != (uint16_t)CV2_PRODUCER_BLINK_OPTICAL) {
    return none_gesture(); /* only blink channels may feed the code matcher */
  }
  if (in->hdr.kind == (uint8_t)CV2_BLINK_CANCEL) {
    cancel_sequence(st);
    return none_gesture();
  }

  const cv2_ms_t now_ms = in->hdr.t_ms;
  if (st->click_suppressed) {
    if (now_ms - st->last_click_ms < cfg->click_refractory_ms) {
      return none_gesture();
    }
    st->click_suppressed = false;
  }

  if (st->sequence_impulses > 0u) {
    const uint32_t interval_ms = now_ms - st->previous_impulse_ms;
    if (!interval_matches(cfg, interval_ms, st->sequence_impulses)) {
      /* When a long pause was expected but another short interval arrives,
       * retain those two newest impulses as a possible first double blink.
       * Every other mismatch makes the current impulse the new pattern
       * start. */
      if (st->sequence_impulses == 2u && interval_matches(cfg, interval_ms, 1u)) {
        st->sequence_impulses = 2u;
        st->first_impulse_ms = st->previous_impulse_ms;
      } else {
        st->sequence_impulses = 1u;
        st->first_impulse_ms = now_ms;
      }
      st->previous_impulse_ms = now_ms;
      return none_gesture();
    }
  } else {
    st->first_impulse_ms = now_ms;
  }
  ++st->sequence_impulses;
  st->previous_impulse_ms = now_ms;

  if (st->sequence_impulses < (uint8_t)CV2_BLINK_CODE_PATTERN_IMPULSES) {
    return none_gesture();
  }

  const uint32_t span_ms = now_ms - st->first_impulse_ms;
  cancel_sequence(st);
  st->click_suppressed = true;
  st->last_click_ms = now_ms;
  ++st->completed_sequences;

  cv2_gesture_event_t ev;
  ev.hdr.magic = (uint16_t)CV2_EVENT_MAGIC;
  ev.hdr.version = (uint8_t)CV2_EVENT_VERSION;
  ev.hdr.kind = (uint8_t)CV2_GESTURE_CLICK_CANDIDATE;
  ev.hdr.producer_id = (uint16_t)CV2_PRODUCER_BLINK_CODE;
  ev.hdr.size = (uint16_t)CV2_EVENT_SIZE;
  ev.hdr.seq = st->next_seq++;
  ev.hdr.t_ms = now_ms;
  ev.ttl_ms = (uint16_t)CV2_FEEL_TTL_CLICK_MS;
  ev.confidence = (uint8_t)CV2_FEEL_BLINK_CODE_CONFIDENCE;
  ev.impulses = (uint8_t)CV2_BLINK_CODE_PATTERN_IMPULSES;
  ev.span_ms = span_ms > 0xFFFFu ? (uint16_t)0xFFFFu : (uint16_t)span_ms;
  ev.reserved = 0u;
  return ev;
}
