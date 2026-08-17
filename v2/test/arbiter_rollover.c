/*
 * Timestamp and sequence wrap: every time comparison in the arbiter must
 * behave identically when device millis cross 0xFFFFFFFF -> 0 (every ~49.7
 * days of uptime), when a producer's seq wraps, and - the 2^31 horizon - when
 * a stored timestamp is 24.8 days or more behind now, where CV2_MS_DIFF alone
 * would invert its sign (arbiter state is aged to CV2_INTENT_STATE_HORIZON_MS
 * on every call so it never gets there).
 */
#include "colibrino/v2/access_intent.h"
#include "support/intent_fixture.h"
#include "support/unity_main.h"

#define WRAP_BASE 0xFFFFFF00u
#define HALF_RANGE 0x80000000u /* 2^31 ms: the CV2_MS_DIFF sign boundary */
#define HORIZON CV2_INTENT_STATE_HORIZON_MS

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

/* ---- the 2^31 horizon ---------------------------------------------------- */

/* Heartbeat 100 ms before now, then a fresh in-order click at now. */
static cv2_action_t fresh_click_at(intent_fixture_t *f, uint32_t seq,
                                   cv2_ms_t now) {
  cv2_intent_heartbeat(&f->st, CV2_PRODUCER_BLINK_CODE, now - 100u, 0u);
  const cv2_intent_event_t ev = fixture_click(seq, now);
  return fixture_arbitrate(f, &ev, now);
}

/* A producer that granted once and then sent nothing for 2^31 ms (24.8 days,
 * plausible for a rarely used click source) must not be locked out: its
 * next fresh event is in order, not STALE. Then one click per hour for a
 * day stays granted. */
static void test_ordering_survives_2_31_of_producer_silence(void) {
  intent_fixture_t f;
  fixture_init(&f, 900u);
  cv2_intent_event_t ev = fixture_click(1u, 1000u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 1000u), 1u);
  /* Largest positive distance, then exactly 2^31 further (the sign
   * boundary itself); a single hop of exactly 2^31; and well past it. */
  ASSERT_GRANTED_CLICK(fresh_click_at(&f, 2u, 1000u + HALF_RANGE - 1u), 2u);
  ASSERT_GRANTED_CLICK(fresh_click_at(&f, 3u, 1000u + 2u * HALF_RANGE - 1u),
                       3u);
  fixture_init(&f, 900u);
  ev = fixture_click(1u, 1000u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 1000u), 1u);
  ASSERT_GRANTED_CLICK(fresh_click_at(&f, 2u, 1000u + HALF_RANGE), 2u);
  fixture_init(&f, 900u);
  ev = fixture_click(1u, 1000u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 1000u), 1u);
  cv2_ms_t now = 1000u + HALF_RANGE + 5000u;
  ASSERT_GRANTED_CLICK(fresh_click_at(&f, 2u, now), 2u);
  for (uint32_t hour = 1u; hour <= 24u; ++hour) {
    now += 3600000u;
    ASSERT_GRANTED_CLICK(fresh_click_at(&f, 2u + hour, now), 2u + hour);
  }
}

/* Aging pins, it does not forget: past the horizon the seq memory of a
 * producer still rejects a replay (DUPLICATE) and a regression (STALE),
 * while a fresh in-order event is granted. */
static void test_ordering_memory_pinned_past_horizon(void) {
  intent_fixture_t f;
  fixture_init(&f, 900u);
  cv2_intent_event_t ev = fixture_click(7u, 1000u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 1000u), 7u);
  const cv2_ms_t now = 1000u + HORIZON + 1u;
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, now - 100u, 0u);
  ev = fixture_click(7u, now); /* replayed seq, fresh stamp */
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, now), CV2_FAULT_DUPLICATE);
  ev = fixture_click(6u, now); /* older seq, fresh stamp */
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, now), CV2_FAULT_STALE);
  ev = fixture_click(8u, now);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, now), 8u);
  /* The same three answers 2^31 ms later still. */
  const cv2_ms_t later = now + HALF_RANGE + 7u;
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, later - 100u, 0u);
  ev = fixture_click(8u, later);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, later), CV2_FAULT_DUPLICATE);
  ev = fixture_click(2u, later);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, later), CV2_FAULT_STALE);
  ev = fixture_click(9u, later);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, later), 9u);
}

