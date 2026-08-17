/*
 * Profile defaults, validation and atomic load.
 *
 * The blob is decoded field by field (little-endian) into a local; *out is
 * published only after magic, version, size, CRC, value ranges and the
 * producer mask all validate. Range rules keep every unit's invariants
 * (impulse exit < enter <= head gate, double window below the pause window,
 * a positive confidence floor) so a corrupted-but-CRC-valid profile written
 * by buggy tooling still cannot disable a safety gate.
 */
#include "colibrino/v2/profile.h"

#include "wire/le_bytes.h"

/* Encoded layout offsets (bytes). */
#define OFF_MAGIC 0u
#define OFF_VERSION 4u
#define OFF_SIZE 6u
#define OFF_MASK 8u
#define OFF_RESERVED 9u
#define OFF_DSP 12u
#define OFF_CODE 44u
#define OFF_INTENT 64u
#define OFF_MOTION 72u
#define OFF_CRC 92u

void cv2_profile_defaults(cv2_profile_t *out) {
  if (out == NULL) {
    return;
  }
  out->magic = CV2_PROFILE_MAGIC;
  out->version = CV2_PROFILE_VERSION;
  out->size = CV2_PROFILE_SIZE;
  /* Round one enables the blink pipeline producers; head motion is
   * contract-only and stays off until its implementation lands. */
  out->enabled_producers_mask =
      (uint8_t)(CV2_PRODUCER_BIT(CV2_PRODUCER_BLINK_IMU) |
                CV2_PRODUCER_BIT(CV2_PRODUCER_BLINK_CODE));
  out->reserved[0] = 0;
  out->reserved[1] = 0;
  out->reserved[2] = 0;

  out->blink_dsp.baseline_time_constant_ms =
      CV2_FEEL_BLINK_BASELINE_TIME_CONSTANT_MS;
  out->blink_dsp.impulse_enter_dps = CV2_FEEL_BLINK_IMPULSE_ENTER_DPS;
  out->blink_dsp.impulse_exit_dps = CV2_FEEL_BLINK_IMPULSE_EXIT_DPS;
  out->blink_dsp.maximum_head_rate_dps = CV2_FEEL_BLINK_MAXIMUM_HEAD_RATE_DPS;
  out->blink_dsp.head_motion_suppression_ms =
      CV2_FEEL_BLINK_HEAD_MOTION_SUPPRESSION_MS;
  out->blink_dsp.impulse_minimum_ms = CV2_FEEL_BLINK_IMPULSE_MINIMUM_MS;
  out->blink_dsp.impulse_maximum_ms = CV2_FEEL_BLINK_IMPULSE_MAXIMUM_MS;
  out->blink_dsp.impulse_refractory_ms = CV2_FEEL_BLINK_IMPULSE_REFRACTORY_MS;

  out->blink_code.double_blink_minimum_ms = CV2_FEEL_BLINK_DOUBLE_MINIMUM_MS;
  out->blink_code.double_blink_maximum_ms = CV2_FEEL_BLINK_DOUBLE_MAXIMUM_MS;
  out->blink_code.deliberate_pause_minimum_ms = CV2_FEEL_BLINK_PAUSE_MINIMUM_MS;
  out->blink_code.deliberate_pause_maximum_ms = CV2_FEEL_BLINK_PAUSE_MAXIMUM_MS;
  out->blink_code.click_refractory_ms = CV2_FEEL_BLINK_CLICK_REFRACTORY_MS;

  cv2_intent_config_defaults(&out->intent);

  out->motion.horizontal_axis = CV2_FEEL_MOTION_HORIZONTAL_AXIS;
  out->motion.horizontal_sign = CV2_FEEL_MOTION_HORIZONTAL_SIGN;
  out->motion.vertical_axis = CV2_FEEL_MOTION_VERTICAL_AXIS;
  out->motion.vertical_sign = CV2_FEEL_MOTION_VERTICAL_SIGN;
  out->motion.deadzone_dps = CV2_FEEL_MOTION_DEADZONE_DPS;
  out->motion.pixels_per_degree = CV2_FEEL_MOTION_PIXELS_PER_DEGREE;
  out->motion.low_pass_alpha = CV2_FEEL_MOTION_LOW_PASS_ALPHA;
  out->motion.max_report = CV2_FEEL_MOTION_MAXIMUM_REPORT;
  out->motion.reserved[0] = 0;
  out->motion.reserved[1] = 0;
  out->motion.reserved[2] = 0;

  out->crc32 = cv2_profile_crc(out);
}

