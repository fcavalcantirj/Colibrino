/*
 * Wire codec oracle: byte-exact little-endian layout, round trips for every
 * event family, and the no-partial-write guarantee on every error path.
 */
#include <string.h>

#include "colibrino/v2/wire.h"
#include "support/unity_main.h"

static cv2_event_hdr_t sample_hdr(uint8_t kind, uint16_t producer) {
  cv2_event_hdr_t h;
  h.magic = CV2_EVENT_MAGIC;
  h.version = CV2_EVENT_VERSION;
  h.kind = kind;
  h.producer_id = producer;
  h.size = CV2_EVENT_SIZE;
  h.seq = 0x11223344u;
  h.t_ms = 0xAABBCCDDu;
  return h;
}

static void test_header_layout_is_little_endian(void) {
  uint8_t buf[16];
  const cv2_event_hdr_t h = sample_hdr(2u, CV2_PRODUCER_BLINK_CODE);
  TEST_ASSERT_EQUAL_size_t(16u, cv2_hdr_encode(&h, buf, sizeof buf));
  const uint8_t expected[16] = {0xA1, 0xC2, 0x01, 0x02, 0x03, 0x00, 0x18, 0x00,
                                0x44, 0x33, 0x22, 0x11, 0xDD, 0xCC, 0xBB, 0xAA};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 16);
  cv2_event_hdr_t back;
  memset(&back, 0, sizeof back);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_OK, cv2_hdr_decode(buf, sizeof buf, &back));
  TEST_ASSERT_EQUAL_MEMORY(&h, &back, sizeof h);
}

static void test_intent_round_trip(void) {
  cv2_intent_event_t ev;
  memset(&ev, 0, sizeof ev);
  ev.hdr = sample_hdr(CV2_INTENT_POINTER_MOVE, CV2_PRODUCER_IMU_MOTION);
  ev.ttl_ms = 0x0102u;
  ev.confidence = 0xEEu;
  ev.flags = 0x0Fu;
  ev.dx = -300;
  ev.dy = 12345;
  uint8_t buf[24];
  TEST_ASSERT_EQUAL_size_t(24u, cv2_intent_event_encode(&ev, buf, sizeof buf));
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[16]);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[17]);
  TEST_ASSERT_EQUAL_HEX8(0xEE, buf[18]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, buf[19]);
  TEST_ASSERT_EQUAL_HEX8(0xD4, buf[20]); /* -300 = 0xFED4 */
  TEST_ASSERT_EQUAL_HEX8(0xFE, buf[21]);
  cv2_intent_event_t back;
  memset(&back, 0xA5, sizeof back);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_OK,
                        cv2_intent_event_decode(buf, sizeof buf, &back));
  TEST_ASSERT_EQUAL_MEMORY(&ev, &back, sizeof ev);
}

static void test_blink_round_trip(void) {
  cv2_blink_event_t ev;
  memset(&ev, 0, sizeof ev);
  ev.hdr = sample_hdr(CV2_BLINK_IMPULSE, CV2_PRODUCER_BLINK_IMU);
  ev.ttl_ms = 1000u;
  ev.confidence = 64u;
  ev.reason = 0u;
  ev.duration_ms = 45u;
  uint8_t buf[24];
  TEST_ASSERT_EQUAL_size_t(24u, cv2_blink_event_encode(&ev, buf, sizeof buf));
  cv2_blink_event_t back;
  memset(&back, 0xA5, sizeof back);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_OK,
                        cv2_blink_event_decode(buf, sizeof buf, &back));
  TEST_ASSERT_EQUAL_MEMORY(&ev, &back, sizeof ev);
}

static void test_gesture_round_trip(void) {
  cv2_gesture_event_t ev;
  memset(&ev, 0, sizeof ev);
  ev.hdr = sample_hdr(CV2_GESTURE_CLICK_CANDIDATE, CV2_PRODUCER_BLINK_CODE);
  ev.ttl_ms = 250u;
  ev.confidence = 255u;
  ev.impulses = 4u;
  ev.span_ms = 2100u;
  uint8_t buf[24];
  TEST_ASSERT_EQUAL_size_t(24u,
                           cv2_gesture_event_encode(&ev, buf, sizeof buf));
  cv2_gesture_event_t back;
  memset(&back, 0xA5, sizeof back);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_OK,
                        cv2_gesture_event_decode(buf, sizeof buf, &back));
  TEST_ASSERT_EQUAL_MEMORY(&ev, &back, sizeof ev);
}

