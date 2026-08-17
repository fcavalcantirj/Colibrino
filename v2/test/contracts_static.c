/* Runtime re-check of the compile-time contract facts in core/contracts.c. */
#include <stddef.h>

#include "colibrino/v2/access_intent.h"
#include "colibrino/v2/blink_code.h"
#include "colibrino/v2/blink_dsp.h"
#include "colibrino/v2/common.h"
#include "colibrino/v2/profile.h"
#include "support/unity_main.h"

static void test_header_is_16_bytes(void) {
  TEST_ASSERT_EQUAL_size_t(16u, sizeof(cv2_event_hdr_t));
  TEST_ASSERT_EQUAL_size_t(CV2_EVENT_HDR_SIZE, sizeof(cv2_event_hdr_t));
}

static void test_header_field_offsets(void) {
  TEST_ASSERT_EQUAL_size_t(0u, offsetof(cv2_event_hdr_t, magic));
  TEST_ASSERT_EQUAL_size_t(2u, offsetof(cv2_event_hdr_t, version));
  TEST_ASSERT_EQUAL_size_t(3u, offsetof(cv2_event_hdr_t, kind));
  TEST_ASSERT_EQUAL_size_t(4u, offsetof(cv2_event_hdr_t, producer_id));
  TEST_ASSERT_EQUAL_size_t(6u, offsetof(cv2_event_hdr_t, size));
  TEST_ASSERT_EQUAL_size_t(8u, offsetof(cv2_event_hdr_t, seq));
  TEST_ASSERT_EQUAL_size_t(12u, offsetof(cv2_event_hdr_t, t_ms));
}

static void test_events_are_24_bytes(void) {
  TEST_ASSERT_EQUAL_size_t(24u, sizeof(cv2_intent_event_t));
  TEST_ASSERT_EQUAL_size_t(24u, sizeof(cv2_blink_event_t));
  TEST_ASSERT_EQUAL_size_t(24u, sizeof(cv2_gesture_event_t));
  TEST_ASSERT_EQUAL_size_t(16u, offsetof(cv2_intent_event_t, ttl_ms));
  TEST_ASSERT_EQUAL_size_t(16u, offsetof(cv2_blink_event_t, ttl_ms));
  TEST_ASSERT_EQUAL_size_t(16u, offsetof(cv2_gesture_event_t, ttl_ms));
}

static void test_imu_sample_and_action_sizes(void) {
  TEST_ASSERT_EQUAL_size_t(32u, sizeof(cv2_imu_sample_t));
  TEST_ASSERT_EQUAL_size_t(12u, sizeof(cv2_action_t));
  TEST_ASSERT_EQUAL_size_t(96u, sizeof(cv2_profile_t));
}

static void test_producer_ids_are_fixed(void) {
  TEST_ASSERT_EQUAL_INT(0, CV2_PRODUCER_NONE);
  TEST_ASSERT_EQUAL_INT(1, CV2_PRODUCER_IMU_MOTION);
  TEST_ASSERT_EQUAL_INT(2, CV2_PRODUCER_BLINK_IMU);
  TEST_ASSERT_EQUAL_INT(3, CV2_PRODUCER_BLINK_CODE);
  TEST_ASSERT_EQUAL_INT(4, CV2_PRODUCER_BLINK_OPTICAL);
  TEST_ASSERT_EQUAL_INT(5, CV2_PRODUCER_VOICE);
  TEST_ASSERT_EQUAL_INT(6, CV2_PRODUCER_SWITCH);
  TEST_ASSERT_EQUAL_INT(7, CV2_PRODUCER_COUNT);
  TEST_ASSERT_EQUAL_HEX8(0x7Eu, CV2_PRODUCER_KNOWN_MASK);
}

static void test_magic_version_and_horizon(void) {
  TEST_ASSERT_EQUAL_HEX16(0xC2A1u, CV2_EVENT_MAGIC);
  TEST_ASSERT_EQUAL_UINT(1u, CV2_EVENT_VERSION);
  TEST_ASSERT_EQUAL_UINT32(60000u, CV2_MAX_TTL_MS);
  TEST_ASSERT_TRUE(CV2_MAX_TTL_MS < 0x7FFFFFFFu);
}

