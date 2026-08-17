/*
 * blink pipeline facade oracle: streaming step == blink_detect(n = 1), the
 * window form returns the last non-NONE gesture, reset semantics, the click
 * refractory hold reaching blink-dsp, and the event ttl / producer / seq
 * fields end to end.
 */
#include <string.h>

#include "colibrino/v2/blink_pipeline.h"
#include "support/blink_fixture.h"
#include "support/unity_main.h"

/* Builds the sticks3 double-pause-double pattern (after a 2.1 s quiet
 * lead-in) into buf; returns the sample count. */
static size_t build_pattern(cv2_imu_sample_t *buf, size_t cap,
                            uint32_t start_ms) {
  size_t n = 0u;
  uint32_t now = start_ms;
  const uint32_t quiet[5] = {2100u, 400u, 900u, 400u, 400u};
  for (int seg = 0; seg < 5; ++seg) {
    for (uint32_t e = 0; e < quiet[seg]; e += 5u) {
      TEST_ASSERT_TRUE(n < cap);
      const float noise = (e % 20u == 0u) ? 0.12f : -0.06f;
      memset(&buf[n], 0, sizeof buf[n]);
      buf[n].t_ms = now;
      buf[n].gyro_dps[0] = noise;
      buf[n].gyro_dps[1] = -noise;
      ++n;
      now += 5u;
    }
    if (seg == 4) {
      break;
    }
    for (int index = 0; index < 12; ++index) {
      TEST_ASSERT_TRUE(n < cap);
      const float value = index < 4 ? -1.35f : (index < 8 ? 0.95f : 0.10f);
      memset(&buf[n], 0, sizeof buf[n]);
      buf[n].t_ms = now;
      buf[n].gyro_dps[0] = value;
      buf[n].gyro_dps[1] = 0.2f * value;
      buf[n].gyro_dps[2] = -0.1f * value;
      ++n;
      now += 5u;
    }
  }
  return n;
}

#define PATTERN_CAP 1200u

static void test_step_equals_blink_detect_n1(void) {
  static cv2_imu_sample_t buf[PATTERN_CAP];
  const size_t n = build_pattern(buf, PATTERN_CAP, 0u);
  cv2_blink_pipeline_state_t a;
  cv2_blink_pipeline_state_t b;
  cv2_blink_pipeline_init(&a, NULL, NULL);
  cv2_blink_pipeline_init(&b, NULL, NULL);
  uint32_t clicks = 0u;
  for (size_t i = 0; i < n; ++i) {
    const cv2_gesture_event_t ga = cv2_blink_pipeline_step(&a, &buf[i], NULL);
    const cv2_gesture_event_t gb = blink_detect(&buf[i], 1u, &b);
    TEST_ASSERT_EQUAL_MEMORY(&ga, &gb, sizeof ga);
    TEST_ASSERT_EQUAL_UINT32(a.impulses, b.impulses);
    TEST_ASSERT_EQUAL_UINT32(a.clicks, b.clicks);
    if (ga.hdr.kind == CV2_GESTURE_CLICK_CANDIDATE) {
      ++clicks;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, clicks);
  TEST_ASSERT_EQUAL_UINT32(4u, a.impulses);
  TEST_ASSERT_EQUAL_UINT32(1u, a.clicks);
}

static void test_window_returns_last_non_none_event(void) {
  static cv2_imu_sample_t buf[PATTERN_CAP];
  const size_t n = build_pattern(buf, PATTERN_CAP, 500u);
  cv2_blink_pipeline_state_t p;
  cv2_blink_pipeline_init(&p, NULL, NULL);
  const cv2_gesture_event_t g = blink_detect(buf, n, &p);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_CLICK_CANDIDATE, g.hdr.kind);
  TEST_ASSERT_EQUAL_UINT32(1u, p.clicks);
  /* The click is stamped at the fourth impulse, well before the window end. */
  TEST_ASSERT_TRUE(g.hdr.t_ms < buf[n - 1u].t_ms);
  TEST_ASSERT_TRUE(g.hdr.t_ms > buf[0].t_ms + 2100u);
  /* A window with nothing in it returns an all-zero NONE. */
  cv2_gesture_event_t zero;
  memset(&zero, 0, sizeof zero);
  const cv2_gesture_event_t g2 = blink_detect(buf, 100u, &p);
  TEST_ASSERT_EQUAL_MEMORY(&zero, &g2, sizeof zero);
  /* NULL / empty inputs are NONE and leave state alone. */
  const uint32_t before = p.impulses;
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE, blink_detect(NULL, 5u, &p).hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE, blink_detect(buf, 0u, &p).hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE, blink_detect(buf, 5u, NULL).hdr.kind);
  TEST_ASSERT_EQUAL_UINT32(before, p.impulses);
}