static void test_truncated_buffer_leaves_output_untouched(void) {
  cv2_intent_event_t ev;
  memset(&ev, 0, sizeof ev);
  ev.hdr = sample_hdr(CV2_INTENT_CLICK, CV2_PRODUCER_BLINK_CODE);
  uint8_t buf[24];
  TEST_ASSERT_EQUAL_size_t(24u, cv2_intent_event_encode(&ev, buf, sizeof buf));

  cv2_intent_event_t out;
  cv2_intent_event_t sentinel;
  memset(&out, 0x5A, sizeof out);
  memcpy(&sentinel, &out, sizeof out);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_TRUNCATED,
                        cv2_intent_event_decode(buf, 23u, &out));
  TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof out);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_TRUNCATED,
                        cv2_intent_event_decode(buf, 0u, &out));
  TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof out);

  cv2_event_hdr_t hout;
  cv2_event_hdr_t hsentinel;
  memset(&hout, 0x5A, sizeof hout);
  memcpy(&hsentinel, &hout, sizeof hout);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_TRUNCATED, cv2_hdr_decode(buf, 15u, &hout));
  TEST_ASSERT_EQUAL_MEMORY(&hsentinel, &hout, sizeof hout);

  cv2_blink_event_t bout;
  memset(&bout, 0x5A, sizeof bout);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_TRUNCATED,
                        cv2_blink_event_decode(buf, 10u, &bout));
  cv2_gesture_event_t gout;
  memset(&gout, 0x5A, sizeof gout);
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_TRUNCATED,
                        cv2_gesture_event_decode(buf, 10u, &gout));
}

static void test_garbage_magic_and_version_rejected(void) {
  cv2_gesture_event_t ev;
  memset(&ev, 0, sizeof ev);
  ev.hdr = sample_hdr(CV2_GESTURE_CLICK_CANDIDATE, CV2_PRODUCER_BLINK_CODE);
  uint8_t buf[24];
  TEST_ASSERT_EQUAL_size_t(24u,
                           cv2_gesture_event_encode(&ev, buf, sizeof buf));
  cv2_gesture_event_t out;
  cv2_gesture_event_t sentinel;
  memset(&out, 0x33, sizeof out);
  memcpy(&sentinel, &out, sizeof out);

  buf[0] ^= 0xFFu; /* magic */
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_BAD_HEADER,
                        cv2_gesture_event_decode(buf, sizeof buf, &out));
  TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof out);
  cv2_event_hdr_t hout;
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_BAD_HEADER,
                        cv2_hdr_decode(buf, sizeof buf, &hout));
  buf[0] ^= 0xFFu;

  buf[2] = 9u; /* version */
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_BAD_HEADER,
                        cv2_gesture_event_decode(buf, sizeof buf, &out));
  TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof out);
  buf[2] = (uint8_t)CV2_EVENT_VERSION;

  buf[6] = 16u; /* size mismatch for a typed decode */
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_BAD_HEADER,
                        cv2_gesture_event_decode(buf, sizeof buf, &out));
  TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof out);
  buf[6] = (uint8_t)CV2_EVENT_SIZE;
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_OK,
                        cv2_gesture_event_decode(buf, sizeof buf, &out));
}

static void test_encode_cap_too_small_returns_zero(void) {
  cv2_intent_event_t ev;
  memset(&ev, 0, sizeof ev);
  ev.hdr = sample_hdr(CV2_INTENT_CLICK, CV2_PRODUCER_BLINK_CODE);
  uint8_t buf[24];
  memset(buf, 0xEE, sizeof buf);
  TEST_ASSERT_EQUAL_size_t(0u, cv2_intent_event_encode(&ev, buf, 23u));
  TEST_ASSERT_EQUAL_size_t(0u, cv2_hdr_encode(&ev.hdr, buf, 15u));
  cv2_blink_event_t b;
  memset(&b, 0, sizeof b);
  TEST_ASSERT_EQUAL_size_t(0u, cv2_blink_event_encode(&b, buf, 0u));
  cv2_gesture_event_t g;
  memset(&g, 0, sizeof g);
  TEST_ASSERT_EQUAL_size_t(0u, cv2_gesture_event_encode(&g, buf, 8u));
  /* Nothing was written. */
  for (size_t i = 0; i < sizeof buf; ++i) {
    TEST_ASSERT_EQUAL_HEX8(0xEE, buf[i]);
  }
}

static void test_null_arguments(void) {
  uint8_t buf[24] = {0};
  cv2_intent_event_t ev;
  memset(&ev, 0, sizeof ev);
  TEST_ASSERT_EQUAL_size_t(0u, cv2_intent_event_encode(NULL, buf, sizeof buf));
  TEST_ASSERT_EQUAL_size_t(0u, cv2_intent_event_encode(&ev, NULL, sizeof buf));
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_BAD_ARG,
                        cv2_intent_event_decode(NULL, sizeof buf, &ev));
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_BAD_ARG,
                        cv2_intent_event_decode(buf, sizeof buf, NULL));
  TEST_ASSERT_EQUAL_INT(CV2_WIRE_BAD_ARG, cv2_hdr_decode(NULL, 16u, &ev.hdr));
}

CV2_UNITY_MAIN(
  RUN_TEST(test_header_layout_is_little_endian);
  RUN_TEST(test_intent_round_trip);
  RUN_TEST(test_blink_round_trip);
  RUN_TEST(test_gesture_round_trip);
  RUN_TEST(test_truncated_buffer_leaves_output_untouched);
  RUN_TEST(test_garbage_magic_and_version_rejected);
  RUN_TEST(test_encode_cap_too_small_returns_zero);
  RUN_TEST(test_null_arguments);
)