/* Field-by-field encode of everything but the CRC. */
static void encode_body(const cv2_profile_t *p, uint8_t *b) {
  cv2_le_put_u32(b + OFF_MAGIC, p->magic);
  cv2_le_put_u16(b + OFF_VERSION, p->version);
  cv2_le_put_u16(b + OFF_SIZE, p->size);
  cv2_le_put_u8(b + OFF_MASK, p->enabled_producers_mask);
  cv2_le_put_u8(b + OFF_RESERVED + 0, p->reserved[0]);
  cv2_le_put_u8(b + OFF_RESERVED + 1, p->reserved[1]);
  cv2_le_put_u8(b + OFF_RESERVED + 2, p->reserved[2]);

  cv2_le_put_f32(b + OFF_DSP + 0, p->blink_dsp.baseline_time_constant_ms);
  cv2_le_put_f32(b + OFF_DSP + 4, p->blink_dsp.impulse_enter_dps);
  cv2_le_put_f32(b + OFF_DSP + 8, p->blink_dsp.impulse_exit_dps);
  cv2_le_put_f32(b + OFF_DSP + 12, p->blink_dsp.maximum_head_rate_dps);
  cv2_le_put_u32(b + OFF_DSP + 16, p->blink_dsp.head_motion_suppression_ms);
  cv2_le_put_u32(b + OFF_DSP + 20, p->blink_dsp.impulse_minimum_ms);
  cv2_le_put_u32(b + OFF_DSP + 24, p->blink_dsp.impulse_maximum_ms);
  cv2_le_put_u32(b + OFF_DSP + 28, p->blink_dsp.impulse_refractory_ms);

  cv2_le_put_u32(b + OFF_CODE + 0, p->blink_code.double_blink_minimum_ms);
  cv2_le_put_u32(b + OFF_CODE + 4, p->blink_code.double_blink_maximum_ms);
  cv2_le_put_u32(b + OFF_CODE + 8, p->blink_code.deliberate_pause_minimum_ms);
  cv2_le_put_u32(b + OFF_CODE + 12, p->blink_code.deliberate_pause_maximum_ms);
  cv2_le_put_u32(b + OFF_CODE + 16, p->blink_code.click_refractory_ms);

  cv2_le_put_u16(b + OFF_INTENT + 0, p->intent.producer_timeout_ms);
  cv2_le_put_u16(b + OFF_INTENT + 2, p->intent.cooldown_ms);
  cv2_le_put_u16(b + OFF_INTENT + 4, p->intent.max_event_age_ms);
  cv2_le_put_u8(b + OFF_INTENT + 6, p->intent.min_confidence);
  cv2_le_put_u8(b + OFF_INTENT + 7, p->intent.reserved);

  cv2_le_put_u8(b + OFF_MOTION + 0, p->motion.horizontal_axis);
  cv2_le_put_i8(b + OFF_MOTION + 1, p->motion.horizontal_sign);
  cv2_le_put_u8(b + OFF_MOTION + 2, p->motion.vertical_axis);
  cv2_le_put_i8(b + OFF_MOTION + 3, p->motion.vertical_sign);
  cv2_le_put_f32(b + OFF_MOTION + 4, p->motion.deadzone_dps);
  cv2_le_put_f32(b + OFF_MOTION + 8, p->motion.pixels_per_degree);
  cv2_le_put_f32(b + OFF_MOTION + 12, p->motion.low_pass_alpha);
  cv2_le_put_i8(b + OFF_MOTION + 16, p->motion.max_report);
  cv2_le_put_u8(b + OFF_MOTION + 17, p->motion.reserved[0]);
  cv2_le_put_u8(b + OFF_MOTION + 18, p->motion.reserved[1]);
  cv2_le_put_u8(b + OFF_MOTION + 19, p->motion.reserved[2]);
}