/* A seen producer that fell silent stays unhealthy for ever: at 10 s, at the
 * last positive distance, at exactly 2^31, and 24.8 days beyond - on ticks
 * and on its own (fresh) event. Never fail-open. */
static void test_silent_producer_stays_unhealthy_past_2_31(void) {
  intent_fixture_t f;
  const cv2_ms_t at[] = {10000u, HALF_RANGE - 1u, HALF_RANGE, HALF_RANGE + 10u,
                         2u * HALF_RANGE - 1u, 3u * HALF_RANGE};
  /* Ticks carry the state across every horizon (ticks refresh nothing). */
  fixture_init(&f, 0u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_IMU_MOTION, 0u, 0u);
  for (size_t i = 0; i < sizeof at / sizeof at[0]; ++i) {
    ASSERT_REJECTED(fixture_arbitrate(&f, NULL, at[i]),
                    CV2_FAULT_PRODUCER_UNHEALTHY);
  }
  /* A single call after the gap is enough, on a tick and on the silent
   * producer's own (fresh, in-order) event: nothing in between. */
  for (size_t i = 0; i < sizeof at / sizeof at[0]; ++i) {
    fixture_init(&f, 0u); /* blink-code heartbeat at 0, then silence */
    ASSERT_REJECTED(fixture_arbitrate(&f, NULL, at[i]),
                    CV2_FAULT_PRODUCER_UNHEALTHY);
    fixture_init(&f, 0u);
    const cv2_intent_event_t ev = fixture_click(1u, at[i]);
    ASSERT_REJECTED(fixture_arbitrate(&f, &ev, at[i]),
                    CV2_FAULT_PRODUCER_UNHEALTHY);
  }
  /* Calls spaced just under 2^31 ms each see a positive distance, and the
   * aging at each of them is what keeps the fact alive across a full 2^32
   * wrap: heartbeat at 0, tick at 2^31 - 1, tick at 2^32 + 10 (== 10). A
   * lazy "negative distance = timed out" alone would read the last one as
   * a 10 ms old heartbeat and fail open. */
  fixture_init(&f, 0u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_IMU_MOTION, 0u, 0u);
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, HALF_RANGE - 1u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 10u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, HALF_RANGE + 9u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
  fixture_init(&f, 0u); /* same on the silent producer's own event */
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, HALF_RANGE - 1u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
  const cv2_intent_event_t late = fixture_click(1u, 10u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &late, 10u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
  /* Only a real heartbeat brings it back. */
  fixture_init(&f, 0u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_IMU_MOTION, 0u, 0u);
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, HALF_RANGE + 10u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_IMU_MOTION, HALF_RANGE + 20u, 0u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, HALF_RANGE + 20u, 0u);
  const cv2_action_t a = fixture_arbitrate(&f, NULL, HALF_RANGE + 30u);
  TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
  const cv2_intent_event_t ev = fixture_click(1u, HALF_RANGE + 30u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, HALF_RANGE + 30u), 1u);
}

/* A heartbeat stamped after now (skewed clock, or > 2^31 ms old - the two
 * are indistinguishable) counts as timed out, like a future event is STALE. */
static void test_future_heartbeat_reads_as_timed_out(void) {
  intent_fixture_t f;
  fixture_init(&f, 1001u);
  const cv2_intent_event_t ev = fixture_click(1u, 1000u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, 1000u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
  fixture_init(&f, 1001u);
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 1000u),
                  CV2_FAULT_PRODUCER_UNHEALTHY);
}

