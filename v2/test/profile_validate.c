/*
 * Profile oracle: defaults validate; every header/CRC/range/mask corruption is
 * refused (including the values that would silently disable a safety gate:
 * zero windows, infinite floats, nonzero reserved bytes); a failed load never
 * touches the caller's struct; the CRC is the IEEE one and is only corruption
 * detection.
 */
#include <string.h>

#include "colibrino/v2/profile.h"
#include "support/unity_main.h"

static void reseal(cv2_profile_t *p) { p->crc32 = cv2_profile_crc(p); }

static void test_crc32_known_vector(void) {
  const uint8_t check[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, cv2_crc32(check, sizeof check));
  TEST_ASSERT_EQUAL_HEX32(0u, cv2_crc32(check, 0u));
  /* Streaming form composes. */
  uint32_t c = cv2_crc32_update(0u, check, 4u);
  c = cv2_crc32_update(c, check + 4, 5u);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, c);
}

static void test_defaults_validate_and_round_trip(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OK, cv2_profile_validate(&p, sizeof p));
  TEST_ASSERT_EQUAL_HEX32(CV2_PROFILE_MAGIC, p.magic);
  TEST_ASSERT_EQUAL_UINT16(CV2_PROFILE_VERSION, p.version);
  TEST_ASSERT_EQUAL_UINT16(CV2_PROFILE_SIZE, p.size);
  TEST_ASSERT_EQUAL_FLOAT(1.1f, p.blink_dsp.impulse_enter_dps);
  TEST_ASSERT_EQUAL_UINT32(1400u, p.blink_code.deliberate_pause_maximum_ms);
  TEST_ASSERT_EQUAL_UINT16(1500u, p.intent.cooldown_ms);

  uint8_t blob[CV2_PROFILE_SIZE];
  TEST_ASSERT_EQUAL_size_t(CV2_PROFILE_SIZE,
                           cv2_profile_encode(&p, blob, sizeof blob));
  TEST_ASSERT_EQUAL_HEX8('C', blob[0]);
  TEST_ASSERT_EQUAL_HEX8('V', blob[1]);
  TEST_ASSERT_EQUAL_HEX8('2', blob[2]);
  TEST_ASSERT_EQUAL_HEX8('P', blob[3]);
  cv2_profile_t back;
  memset(&back, 0, sizeof back);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OK,
                        cv2_profile_load(blob, sizeof blob, &back));
  TEST_ASSERT_EQUAL_MEMORY(&p, &back, sizeof p);
}

static void test_bad_magic(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.magic = 0x50325644u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_MAGIC,
                        cv2_profile_validate(&p, sizeof p));
}

static void test_bad_version(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.version = 2u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_VERSION,
                        cv2_profile_validate(&p, sizeof p));
}

static void test_bad_size(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.size = 95u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_SIZE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_SIZE,
                        cv2_profile_validate(&p, sizeof p - 1u));
  uint8_t blob[CV2_PROFILE_SIZE + 1u];
  TEST_ASSERT_EQUAL_size_t(CV2_PROFILE_SIZE,
                           cv2_profile_encode(&p, blob, sizeof blob));
  cv2_profile_t out;
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_SIZE,
                        cv2_profile_load(blob, CV2_PROFILE_SIZE - 1u, &out));
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_SIZE,
                        cv2_profile_load(blob, CV2_PROFILE_SIZE + 1u, &out));
}

static void test_bad_crc(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.crc32 ^= 1u;
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_CRC,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  uint8_t blob[CV2_PROFILE_SIZE];
  cv2_profile_encode(&p, blob, sizeof blob);
  blob[20] ^= 0x10u; /* flip a bit inside blink_dsp */
  cv2_profile_t out;
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_CRC,
                        cv2_profile_load(blob, sizeof blob, &out));
}

static void test_enter_not_above_exit_rejected(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.blink_dsp.impulse_enter_dps = p.blink_dsp.impulse_exit_dps;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  p.blink_dsp.impulse_enter_dps = 0.5f; /* below exit */
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  p.blink_dsp.impulse_enter_dps = 3.0f; /* above the head gate */
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
}