static void decode_body(const uint8_t *b, cv2_profile_t *p) {
  p->magic = cv2_le_get_u32(b + OFF_MAGIC);
  p->version = cv2_le_get_u16(b + OFF_VERSION);
  p->size = cv2_le_get_u16(b + OFF_SIZE);
  p->enabled_producers_mask = cv2_le_get_u8(b + OFF_MASK);
  p->reserved[0] = cv2_le_get_u8(b + OFF_RESERVED + 0);
  p->reserved[1] = cv2_le_get_u8(b + OFF_RESERVED + 1);
  p->reserved[2] = cv2_le_get_u8(b + OFF_RESERVED + 2);

  p->blink_dsp.baseline_time_constant_ms = cv2_le_get_f32(b + OFF_DSP + 0);
  p->blink_dsp.impulse_enter_dps = cv2_le_get_f32(b + OFF_DSP + 4);
  p->blink_dsp.impulse_exit_dps = cv2_le_get_f32(b + OFF_DSP + 8);
  p->blink_dsp.maximum_head_rate_dps = cv2_le_get_f32(b + OFF_DSP + 12);
  p->blink_dsp.head_motion_suppression_ms = cv2_le_get_u32(b + OFF_DSP + 16);
  p->blink_dsp.impulse_minimum_ms = cv2_le_get_u32(b + OFF_DSP + 20);
  p->blink_dsp.impulse_maximum_ms = cv2_le_get_u32(b + OFF_DSP + 24);
  p->blink_dsp.impulse_refractory_ms = cv2_le_get_u32(b + OFF_DSP + 28);

  p->blink_code.double_blink_minimum_ms = cv2_le_get_u32(b + OFF_CODE + 0);
  p->blink_code.double_blink_maximum_ms = cv2_le_get_u32(b + OFF_CODE + 4);
  p->blink_code.deliberate_pause_minimum_ms = cv2_le_get_u32(b + OFF_CODE + 8);
  p->blink_code.deliberate_pause_maximum_ms =
      cv2_le_get_u32(b + OFF_CODE + 12);
  p->blink_code.click_refractory_ms = cv2_le_get_u32(b + OFF_CODE + 16);

  p->intent.producer_timeout_ms = cv2_le_get_u16(b + OFF_INTENT + 0);
  p->intent.cooldown_ms = cv2_le_get_u16(b + OFF_INTENT + 2);
  p->intent.max_event_age_ms = cv2_le_get_u16(b + OFF_INTENT + 4);
  p->intent.min_confidence = cv2_le_get_u8(b + OFF_INTENT + 6);
  p->intent.reserved = cv2_le_get_u8(b + OFF_INTENT + 7);

  p->motion.horizontal_axis = cv2_le_get_u8(b + OFF_MOTION + 0);
  p->motion.horizontal_sign = cv2_le_get_i8(b + OFF_MOTION + 1);
  p->motion.vertical_axis = cv2_le_get_u8(b + OFF_MOTION + 2);
  p->motion.vertical_sign = cv2_le_get_i8(b + OFF_MOTION + 3);
  p->motion.deadzone_dps = cv2_le_get_f32(b + OFF_MOTION + 4);
  p->motion.pixels_per_degree = cv2_le_get_f32(b + OFF_MOTION + 8);
  p->motion.low_pass_alpha = cv2_le_get_f32(b + OFF_MOTION + 12);
  p->motion.max_report = cv2_le_get_i8(b + OFF_MOTION + 16);
  p->motion.reserved[0] = cv2_le_get_u8(b + OFF_MOTION + 17);
  p->motion.reserved[1] = cv2_le_get_u8(b + OFF_MOTION + 18);
  p->motion.reserved[2] = cv2_le_get_u8(b + OFF_MOTION + 19);

  p->crc32 = cv2_le_get_u32(b + OFF_CRC);
}

uint32_t cv2_profile_crc(const cv2_profile_t *p) {
  uint8_t body[CV2_PROFILE_SIZE];
  if (p == NULL) {
    return 0;
  }
  encode_body(p, body);
  return cv2_crc32(body, OFF_CRC);
}

size_t cv2_profile_encode(const cv2_profile_t *p, uint8_t *buf, size_t cap) {
  if (p == NULL || buf == NULL || cap < CV2_PROFILE_SIZE) {
    return 0;
  }
  encode_body(p, buf);
  cv2_le_put_u32(buf + OFF_CRC, p->crc32);
  return CV2_PROFILE_SIZE;
}

