/*
 * Colibrino v2 - feel defaults.
 *
 * DENYLISTED for the loop: human-owned feel constants. Every tuning value of
 * the pure core lives here (and only here) so that an assisted implementation
 * loop can never move a threshold while "fixing" a unit. The impulse
 * thresholds are sensitivity feel; the 300-700 / 800-1400 ms windows are the
 * gesture definition; the intent limits are the safety envelope. Values are
 * the ones physically validated on the sticks3 v1 firmware.
 *
 * The blink config structs are declared here (rather than in the unit
 * headers) because the profile blob carries them by value and this header is
 * the single place a human edits feel.
 */
#ifndef COLIBRINO_V2_FEEL_DEFAULTS_H
#define COLIBRINO_V2_FEEL_DEFAULTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- blink-dsp (stage 0 baseline + stage 1 impulse detector) ------------ */
#define CV2_FEEL_BLINK_BASELINE_TIME_CONSTANT_MS 350.0f
#define CV2_FEEL_BLINK_IMPULSE_ENTER_DPS 1.1f
#define CV2_FEEL_BLINK_IMPULSE_EXIT_DPS 0.6f
#define CV2_FEEL_BLINK_MAXIMUM_HEAD_RATE_DPS 2.5f
#define CV2_FEEL_BLINK_HEAD_MOTION_SUPPRESSION_MS 2000u
#define CV2_FEEL_BLINK_IMPULSE_MINIMUM_MS 20u
#define CV2_FEEL_BLINK_IMPULSE_MAXIMUM_MS 300u
#define CV2_FEEL_BLINK_IMPULSE_REFRACTORY_MS 300u

/* Padding-free by construction (8 x 4 bytes = 32) so the profile blob and the
 * in-memory struct have the same size; encoded field by field regardless. */
typedef struct {
  float baseline_time_constant_ms; /* EMA tau of the per-axis gyro baseline */
  float impulse_enter_dps;         /* residual >= enter opens an impulse */
  float impulse_exit_dps;          /* residual <= exit closes it */
  float maximum_head_rate_dps;     /* |gyro| above this = head motion gate */
  uint32_t head_motion_suppression_ms; /* quiet time after head motion */
  uint32_t impulse_minimum_ms;     /* shorter = spike, rejected */
  uint32_t impulse_maximum_ms;     /* longer = hold, cancelled */
  uint32_t impulse_refractory_ms;  /* dead time after an accepted impulse */
} cv2_blink_dsp_config_t;

/* ---- blink-code (double . pause . double temporal code) ------------------ */
#define CV2_FEEL_BLINK_DOUBLE_MINIMUM_MS 300u
#define CV2_FEEL_BLINK_DOUBLE_MAXIMUM_MS 700u
#define CV2_FEEL_BLINK_PAUSE_MINIMUM_MS 800u
#define CV2_FEEL_BLINK_PAUSE_MAXIMUM_MS 1400u
#define CV2_FEEL_BLINK_CLICK_REFRACTORY_MS 1500u

typedef struct {
  uint32_t double_blink_minimum_ms;   /* gap after impulses 1 and 3 */
  uint32_t double_blink_maximum_ms;
  uint32_t deliberate_pause_minimum_ms; /* gap after impulse 2 */
  uint32_t deliberate_pause_maximum_ms;
  uint32_t click_refractory_ms;       /* impulses ignored after a click */
} cv2_blink_code_config_t;

/* ---- event lifetimes ----------------------------------------------------- */
#define CV2_FEEL_TTL_IMPULSE_MS 1000u
#define CV2_FEEL_TTL_CLICK_MS 250u
/* Confidence a coded click candidate carries (0..255). */
#define CV2_FEEL_BLINK_CODE_CONFIDENCE 255u
/* Confidence a raw impulse carries; below the intent minimum on purpose so a
 * lone impulse can never be mistaken for a click by a misrouted consumer. */
#define CV2_FEEL_BLINK_IMPULSE_CONFIDENCE 64u

/* ---- access-intent arbiter ---------------------------------------------- */
#define CV2_FEEL_INTENT_PRODUCER_TIMEOUT_MS 500u
#define CV2_FEEL_INTENT_COOLDOWN_MS 1500u
#define CV2_FEEL_INTENT_MAX_EVENT_AGE_MS 100u
#define CV2_FEEL_INTENT_MIN_CONFIDENCE 128u

/* ---- imu-motion (contract-only this round; mirrors sticks3 MotionConfig) - */
#define CV2_FEEL_MOTION_HORIZONTAL_AXIS 1u
#define CV2_FEEL_MOTION_HORIZONTAL_SIGN (-1)
#define CV2_FEEL_MOTION_VERTICAL_AXIS 0u
#define CV2_FEEL_MOTION_VERTICAL_SIGN 1
#define CV2_FEEL_MOTION_DEADZONE_DPS 1.4f
#define CV2_FEEL_MOTION_PIXELS_PER_DEGREE 18.0f
#define CV2_FEEL_MOTION_LOW_PASS_ALPHA 0.28f
#define CV2_FEEL_MOTION_MAXIMUM_REPORT 60

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_FEEL_DEFAULTS_H */