static void test_double_max_reaching_pause_min_rejected(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.blink_code.double_blink_maximum_ms = p.blink_code.deliberate_pause_minimum_ms;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.blink_code.deliberate_pause_maximum_ms =
      p.blink_code.deliberate_pause_minimum_ms - 1u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.blink_dsp.impulse_minimum_ms = p.blink_dsp.impulse_maximum_ms + 1u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
}

static void test_zero_confidence_and_nan_rejected(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.intent.min_confidence = 0u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  const uint32_t nan_bits = 0x7FC00000u;
  memcpy(&p.blink_dsp.impulse_exit_dps, &nan_bits, sizeof nan_bits);
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.motion.horizontal_sign = 2;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
}

/* Every safety-envelope window must be > 0: a zero cooldown lets a click
 * follow a click (and the queue latch clear in the same ms), zero refractory
 * or quiet-gate windows let a burst be counted as a pattern, a zero freshness
 * bound is a contradiction. */
static void test_zero_gate_windows_rejected(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.intent.cooldown_ms = 0u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.intent.max_event_age_ms = 0u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.intent.producer_timeout_ms = 0u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.blink_dsp.head_motion_suppression_ms = 0u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.blink_dsp.impulse_refractory_ms = 0u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.blink_code.click_refractory_ms = 0u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  /* 1 ms is the documented minimum for each of them. */
  cv2_profile_defaults(&p);
  p.intent.cooldown_ms = 1u;
  p.intent.max_event_age_ms = 1u;
  p.intent.producer_timeout_ms = 1u;
  p.blink_dsp.head_motion_suppression_ms = 1u;
  p.blink_dsp.impulse_refractory_ms = 1u;
  p.blink_code.click_refractory_ms = 1u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OK, cv2_profile_validate(&p, sizeof p));
}

/* +inf passes every ordering comparison, so it must be refused explicitly
 * for each float, in each direction that the ordering allows. */
static void test_infinite_floats_rejected(void) {
  const uint32_t pos_inf_bits = 0x7F800000u;
  const uint32_t neg_inf_bits = 0xFF800000u;
  float pos_inf;
  float neg_inf;
  memcpy(&pos_inf, &pos_inf_bits, sizeof pos_inf);
  memcpy(&neg_inf, &neg_inf_bits, sizeof neg_inf);
  cv2_profile_t p;

  cv2_profile_defaults(&p);
  p.blink_dsp.baseline_time_constant_ms = pos_inf;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.blink_dsp.impulse_enter_dps = pos_inf;
  p.blink_dsp.maximum_head_rate_dps = pos_inf;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.blink_dsp.maximum_head_rate_dps = pos_inf; /* head gate never trips */
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.blink_dsp.impulse_exit_dps = neg_inf; /* fails > 0 already; stays out */
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.motion.pixels_per_degree = pos_inf;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.motion.deadzone_dps = pos_inf;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  const uint32_t nan_bits = 0x7FC00000u;
  memcpy(&p.motion.low_pass_alpha, &nan_bits, sizeof nan_bits);
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  /* Large but finite values are still in range. */
  cv2_profile_defaults(&p);
  p.blink_dsp.maximum_head_rate_dps = 3.0e38f;
  p.motion.pixels_per_degree = 3.0e38f;
  p.motion.deadzone_dps = 3.0e38f;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OK, cv2_profile_validate(&p, sizeof p));
}

/* Reserved bytes are zero in version 1, in the header and in each unit. */
static void test_nonzero_reserved_bytes_rejected(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.reserved[1] = 1u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.intent.reserved = 0x80u;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  cv2_profile_defaults(&p);
  p.motion.reserved[2] = 0xFFu;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_validate(&p, sizeof p));
  /* Through the blob as well: byte 9 is header reserved[0]. */
  cv2_profile_defaults(&p);
  uint8_t blob[CV2_PROFILE_SIZE];
  cv2_profile_encode(&p, blob, sizeof blob);
  blob[9] = 1u;
  const uint32_t crc = cv2_crc32(blob, 92u);
  blob[92] = (uint8_t)(crc & 0xFFu);
  blob[93] = (uint8_t)((crc >> 8) & 0xFFu);
  blob[94] = (uint8_t)((crc >> 16) & 0xFFu);
  blob[95] = (uint8_t)(crc >> 24);
  cv2_profile_t out;
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OUT_OF_RANGE,
                        cv2_profile_load(blob, sizeof blob, &out));
}