/* Comparisons are written so that NaN fails them (NaN < x is false). */
static bool dsp_in_range(const cv2_blink_dsp_config_t *c) {
  if (!(c->baseline_time_constant_ms > 0.0f)) {
    return false;
  }
  if (!(c->impulse_exit_dps > 0.0f)) {
    return false;
  }
  if (!(c->impulse_exit_dps < c->impulse_enter_dps)) {
    return false;
  }
  if (!(c->impulse_enter_dps <= c->maximum_head_rate_dps)) {
    return false;
  }
  if (c->impulse_minimum_ms > c->impulse_maximum_ms) {
    return false;
  }
  if (c->impulse_maximum_ms == 0u) {
    return false;
  }
  return true;
}

static bool code_in_range(const cv2_blink_code_config_t *c) {
  if (c->double_blink_minimum_ms > c->double_blink_maximum_ms) {
    return false;
  }
  if (c->deliberate_pause_minimum_ms > c->deliberate_pause_maximum_ms) {
    return false;
  }
  /* The two windows must not overlap or a uniform cadence could match. */
  if (c->double_blink_maximum_ms >= c->deliberate_pause_minimum_ms) {
    return false;
  }
  return true;
}

static bool intent_in_range(const cv2_intent_config_t *c) {
  if (c->min_confidence == 0u) {
    return false;
  }
  if (c->producer_timeout_ms == 0u) {
    return false;
  }
  return true;
}

static bool motion_in_range(const cv2_motion_config_t *c) {
  if (c->horizontal_axis > 2u || c->vertical_axis > 2u) {
    return false;
  }
  if (c->horizontal_sign != 1 && c->horizontal_sign != -1) {
    return false;
  }
  if (c->vertical_sign != 1 && c->vertical_sign != -1) {
    return false;
  }
  if (!(c->deadzone_dps >= 0.0f)) {
    return false;
  }
  if (!(c->pixels_per_degree > 0.0f)) {
    return false;
  }
  if (!(c->low_pass_alpha >= 0.0f && c->low_pass_alpha <= 1.0f)) {
    return false;
  }
  if (c->max_report <= 0) {
    return false;
  }
  return true;
}

cv2_profile_status_t cv2_profile_validate(const cv2_profile_t *p, size_t len) {
  if (p == NULL || len != sizeof(cv2_profile_t)) {
    return CV2_PROFILE_BAD_SIZE;
  }
  if (p->magic != CV2_PROFILE_MAGIC) {
    return CV2_PROFILE_BAD_MAGIC;
  }
  if (p->version != CV2_PROFILE_VERSION) {
    return CV2_PROFILE_BAD_VERSION;
  }
  if (p->size != CV2_PROFILE_SIZE) {
    return CV2_PROFILE_BAD_SIZE;
  }
  if (p->crc32 != cv2_profile_crc(p)) {
    return CV2_PROFILE_BAD_CRC;
  }
  if (!dsp_in_range(&p->blink_dsp) || !code_in_range(&p->blink_code) ||
      !intent_in_range(&p->intent) || !motion_in_range(&p->motion)) {
    return CV2_PROFILE_OUT_OF_RANGE;
  }
  if ((p->enabled_producers_mask & (uint8_t)~CV2_PRODUCER_KNOWN_MASK) != 0u) {
    return CV2_PROFILE_UNKNOWN_PRODUCER;
  }
  return CV2_PROFILE_OK;
}

cv2_profile_status_t cv2_profile_load(const uint8_t *blob, size_t len,
                                      cv2_profile_t *out) {
  cv2_profile_t tmp;
  cv2_profile_status_t st;
  if (blob == NULL || out == NULL) {
    return CV2_PROFILE_BAD_SIZE;
  }
  if (len != CV2_PROFILE_SIZE) {
    return CV2_PROFILE_BAD_SIZE;
  }
  decode_body(blob, &tmp);
  st = cv2_profile_validate(&tmp, sizeof tmp);
  if (st != CV2_PROFILE_OK) {
    return st;
  }
  *out = tmp; /* atomic publish: only after every check passed */
  return CV2_PROFILE_OK;
}