static void test_reset_semantics(void) {
  static cv2_imu_sample_t buf[PATTERN_CAP];
  const size_t n = build_pattern(buf, PATTERN_CAP, 0u);
  cv2_blink_pipeline_state_t p;
  cv2_blink_pipeline_init(&p, NULL, NULL);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_CLICK_CANDIDATE,
                          blink_detect(buf, n, &p).hdr.kind);
  TEST_ASSERT_EQUAL_UINT32(1u, p.clicks);
  cv2_blink_pipeline_reset(&p);
  TEST_ASSERT_EQUAL_UINT32(0u, p.impulses);
  TEST_ASSERT_EQUAL_UINT32(0u, p.clicks);
  TEST_ASSERT_FALSE(p.dsp.channel.initialized);
  TEST_ASSERT_EQUAL_UINT32(1u, p.dsp.impulse.next_seq);
  TEST_ASSERT_EQUAL_UINT32(1u, p.code.next_seq);
  TEST_ASSERT_FALSE(p.code.click_suppressed);
  /* Configs survive a reset. */
  TEST_ASSERT_EQUAL_UINT32(1500u, p.code_cfg.click_refractory_ms);
  TEST_ASSERT_EQUAL_FLOAT(1.1f, p.dsp_cfg.impulse_enter_dps);
  /* After reset the quiet gate re-opens: the same pattern without its 2.1 s
   * lead-in cannot click, with the lead-in it clicks again with seq 1. */
  const size_t skip = 2100u / 5u; /* drop the lead-in */
  cv2_gesture_event_t g = blink_detect(buf + skip, n - skip, &p);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE, g.hdr.kind);
  cv2_blink_pipeline_reset(&p);
  g = blink_detect(buf, n, &p);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_CLICK_CANDIDATE, g.hdr.kind);
  TEST_ASSERT_EQUAL_UINT32(1u, g.hdr.seq);
}

static void test_click_refractory_holds_dsp(void) {
  cv2_blink_pipeline_state_t p;
  cv2_blink_pipeline_init(&p, NULL, NULL);
  uint32_t now = 0u;
  uint32_t clicks = 0u;
  blink_tally_t t;
  tally_reset(&t);
  feed_still(&p, &now, 2100u, &t, &clicks);
  for (int k = 0; k < 4; ++k) {
    feed_impulse(&p, &now, &t, &clicks);
    feed_still(&p, &now, k == 1 ? 900u : 400u, &t, &clicks);
  }
  TEST_ASSERT_EQUAL_UINT32(1u, clicks);
  TEST_ASSERT_EQUAL_UINT32(4u, p.impulses);
  TEST_ASSERT_TRUE(p.dsp.impulse.hold_active);
  TEST_ASSERT_EQUAL_UINT32(1500u, p.dsp.impulse.hold_ms);
  /* Impulses inside the 1500 ms click refractory are not detected at all
   * (v1 parity: acceptedImpulses does not move). */
  feed_impulse(&p, &now, &t, &clicks);
  feed_still(&p, &now, 400u, &t, &clicks);
  feed_impulse(&p, &now, &t, &clicks);
  TEST_ASSERT_EQUAL_UINT32(4u, p.impulses);
  TEST_ASSERT_EQUAL_UINT32(4u, t.impulses);
  /* Past the refractory they are counted again. */
  feed_still(&p, &now, 1600u, &t, &clicks);
  feed_impulse(&p, &now, &t, &clicks);
  feed_still(&p, &now, 100u, &t, &clicks);
  TEST_ASSERT_EQUAL_UINT32(5u, p.impulses);
  TEST_ASSERT_EQUAL_UINT32(1u, clicks);
}

