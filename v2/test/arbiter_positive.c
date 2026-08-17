/*
 * Positive oracle: the single grant path, and the release / latch dynamics
 * around it. Fixture: heartbeat at 100 ms, safe context, defaults
 * (timeout 500, cooldown 1500, max age 100, min confidence 128).
 */
#include "colibrino/v2/access_intent.h"
#include "support/intent_fixture.h"
#include "support/unity_main.h"

static void test_fresh_click_granted_once_with_seq(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(42u, 200u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 200u), 42u);
  TEST_ASSERT_TRUE(f.st.holding);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_DUPLICATE);
  /* Every release_all also drops the host-side hold. */
  TEST_ASSERT_FALSE(f.st.holding);
}

static void test_cooldown_blocks_second_click(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 200u), 1u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 1600u, 0u);
  ev = fixture_click(2u, 1699u); /* 1499 ms after the grant */
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 1699u), CV2_FAULT_COOLDOWN);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 1700u, 0u);
  ev = fixture_click(3u, 1700u); /* exactly one cooldown later */
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 1700u), 3u);
}

static void test_pointer_move_grants_delta_without_cooldown(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_IMU_MOTION, 100u, 0u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.hdr.producer_id = CV2_PRODUCER_IMU_MOTION;
  ev.hdr.kind = CV2_INTENT_POINTER_MOVE;
  ev.dx = -7;
  ev.dy = 3;
  cv2_action_t a = fixture_arbitrate(&f, &ev, 200u);
  TEST_ASSERT_EQUAL_UINT8(CV2_INTENT_POINTER_MOVE, a.kind);
  TEST_ASSERT_EQUAL_INT16(-7, a.dx);
  TEST_ASSERT_EQUAL_INT16(3, a.dy);
  TEST_ASSERT_EQUAL_UINT32(1u, a.granted_seq);
  ev.hdr.seq = 2u;
  ev.hdr.t_ms = 205u;
  a = fixture_arbitrate(&f, &ev, 205u);
  TEST_ASSERT_EQUAL_UINT8(CV2_INTENT_POINTER_MOVE, a.kind);
  TEST_ASSERT_EQUAL_UINT32(2u, a.granted_seq);
}

static void test_valid_header_rejected_event_still_refreshes_heartbeat(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  /* Low-confidence event at 500: rejected, but it is a valid header. */
  cv2_intent_event_t ev = fixture_click(1u, 500u);
  ev.confidence = 10u;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 500u), CV2_FAULT_LOW_CONFIDENCE);
  TEST_ASSERT_EQUAL_UINT32(500u, f.st.last_seen_ms[CV2_PRODUCER_BLINK_CODE]);
  /* Without that refresh the producer (last heartbeat 100) would be dead at
   * 900; with it the next event is granted. */
  ev = fixture_click(2u, 900u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 900u), 2u);
}

static void test_tick_no_action_when_safe(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_action_t a = fixture_arbitrate(&f, NULL, 200u);
  TEST_ASSERT_EQUAL_UINT8(CV2_INTENT_NONE, a.kind);
  TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
  TEST_ASSERT_EQUAL_UINT8(CV2_FAULT_NONE, a.fault);
  TEST_ASSERT_TRUE(f.st.prev_ctx_ok);
}

static void test_disarm_transition_tick_releases_all(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 200u), 1u);
  TEST_ASSERT_TRUE(f.st.holding);
  f.ctx.armed = false;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 210u), CV2_FAULT_UNARMED);
  TEST_ASSERT_FALSE(f.st.holding);
  TEST_ASSERT_FALSE(f.st.prev_ctx_ok);
  /* Still unsafe: the release is idempotent, never withdrawn. */
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 220u), CV2_FAULT_UNARMED);
}

static void test_tick_releases_on_producer_timeout(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 200u), 1u);
  cv2_action_t a = fixture_arbitrate(&f, NULL, 700u); /* 500 ms: alive */
  TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
  a = fixture_arbitrate(&f, NULL, 701u); /* 501 ms: timed out */
  ASSERT_REJECTED(a, CV2_FAULT_PRODUCER_UNHEALTHY);
  TEST_ASSERT_FALSE(f.st.holding);
}

static void test_queue_fault_latch_clears_after_ctx_clear_and_cooldown(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.ctx.queue_fault = true;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 200u), CV2_FAULT_QUEUE_FAULT);
  f.ctx.queue_fault = false;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 1699u), CV2_FAULT_QUEUE_FAULT);
  /* Cooldown elapsed since the last observed fault: latch clears; keep the
   * producer alive so only the latch is under test. */
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 1700u, 0u);
  cv2_action_t a = fixture_arbitrate(&f, NULL, 1700u);
  TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
  TEST_ASSERT_EQUAL_UINT8(CV2_FAULT_NONE, a.fault);
  const cv2_intent_event_t ev = fixture_click(1u, 1710u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 1710u), 1u);
}

static void test_queue_fault_seen_again_restarts_the_latch(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.ctx.queue_fault = true;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 200u), CV2_FAULT_QUEUE_FAULT);
  f.ctx.queue_fault = false;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 1000u), CV2_FAULT_QUEUE_FAULT);
  f.ctx.queue_fault = true; /* second burst restarts the cooldown */
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 1100u), CV2_FAULT_QUEUE_FAULT);
  f.ctx.queue_fault = false;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 2599u), CV2_FAULT_QUEUE_FAULT);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 2600u, 0u);
  const cv2_action_t a = fixture_arbitrate(&f, NULL, 2600u);
  TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
}

CV2_UNITY_MAIN(
  RUN_TEST(test_fresh_click_granted_once_with_seq);
  RUN_TEST(test_cooldown_blocks_second_click);
  RUN_TEST(test_pointer_move_grants_delta_without_cooldown);
  RUN_TEST(test_valid_header_rejected_event_still_refreshes_heartbeat);
  RUN_TEST(test_tick_no_action_when_safe);
  RUN_TEST(test_disarm_transition_tick_releases_all);
  RUN_TEST(test_tick_releases_on_producer_timeout);
  RUN_TEST(test_queue_fault_latch_clears_after_ctx_clear_and_cooldown);
  RUN_TEST(test_queue_fault_seen_again_restarts_the_latch);
)