/* 24.8 days without a click is not a cooldown. */
static void test_cooldown_after_2_31_without_clicks(void) {
  intent_fixture_t f;
  fixture_init(&f, 0u);
  cv2_intent_event_t ev = fixture_click(1u, 0u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 0u), 1u);
  ASSERT_GRANTED_CLICK(fresh_click_at(&f, 2u, HALF_RANGE - 1u), 2u);
  fixture_init(&f, 0u);
  ev = fixture_click(1u, 0u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 0u), 1u);
  ASSERT_GRANTED_CLICK(fresh_click_at(&f, 2u, HALF_RANGE), 2u);
  fixture_init(&f, 0u);
  ev = fixture_click(1u, 0u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 0u), 1u);
  ASSERT_GRANTED_CLICK(fresh_click_at(&f, 2u, HALF_RANGE + 5000u), 2u);
  /* Bounded at each call, alive across the full wrap: click at 0, a tick at
   * 2^31 - 1, a click at 2^32 + 100 (== 100) is not "100 ms after". */
  fixture_init(&f, 0u);
  ev = fixture_click(1u, 0u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 0u), 1u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, HALF_RANGE - 1u, 0u);
  TEST_ASSERT_EQUAL_UINT8(
      0u, fixture_arbitrate(&f, NULL, HALF_RANGE - 1u).release_all);
  ASSERT_GRANTED_CLICK(fresh_click_at(&f, 2u, 100u), 2u);
  /* And a different, freshly announced producer sees no cooldown either. */
  fixture_init(&f, 0u);
  f.ctx.enabled_producers_mask |= CV2_PRODUCER_BIT(CV2_PRODUCER_SWITCH);
  ev = fixture_click(1u, 0u);
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, 0u), 1u);
  const cv2_ms_t now = HALF_RANGE + 5000u;
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_SWITCH, now - 100u, 0u);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, now - 100u, 0u);
  ev = fixture_click(1u, now);
  ev.hdr.producer_id = CV2_PRODUCER_SWITCH;
  ASSERT_GRANTED_CLICK(fixture_arbitrate(&f, &ev, now), 1u);
  /* The cooldown itself is intact right after the pin. */
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, now + 1000u, 0u);
  ev = fixture_click(2u, now + 1000u);
  ASSERT_REJECTED(fixture_arbitrate(&f, &ev, now + 1000u), CV2_FAULT_COOLDOWN);
}

/* A queue fault seen once and then cleared must not stay latched across the
 * horizon: it cools after one cooldown, and 2^31 ms later it is cold, not
 * "not yet". */
static void test_queue_latch_clears_past_2_31(void) {
  intent_fixture_t f;
  fixture_init(&f, 0u);
  f.ctx.queue_fault = true;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 0u), CV2_FAULT_QUEUE_FAULT);
  f.ctx.queue_fault = false;
  const cv2_ms_t at[] = {HALF_RANGE - 1u, HALF_RANGE, HALF_RANGE + 10u,
                         2u * HALF_RANGE - 1u};
  for (size_t i = 0; i < sizeof at / sizeof at[0]; ++i) {
    fixture_init(&f, 0u);
    f.ctx.queue_fault = true;
    ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 0u), CV2_FAULT_QUEUE_FAULT);
    f.ctx.queue_fault = false;
    cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, at[i], 0u);
    const cv2_action_t a = fixture_arbitrate(&f, NULL, at[i]);
    TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)CV2_FAULT_NONE, a.fault);
  }
  /* A fault that is STILL present past the horizon stays latched. */
  fixture_init(&f, 0u);
  f.ctx.queue_fault = true;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, 0u), CV2_FAULT_QUEUE_FAULT);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, HALF_RANGE + 10u, 0u);
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, HALF_RANGE + 10u),
                  CV2_FAULT_QUEUE_FAULT);
  f.ctx.queue_fault = false;
  ASSERT_REJECTED(fixture_arbitrate(&f, NULL, HALF_RANGE + 1509u),
                  CV2_FAULT_QUEUE_FAULT);
  cv2_intent_heartbeat(&f.st, CV2_PRODUCER_BLINK_CODE, HALF_RANGE + 1510u, 0u);
  const cv2_action_t a = fixture_arbitrate(&f, NULL, HALF_RANGE + 1510u);
  TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
}

CV2_UNITY_MAIN(
  RUN_TEST(test_stale_and_fresh_across_wrap);
  RUN_TEST(test_expired_across_wrap);
  RUN_TEST(test_cooldown_across_wrap);
  RUN_TEST(test_producer_timeout_across_wrap);
  RUN_TEST(test_seq_wrap_stays_after);
  RUN_TEST(test_queue_latch_across_wrap);
  RUN_TEST(test_ordering_survives_2_31_of_producer_silence);
  RUN_TEST(test_ordering_memory_pinned_past_horizon);
  RUN_TEST(test_silent_producer_stays_unhealthy_past_2_31);
  RUN_TEST(test_future_heartbeat_reads_as_timed_out);
  RUN_TEST(test_cooldown_after_2_31_without_clicks);
  RUN_TEST(test_queue_latch_clears_past_2_31);
)