static void test_ttl_producer_and_seq_fields(void) {
  static cv2_imu_sample_t buf[PATTERN_CAP];
  const size_t n = build_pattern(buf, PATTERN_CAP, 0u);
  cv2_blink_pipeline_state_t p;
  cv2_blink_pipeline_init(&p, NULL, NULL);
  cv2_blink_event_t last_impulse;
  memset(&last_impulse, 0, sizeof last_impulse);
  cv2_gesture_event_t click;
  memset(&click, 0, sizeof click);
  uint32_t blink_events = 0u;
  for (size_t i = 0; i < n; ++i) {
    cv2_blink_event_t b;
    const cv2_gesture_event_t g = cv2_blink_pipeline_step(&p, &buf[i], &b);
    if (b.hdr.kind != CV2_BLINK_NONE) {
      ++blink_events;
      TEST_ASSERT_EQUAL_UINT32(blink_events, b.hdr.seq); /* seq per producer */
      TEST_ASSERT_EQUAL_UINT16(CV2_PRODUCER_BLINK_IMU, b.hdr.producer_id);
      TEST_ASSERT_EQUAL_UINT16(CV2_FEEL_TTL_IMPULSE_MS, b.ttl_ms);
      TEST_ASSERT_EQUAL_UINT32(buf[i].t_ms, b.hdr.t_ms);
    }
    if (b.hdr.kind == CV2_BLINK_IMPULSE) {
      last_impulse = b;
    }
    if (g.hdr.kind == CV2_GESTURE_CLICK_CANDIDATE) {
      click = g;
      TEST_ASSERT_EQUAL_UINT32(buf[i].t_ms, g.hdr.t_ms);
    }
  }
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_CLICK_CANDIDATE, click.hdr.kind);
  TEST_ASSERT_EQUAL_UINT16(CV2_PRODUCER_BLINK_CODE, click.hdr.producer_id);
  TEST_ASSERT_EQUAL_UINT16(CV2_FEEL_TTL_CLICK_MS, click.ttl_ms);
  TEST_ASSERT_EQUAL_UINT32(1u, click.hdr.seq);
  TEST_ASSERT_EQUAL_UINT8(4u, click.impulses);
  /* The click is stamped at the fourth impulse's end. */
  TEST_ASSERT_EQUAL_UINT32(last_impulse.hdr.t_ms, click.hdr.t_ms);
  TEST_ASSERT_EQUAL_UINT32(1u + 4u, blink_events); /* quiet-gate CANCEL + 4 */
}

static void test_custom_configs_are_used(void) {
  cv2_blink_dsp_config_t dsp;
  cv2_blink_code_config_t code;
  cv2_blink_dsp_config_defaults(&dsp);
  cv2_blink_code_config_defaults(&code);
  code.double_blink_maximum_ms = 350u; /* the sticks3 400 ms gap no longer fits */
  cv2_blink_pipeline_state_t p;
  cv2_blink_pipeline_init(&p, &dsp, &code);
  TEST_ASSERT_EQUAL_UINT32(350u, p.code_cfg.double_blink_maximum_ms);
  static cv2_imu_sample_t buf[PATTERN_CAP];
  const size_t n = build_pattern(buf, PATTERN_CAP, 0u);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE, blink_detect(buf, n, &p).hdr.kind);
  TEST_ASSERT_EQUAL_UINT32(4u, p.impulses);
  TEST_ASSERT_EQUAL_UINT32(0u, p.clicks);
}

CV2_UNITY_MAIN(
  RUN_TEST(test_step_equals_blink_detect_n1);
  RUN_TEST(test_window_returns_last_non_none_event);
  RUN_TEST(test_reset_semantics);
  RUN_TEST(test_click_refractory_holds_dsp);
  RUN_TEST(test_ttl_producer_and_seq_fields);
  RUN_TEST(test_custom_configs_are_used);
)