static void test_unknown_producer_bit_never_enabled(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.enabled_producers_mask = (uint8_t)(CV2_PRODUCER_KNOWN_MASK | 0x80u);
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_UNKNOWN_PRODUCER,
                        cv2_profile_validate(&p, sizeof p));
  p.enabled_producers_mask = CV2_PRODUCER_BIT(CV2_PRODUCER_NONE);
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_UNKNOWN_PRODUCER,
                        cv2_profile_validate(&p, sizeof p));
  p.enabled_producers_mask = CV2_PRODUCER_KNOWN_MASK;
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OK, cv2_profile_validate(&p, sizeof p));
  p.enabled_producers_mask = 0u; /* nothing enabled is a valid profile */
  reseal(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OK, cv2_profile_validate(&p, sizeof p));
}

static void test_load_leaves_out_untouched_on_failure(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  p.enabled_producers_mask = 0x80u;
  reseal(&p);
  uint8_t blob[CV2_PROFILE_SIZE];
  cv2_profile_encode(&p, blob, sizeof blob);

  cv2_profile_t out;
  cv2_profile_t sentinel;
  memset(&out, 0x7B, sizeof out);
  memcpy(&sentinel, &out, sizeof out);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_UNKNOWN_PRODUCER,
                        cv2_profile_load(blob, sizeof blob, &out));
  TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof out);
  blob[0] = 0u;
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_MAGIC,
                        cv2_profile_load(blob, sizeof blob, &out));
  TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof out);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_BAD_SIZE,
                        cv2_profile_load(NULL, sizeof blob, &out));
  TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof out);
}

static void test_encoded_blob_matches_documented_layout(void) {
  cv2_profile_t p;
  cv2_profile_defaults(&p);
  uint8_t blob[CV2_PROFILE_SIZE];
  cv2_profile_encode(&p, blob, sizeof blob);
  /* version @4, size @6, mask @8, dsp @12 (tau 350.0f = 0x43AF0000),
   * code @44 (300 = 0x12C), intent @64 (500 = 0x1F4), motion @72 (axis 1,
   * sign -1), crc @92. */
  TEST_ASSERT_EQUAL_HEX8(0x01, blob[4]);
  TEST_ASSERT_EQUAL_HEX8(0x60, blob[6]);
  TEST_ASSERT_EQUAL_HEX8(0x0C, blob[8]);
  TEST_ASSERT_EQUAL_HEX8(0x00, blob[12]);
  TEST_ASSERT_EQUAL_HEX8(0x00, blob[13]);
  TEST_ASSERT_EQUAL_HEX8(0xAF, blob[14]);
  TEST_ASSERT_EQUAL_HEX8(0x43, blob[15]);
  TEST_ASSERT_EQUAL_HEX8(0x2C, blob[44]);
  TEST_ASSERT_EQUAL_HEX8(0x01, blob[45]);
  TEST_ASSERT_EQUAL_HEX8(0xF4, blob[64]);
  TEST_ASSERT_EQUAL_HEX8(0x01, blob[65]);
  TEST_ASSERT_EQUAL_HEX8(0x01, blob[72]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, blob[73]);
  const uint32_t crc = cv2_crc32(blob, 92u);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xFFu), blob[92]);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 24), blob[95]);
}

CV2_UNITY_MAIN(
  RUN_TEST(test_crc32_known_vector);
  RUN_TEST(test_defaults_validate_and_round_trip);
  RUN_TEST(test_bad_magic);
  RUN_TEST(test_bad_version);
  RUN_TEST(test_bad_size);
  RUN_TEST(test_bad_crc);
  RUN_TEST(test_enter_not_above_exit_rejected);
  RUN_TEST(test_double_max_reaching_pause_min_rejected);
  RUN_TEST(test_zero_confidence_and_nan_rejected);
  RUN_TEST(test_zero_gate_windows_rejected);
  RUN_TEST(test_infinite_floats_rejected);
  RUN_TEST(test_nonzero_reserved_bytes_rejected);
  RUN_TEST(test_unknown_producer_bit_never_enabled);
  RUN_TEST(test_load_leaves_out_untouched_on_failure);
  RUN_TEST(test_encoded_blob_matches_documented_layout);
)
