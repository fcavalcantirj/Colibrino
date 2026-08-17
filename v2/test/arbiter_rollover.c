/*
 * Timestamp and sequence wrap: every time comparison in the arbiter must
 * behave identically when device millis cross 0xFFFFFFFF -> 0 (every ~49.7
 * days of uptime) and when a producer's seq wraps.
 */
#include "colibrino/v2/access_intent.h"
#include "support/intent_fixture.h"
#include "support/unity_main.h"

#define WRAP_BASE 0xFFFFFF00u

static void test_stale_and_fresh_across_wrap(void) {
  intent_fixture_t f;
  fixture_init(&f, WRAP_BASE);
  /* Granted just before the wrap. */
  cv2_intent_event_t ev = fixture_click(1u, WRAP_BASE + 0x40u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, WRAP_BASE + 0x40u), 1u);
  /* Fresh, in-order event just after the wrap: freshness math must see a
   * small positive age, not a ~4e9 ms one; only the cooldown may reject. */
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 0x00000010u, 0u);
  ev = fixture_click(2u, 0x00000020u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 0x00000030u), CV2_FAULT_COOLDOWN);
  /* An event stamped before the wrap but arbitrated after it: too old. */
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 0x00000100u, 0u);
  ev = fixture_click(3u, 0x00000021u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 0x00000100u), CV2_FAULT_STALE);
}

static void test_expired_across_wrap(void) {
  intent_fixture_t f;
  fixture_init(&f, 0xFFFFFFF0u);
  f.cfg.max_event_age_ms = 2000u; /* isolate expiry from freshness */
  cv2_intent_event_t ev = fixture_click(1u, 0xFFFFFFF0u);
  ev.ttl_ms = 100u; /* expires at 0x00000054 */
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 0x00000055u), CV2_FAULT_EXPIRED);
  fixture_init(&f, 0xFFFFFFF0u);
  f.cfg.max_event_age_ms = 2000u;
  ev = fixture_click(1u, 0xFFFFFFF0u);
  ev.ttl_ms = 100u;
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 0x00000054u), 1u);
}

static void test_cooldown_across_wrap(void) {
  intent_fixture_t f;
  fixture_init(&f, WRAP_BASE);
  cv2_intent_event_t ev = fixture_click(1u, WRAP_BASE);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, WRAP_BASE), 1u);
  /* 1499 ms later (crossing zero): still cooling. */
  cv2_ms_t now = WRAP_BASE + 1499u;
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, now, 0u);
  ev = fixture_click(2u, now);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, now), CV2_FAULT_COOLDOWN);
  /* 1500 ms later: granted. */
  now = WRAP_BASE + 1500u;
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, now, 0u);
  ev = fixture_click(3u, now);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, now), 3u);
  TEST_ASSERT_TRUE(now < WRAP_BASE); /* the clock really wrapped */
}

static void test_producer_timeout_across_wrap(void) {
  intent_fixture_t f;
  fixture_init(&f, WRAP_BASE);
  /* 500 ms after the heartbeat (past zero): alive. */
  cv2_intent_event_t ev = fixture_click(1u, WRAP_BASE + 500u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, WRAP_BASE + 500u), 1u);
  fixture_init(&f, WRAP_BASE);
  /* 501 ms after: dead. */
  ev = fixture_click(1u, WRAP_BASE + 501u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, WRAP_BASE + 501u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
  /* Tick judgement uses the same math. */
  fixture_init(&f, WRAP_BASE);
  cv2_action_t a = fixture_arbitrate(&f, NULL, WRAP_BASE + 500u);
  TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
  a = fixture_arbitrate(&f, NULL, WRAP_BASE + 501u);
  ASSERT_REJECTED(a, CV2_FAULT_PRODUCER_UNHEALTHY);
}

static void test_seq_wrap_stays_after(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(0xFFFFFFFFu, 200u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 200u), 0xFFFFFFFFu);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 1800u, 0u);
  ev = fixture_click(0u, 1800u); /* wrapped seq is "after" 0xFFFFFFFF */
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 1800u), 0u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 3400u, 0u);
  ev = fixture_click(0xFFFFFFFEu, 3400u); /* and the old one is stale */
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 3400u), CV2_FAULT_STALE);
}

static void test_queue_latch_across_wrap(void) {
  intent_fixture_t f;
  fixture_init(&f, WRAP_BASE);
  f.ctx.queue_fault = true;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, WRAP_BASE), CV2_FAULT_QUEUE_FAULT);
  f.ctx.queue_fault = false;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, WRAP_BASE + 1499u),
                  CV2_FAULT_QUEUE_FAULT);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, WRAP_BASE + 1500u, 0u);
  const cv2_action_t a = fixture_arbitrate(&f, NULL, WRAP_BASE + 1500u);
  TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
}

CV2_UNITY_MAIN(
  RUN_TEST(test_stale_and_fresh_across_wrap);
  RUN_TEST(test_expired_across_wrap);
  RUN_TEST(test_cooldown_across_wrap);
  RUN_TEST(test_producer_timeout_across_wrap);
  RUN_TEST(test_seq_wrap_stays_after);
  RUN_TEST(test_queue_latch_across_wrap);
)
