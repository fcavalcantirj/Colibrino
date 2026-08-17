/*
 * Negative oracle: every way an event can be wrong yields kind NONE,
 * release_all = 1 and the exact fault id. Each case breaks one predicate of
 * the known-good fixture (heartbeat at 100 ms, event seq 1 at t = 200 ms,
 * arbitrated at now = 200 ms).
 */
#include "colibrino/v2/access_intent.h"
#include "support/intent_fixture.h"
#include "support/unity_main.h"

static void test_fixture_baseline_grants(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  const cv2_action_t a = fixture_arbitrate(&f, &ev, 200u);
  ASSERT_GRANTED_CLICK(a, 1u);
}

static void test_malformed_magic(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.hdr.magic = 0xA1C2u;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_MALFORMED);
}

static void test_malformed_version(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.hdr.version = 2u;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_MALFORMED);
}

static void test_malformed_size(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.hdr.size = 16u;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_MALFORMED);
}

static void test_kind_out_of_range(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.hdr.kind = (uint8_t)(CV2_INTENT_KIND_MAX + 1);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_MALFORMED);
  ev.hdr.kind = (uint8_t)CV2_INTENT_NONE;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_MALFORMED);
}

static void test_ttl_beyond_horizon_is_malformed(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.ttl_ms = (uint16_t)(CV2_MAX_TTL_MS + 1u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_MALFORMED);
}

static void test_unknown_producer(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.hdr.producer_id = CV2_PRODUCER_COUNT;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u),
                  CV2_FAULT_PRODUCER_UNKNOWN);
  ev.hdr.producer_id = 0xFFFFu;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u),
                  CV2_FAULT_PRODUCER_UNKNOWN);
  ev.hdr.producer_id = CV2_PRODUCER_NONE;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u),
                  CV2_FAULT_PRODUCER_UNKNOWN);
}

static void test_disabled_producer(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.ctx.enabled_producers_mask = CV2_PRODUCER_BIT(CV2_PRODUCER_IMU_MOTION);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u),
                  CV2_FAULT_PRODUCER_DISABLED);
}

static void test_stale_out_of_order_seq(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t first = fixture_click(5u, 200u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &first, 200u), 5u);
  /* Older sequence with a newer timestamp: still stale. */
  cv2_intent_event_t older = fixture_click(4u, 2000u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &older, 2000u), CV2_FAULT_STALE);
  /* Newer sequence but a timestamp that does not advance: stale. */
  cv2_intent_event_t same_t = fixture_click(6u, 200u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &same_t, 2000u), CV2_FAULT_STALE);
}

static void test_stale_too_old(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  /* max_event_age is 100 ms; 101 ms old is stale. */
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 301u), CV2_FAULT_STALE);
}

static void test_stale_from_the_future(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(1u, 250u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_STALE);
}

static void test_expired(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.cfg.max_event_age_ms = 1000u; /* isolate expiry from the freshness bound */
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.ttl_ms = 50u;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 251u), CV2_FAULT_EXPIRED);
}

static void test_duplicate(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 200u), 1u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_DUPLICATE);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 210u), CV2_FAULT_DUPLICATE);
}

static void test_low_confidence(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.confidence = (uint8_t)(f.cfg.min_confidence - 1u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_LOW_CONFIDENCE);
}

static void test_unarmed(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.ctx.armed = false;
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_UNARMED);
}

static void test_uncalibrated(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.ctx.calibrated = false;
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_UNCALIBRATED);
}

static void test_unhealthy_timeout(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  /* Last heartbeat 100, timeout 500: an event at 700 finds a dead producer
   * (the event itself must not count as its own liveness proof). */
  const cv2_intent_event_t ev = fixture_click(1u, 700u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 700u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
}

static void test_unhealthy_never_seen(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ev.hdr.producer_id = CV2_PRODUCER_IMU_MOTION; /* enabled, never heartbeat */
  ev.hdr.kind = CV2_INTENT_POINTER_MOVE;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
}

static void test_unhealthy_fault_flag(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, 150u, 0x01u);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
}

static void test_queue_fault_latch(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.ctx.queue_fault = true;
  cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_QUEUE_FAULT);
  /* Cleared by the host, but the latch holds for one cooldown. */
  f.ctx.queue_fault = false;
  ev = fixture_click(2u, 300u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 300u), CV2_FAULT_QUEUE_FAULT);
}

static void test_disconnected(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.ctx.transport_connected = false;
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_DISCONNECTED);
}

static void test_low_battery(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  f.ctx.battery_ok = false;
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_LOW_BATTERY);
}

static void test_context_fault_priority(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  f.ctx.transport_connected = false;
  f.ctx.battery_ok = false;
  f.ctx.armed = false;
  f.ctx.calibrated = false;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_DISCONNECTED);
  f.ctx.transport_connected = true;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_LOW_BATTERY);
  f.ctx.battery_ok = true;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_UNARMED);
  f.ctx.armed = true;
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 200u), CV2_FAULT_UNCALIBRATED);
}

static void test_null_arguments_fail_closed(void) {
  intent_fixture_t f;
  fixture_init(&f, 100u);
  const cv2_intent_event_t ev = fixture_click(1u, 200u);
  ASSERT_REJECTED(cv2_intent_arbitrate(NULL, &f.ctx, &ev, 200u, &f.cfg),
                  CV2_FAULT_MALFORMED);
  ASSERT_REJECTED(cv2_intent_arbitrate(&f.st, NULL, &ev, 200u, &f.cfg),
                  CV2_FAULT_MALFORMED);
  ASSERT_REJECTED(cv2_intent_arbitrate(&f.st, &f.ctx, &ev, 200u, NULL),
                  CV2_FAULT_MALFORMED);
}

CV2_UNITY_MAIN(
  RUN_TEST(test_fixture_baseline_grants);
  RUN_TEST(test_malformed_magic);
  RUN_TEST(test_malformed_version);
  RUN_TEST(test_malformed_size);
  RUN_TEST(test_kind_out_of_range);
  RUN_TEST(test_ttl_beyond_horizon_is_malformed);
  RUN_TEST(test_unknown_producer);
  RUN_TEST(test_disabled_producer);
  RUN_TEST(test_stale_out_of_order_seq);
  RUN_TEST(test_stale_too_old);
  RUN_TEST(test_stale_from_the_future);
  RUN_TEST(test_expired);
  RUN_TEST(test_duplicate);
  RUN_TEST(test_low_confidence);
  RUN_TEST(test_unarmed);
  RUN_TEST(test_uncalibrated);
  RUN_TEST(test_unhealthy_timeout);
  RUN_TEST(test_unhealthy_never_seen);
  RUN_TEST(test_unhealthy_fault_flag);
  RUN_TEST(test_queue_fault_latch);
  RUN_TEST(test_disconnected);
  RUN_TEST(test_low_battery);
  RUN_TEST(test_context_fault_priority);
  RUN_TEST(test_null_arguments_fail_closed);
)