static void test_fault_ids_are_fixed(void) {
  TEST_ASSERT_EQUAL_INT(0, CV2_FAULT_NONE);
  TEST_ASSERT_EQUAL_INT(1, CV2_FAULT_MALFORMED);
  TEST_ASSERT_EQUAL_INT(2, CV2_FAULT_PRODUCER_UNKNOWN);
  TEST_ASSERT_EQUAL_INT(3, CV2_FAULT_PRODUCER_DISABLED);
  TEST_ASSERT_EQUAL_INT(4, CV2_FAULT_STALE);
  TEST_ASSERT_EQUAL_INT(5, CV2_FAULT_EXPIRED);
  TEST_ASSERT_EQUAL_INT(6, CV2_FAULT_DUPLICATE);
  TEST_ASSERT_EQUAL_INT(7, CV2_FAULT_LOW_CONFIDENCE);
  TEST_ASSERT_EQUAL_INT(8, CV2_FAULT_UNARMED);
  TEST_ASSERT_EQUAL_INT(9, CV2_FAULT_PRODUCER_UNHEALTHY);
  TEST_ASSERT_EQUAL_INT(10, CV2_FAULT_QUEUE_FAULT);
  TEST_ASSERT_EQUAL_INT(11, CV2_FAULT_DISCONNECTED);
  TEST_ASSERT_EQUAL_INT(12, CV2_FAULT_LOW_BATTERY);
  TEST_ASSERT_EQUAL_INT(13, CV2_FAULT_COOLDOWN);
  TEST_ASSERT_EQUAL_INT(14, CV2_FAULT_UNCALIBRATED);
}

static void test_time_macros_cross_wrap(void) {
  TEST_ASSERT_TRUE(CV2_MS_AFTER(0x00000005u, 0xFFFFFFF0u));
  TEST_ASSERT_FALSE(CV2_MS_AFTER(0xFFFFFFF0u, 0x00000005u));
  TEST_ASSERT_EQUAL_INT32(21, CV2_MS_DIFF(0x00000005u, 0xFFFFFFF0u));
  TEST_ASSERT_EQUAL_INT32(-21, CV2_MS_DIFF(0xFFFFFFF0u, 0x00000005u));
  TEST_ASSERT_FALSE(CV2_MS_AFTER(7u, 7u));
  TEST_ASSERT_TRUE(CV2_SEQ_AFTER(0u, 0xFFFFFFFFu));
  TEST_ASSERT_FALSE(CV2_SEQ_AFTER(0xFFFFFFFFu, 0u));
  TEST_ASSERT_FALSE(CV2_SEQ_AFTER(9u, 9u));
}

static void test_header_validate_order(void) {
  cv2_event_hdr_t h;
  h.magic = CV2_EVENT_MAGIC;
  h.version = CV2_EVENT_VERSION;
  h.kind = 1u;
  h.producer_id = CV2_PRODUCER_BLINK_CODE;
  h.size = CV2_EVENT_SIZE;
  h.seq = 1u;
  h.t_ms = 0u;
  TEST_ASSERT_EQUAL_INT(CV2_HDR_OK, cv2_header_validate(&h, 24u, 2u, 250u));
  TEST_ASSERT_EQUAL_INT(CV2_HDR_NULL, cv2_header_validate(NULL, 24u, 2u, 0u));
  h.magic = 0x1234u;
  TEST_ASSERT_EQUAL_INT(CV2_HDR_BAD_MAGIC,
                        cv2_header_validate(&h, 24u, 2u, 250u));
  h.magic = CV2_EVENT_MAGIC;
  h.version = 2u;
  TEST_ASSERT_EQUAL_INT(CV2_HDR_BAD_VERSION,
                        cv2_header_validate(&h, 24u, 2u, 250u));
  h.version = CV2_EVENT_VERSION;
  h.size = 23u;
  TEST_ASSERT_EQUAL_INT(CV2_HDR_BAD_SIZE,
                        cv2_header_validate(&h, 24u, 2u, 250u));
  h.size = CV2_EVENT_SIZE;
  h.kind = 3u;
  TEST_ASSERT_EQUAL_INT(CV2_HDR_BAD_KIND,
                        cv2_header_validate(&h, 24u, 2u, 250u));
  h.kind = 0u;
  TEST_ASSERT_EQUAL_INT(CV2_HDR_BAD_KIND,
                        cv2_header_validate(&h, 24u, 2u, 250u));
  h.kind = 1u;
  h.producer_id = CV2_PRODUCER_COUNT;
  TEST_ASSERT_EQUAL_INT(CV2_HDR_BAD_PRODUCER,
                        cv2_header_validate(&h, 24u, 2u, 250u));
  h.producer_id = CV2_PRODUCER_BLINK_CODE;
  TEST_ASSERT_EQUAL_INT(CV2_HDR_BAD_TTL,
                        cv2_header_validate(&h, 24u, 2u, 60001u));
  TEST_ASSERT_EQUAL_INT(CV2_HDR_OK,
                        cv2_header_validate(&h, 24u, 2u, 60000u));
}

CV2_UNITY_MAIN(
  RUN_TEST(test_header_is_16_bytes);
  RUN_TEST(test_header_field_offsets);
  RUN_TEST(test_events_are_24_bytes);
  RUN_TEST(test_imu_sample_and_action_sizes);
  RUN_TEST(test_producer_ids_are_fixed);
  RUN_TEST(test_magic_version_and_horizon);
  RUN_TEST(test_fault_ids_are_fixed);
  RUN_TEST(test_time_macros_cross_wrap);
  RUN_TEST(test_header_validate_order);
)
