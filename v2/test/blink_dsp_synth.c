/*
 * blink-dsp synthetic oracle (rung 3 + 4). Stage 1 is driven with exact
 * channel outputs (residual / head rate) at 5 ms cadence so every gate is
 * asserted at its boundary; stage 0 is checked against the EMA formula; the
 * combined dsp is fed the sticks3 synthetic still / impulse patterns.
 */
#include <math.h>
#include <string.h>

#include "colibrino/v2/blink_dsp.h"
#include "support/blink_fixture.h"
#include "support/unity_main.h"

/* Stage-1 state after the seed sample and a full 2 s quiet gate at t = 0. */
static void primed_stage1(cv2_blink_impulse_state_t *st,
                          cv2_blink_dsp_config_t *cfg, uint32_t *now_ms) {
  cv2_blink_dsp_config_defaults(cfg);
  cv2_blink_impulse_reset(st);
  *now_ms = 0u;
  const cv2_blink_channel_out_t seed = ch_seed();
  const cv2_blink_event_t ev = cv2_blink_dsp_step(st, cfg, *now_ms, &seed);
  TEST_ASSERT_EQUAL_UINT8(CV2_BLINK_CANCEL, ev.hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(CV2_BLINK_REASON_QUIET_GATE, ev.reason);
  *now_ms += 5u;
  feed_stage1(st, cfg, now_ms, 2100u, 0.1f, 0.2f, NULL);
}

static void test_first_sample_opens_quiet_gate(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  cv2_blink_dsp_config_defaults(&cfg);
  cv2_blink_impulse_reset(&st);
  const cv2_blink_channel_out_t seed = ch_seed();
  cv2_blink_event_t ev = cv2_blink_dsp_step(&st, &cfg, 1000u, &seed);
  TEST_ASSERT_EQUAL_UINT8(CV2_BLINK_CANCEL, ev.hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(CV2_BLINK_REASON_QUIET_GATE, ev.reason);
  TEST_ASSERT_TRUE(st.head_suppressed);
  /* A clean impulse 1 s later is inside the gate: nothing at all. */
  uint32_t now = 2000u;
  blink_tally_t t;
  tally_reset(&t);
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.2f, &t);
  feed_stage1(&st, &cfg, &now, 200u, 0.1f, 0.2f, &t);
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  TEST_ASSERT_EQUAL_UINT32(0u, st.accepted_impulses);
  /* At exactly 2000 ms after the seed the gate opens. */
  now = 3000u;
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.2f, &t);
  feed_stage1(&st, &cfg, &now, 50u, 0.1f, 0.2f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
}

static void test_still_never_emits(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  uint32_t now;
  primed_stage1(&st, &cfg, &now);
  blink_tally_t t;
  tally_reset(&t);
  feed_stage1(&st, &cfg, &now, 10000u, 0.59f, 1.0f, &t); /* below enter */
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  for (unsigned r = 0; r < 8u; ++r) {
    TEST_ASSERT_EQUAL_UINT32(0u, t.cancels[r]);
  }
}

static void test_impulse_emits_once_with_duration(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  uint32_t now;
  primed_stage1(&st, &cfg, &now);
  blink_tally_t t;
  tally_reset(&t);
  const uint32_t start = now;
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t); /* enters at start */
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  feed_stage1(&st, &cfg, &now, 5u, 0.6f, 0.5f, &t); /* exit is inclusive */
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
  TEST_ASSERT_EQUAL_UINT16(50u, t.last_impulse.duration_ms);
  TEST_ASSERT_EQUAL_UINT32(start + 50u, t.last_impulse.hdr.t_ms);
  TEST_ASSERT_EQUAL_UINT32(1u, st.accepted_impulses);
  feed_stage1(&st, &cfg, &now, 1000u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
}

static void test_spike_below_minimum_is_cancelled(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  uint32_t now;
  primed_stage1(&st, &cfg, &now);
  blink_tally_t t;
  tally_reset(&t);
  feed_stage1(&st, &cfg, &now, 15u, 2.0f, 0.5f, &t); /* 15 ms above */
  feed_stage1(&st, &cfg, &now, 100u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  TEST_ASSERT_EQUAL_UINT32(1u, t.cancels[CV2_BLINK_REASON_DURATION]);
  TEST_ASSERT_EQUAL_UINT32(0u, st.accepted_impulses);
  /* Exactly the minimum (20 ms) is accepted. */
  feed_stage1(&st, &cfg, &now, 20u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 100u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
  TEST_ASSERT_EQUAL_UINT16(20u, t.last_impulse.duration_ms);
}

static void test_hold_beyond_maximum_cancels(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  uint32_t now;
  primed_stage1(&st, &cfg, &now);
  blink_tally_t t;
  tally_reset(&t);
  const uint32_t start = now;
  feed_stage1(&st, &cfg, &now, 320u, 2.0f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.cancels[CV2_BLINK_REASON_OVERLONG]);
  TEST_ASSERT_EQUAL_UINT32(start + 305u, t.last_cancel.hdr.t_ms);
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  /* v1 parity: after the overlong cancel a new impulse re-opens on the next
   * sample (310); releasing at 320 gives a 10 ms tail -> duration cancel. */
  feed_stage1(&st, &cfg, &now, 5u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.cancels[CV2_BLINK_REASON_DURATION]);
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  TEST_ASSERT_EQUAL_UINT32(0u, st.accepted_impulses);
  /* Exactly the maximum (300 ms) is still accepted. */
  feed_stage1(&st, &cfg, &now, 1000u, 0.1f, 0.5f, NULL);
  feed_stage1(&st, &cfg, &now, 300u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 5u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
  TEST_ASSERT_EQUAL_UINT16(300u, t.last_impulse.duration_ms);
}

static void test_head_motion_cancels_and_suppresses_two_seconds(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  uint32_t now;
  primed_stage1(&st, &cfg, &now);
  blink_tally_t t;
  tally_reset(&t);
  /* Open an impulse, then a head burst: CANCEL(HEAD_MOTION), impulse gone. */
  feed_stage1(&st, &cfg, &now, 30u, 2.0f, 0.5f, &t);
  TEST_ASSERT_TRUE(st.impulse_active);
  const uint32_t burst = now;
  feed_stage1(&st, &cfg, &now, 5u, 0.0f, 2.6f, &t); /* > 2.5 dps gate */
  TEST_ASSERT_EQUAL_UINT32(1u, t.cancels[CV2_BLINK_REASON_HEAD_MOTION]);
  TEST_ASSERT_FALSE(st.impulse_active);
  TEST_ASSERT_TRUE(st.head_suppressed);
  /* Clean impulses within 2 s of the burst are ignored entirely. */
  feed_stage1(&st, &cfg, &now, 1000u, 0.1f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 800u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  /* Exactly 2000 ms after the burst the gate is open again. */
  now = burst + 2000u;
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 5u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
  /* Exactly the gate value (2.5) is NOT head motion. */
  feed_stage1(&st, &cfg, &now, 500u, 0.1f, 2.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.cancels[CV2_BLINK_REASON_HEAD_MOTION]);
}

static void test_impulse_refractory(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  uint32_t now;
  primed_stage1(&st, &cfg, &now);
  blink_tally_t t;
  tally_reset(&t);
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 5u, 0.1f, 0.5f, &t); /* IMPULSE at end1 */
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
  const uint32_t end1 = t.last_impulse.hdr.t_ms;
  /* A second impulse 100 ms later is inside the 300 ms refractory: not even
   * opened. */
  now = end1 + 100u;
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 5u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
  TEST_ASSERT_FALSE(st.impulse_active);
  /* One that starts exactly 300 ms after end1 is accepted. */
  now = end1 + 300u;
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 5u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(2u, t.impulses);
  TEST_ASSERT_EQUAL_UINT32(end1 + 350u, t.last_impulse.hdr.t_ms);
}

static void test_external_hold_ignores_and_does_not_count(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  uint32_t now;
  primed_stage1(&st, &cfg, &now);
  blink_tally_t t;
  tally_reset(&t);
  const uint32_t hold_at = now;
  cv2_blink_impulse_hold(&st, hold_at, 1500u);
  for (int k = 0; k < 3; ++k) {
    feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t);
    feed_stage1(&st, &cfg, &now, 350u, 0.1f, 0.5f, &t);
  }
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  TEST_ASSERT_EQUAL_UINT32(0u, st.accepted_impulses);
  now = hold_at + 1500u;
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 5u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
  TEST_ASSERT_FALSE(st.hold_active);
}

static void test_event_fields(void) {
  cv2_blink_impulse_state_t st;
  cv2_blink_dsp_config_t cfg;
  cv2_blink_dsp_config_defaults(&cfg);
  cv2_blink_impulse_reset(&st);
  const cv2_blink_channel_out_t seed = ch_seed();
  cv2_blink_event_t ev = cv2_blink_dsp_step(&st, &cfg, 7u, &seed);
  TEST_ASSERT_EQUAL_HEX16(CV2_EVENT_MAGIC, ev.hdr.magic);
  TEST_ASSERT_EQUAL_UINT8(CV2_EVENT_VERSION, ev.hdr.version);
  TEST_ASSERT_EQUAL_UINT16(CV2_PRODUCER_BLINK_IMU, ev.hdr.producer_id);
  TEST_ASSERT_EQUAL_UINT16(CV2_EVENT_SIZE, ev.hdr.size);
  TEST_ASSERT_EQUAL_UINT32(1u, ev.hdr.seq);
  TEST_ASSERT_EQUAL_UINT32(7u, ev.hdr.t_ms);
  TEST_ASSERT_EQUAL_UINT16(CV2_FEEL_TTL_IMPULSE_MS, ev.ttl_ms);
  TEST_ASSERT_EQUAL_INT(CV2_HDR_OK,
                        cv2_header_validate(&ev.hdr, CV2_EVENT_SIZE,
                                            CV2_BLINK_KIND_MAX, ev.ttl_ms));
  uint32_t now = 12u;
  blink_tally_t t;
  tally_reset(&t);
  feed_stage1(&st, &cfg, &now, 2100u, 0.1f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 50u, 2.0f, 0.5f, &t);
  feed_stage1(&st, &cfg, &now, 5u, 0.1f, 0.5f, &t);
  TEST_ASSERT_EQUAL_UINT32(2u, t.last_impulse.hdr.seq); /* seq per event */
  TEST_ASSERT_EQUAL_UINT8(CV2_BLINK_REASON_NONE, t.last_impulse.reason);
  TEST_ASSERT_EQUAL_UINT8(CV2_FEEL_BLINK_IMPULSE_CONFIDENCE,
                          t.last_impulse.confidence);
  /* NONE outputs are all-zero. */
  const cv2_blink_channel_out_t quiet = ch_out(0.1f, 0.5f);
  ev = cv2_blink_dsp_step(&st, &cfg, now, &quiet);
  cv2_blink_event_t zero;
  memset(&zero, 0, sizeof zero);
  TEST_ASSERT_EQUAL_MEMORY(&zero, &ev, sizeof ev);
}

static void test_stage0_seed_and_baseline_tracks_drift(void) {
  cv2_blink_channel_imu_state_t ch;
  cv2_blink_dsp_config_t cfg;
  cv2_blink_dsp_config_defaults(&cfg);
  cv2_blink_channel_imu_reset(&ch);
  const float g0[3] = {0.3f, -0.2f, 0.1f};
  cv2_blink_channel_out_t o = cv2_blink_channel_imu_update(&ch, &cfg, 0u, g0);
  TEST_ASSERT_FALSE(o.valid);
  TEST_ASSERT_EQUAL_FLOAT(0.3f, ch.baseline_dps[0]);
  TEST_ASSERT_EQUAL_FLOAT(-0.2f, ch.baseline_dps[1]);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, ch.baseline_dps[2]);
  /* A slow constant offset is absorbed: residual decays, head rate stays. */
  const float g1[3] = {0.5f, 0.0f, 0.0f};
  uint32_t now = 5u;
  float first_residual = 0.0f;
  for (int i = 0; i < 600; ++i) { /* 3 s */
    o = cv2_blink_channel_imu_update(&ch, &cfg, now, g1);
    TEST_ASSERT_TRUE(o.valid);
    if (i == 0) {
      first_residual = o.residual_dps;
    }
    now += 5u;
  }
  TEST_ASSERT_TRUE(first_residual > 0.2f);
  TEST_ASSERT_TRUE(o.residual_dps < 0.001f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, ch.baseline_dps[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, o.head_rate_dps);
}

static void test_stage0_dt_clamp_after_gap(void) {
  cv2_blink_channel_imu_state_t ch;
  cv2_blink_dsp_config_t cfg;
  cv2_blink_dsp_config_defaults(&cfg);
  cv2_blink_channel_imu_reset(&ch);
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  const float one[3] = {1.0f, 0.0f, 0.0f};
  (void)cv2_blink_channel_imu_update(&ch, &cfg, 0u, zero);
  /* dt = 5 -> alpha = 5/355. */
  cv2_blink_channel_out_t o = cv2_blink_channel_imu_update(&ch, &cfg, 5u, one);
  float expect = 0.0f + (5.0f / (350.0f + 5.0f)) * (1.0f - 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(expect, ch.baseline_dps[0]);
  TEST_ASSERT_EQUAL_FLOAT(1.0f - expect, o.residual_dps);
  /* dt = 10 s -> clamped to 50 -> alpha = 50/400, not ~1. */
  o = cv2_blink_channel_imu_update(&ch, &cfg, 10005u, one);
  expect = expect + (50.0f / (350.0f + 50.0f)) * (1.0f - expect);
  TEST_ASSERT_EQUAL_FLOAT(expect, ch.baseline_dps[0]);
  TEST_ASSERT_TRUE(o.residual_dps > 0.8f);
  /* dt = 0 -> clamped to 1. */
  o = cv2_blink_channel_imu_update(&ch, &cfg, 10005u, one);
  expect = expect + (1.0f / (350.0f + 1.0f)) * (1.0f - expect);
  TEST_ASSERT_EQUAL_FLOAT(expect, ch.baseline_dps[0]);
  /* Time going backwards (wrap) is a huge unsigned dt -> clamped to 50. */
  o = cv2_blink_channel_imu_update(&ch, &cfg, 10000u, one);
  expect = expect + (50.0f / (350.0f + 50.0f)) * (1.0f - expect);
  TEST_ASSERT_EQUAL_FLOAT(expect, ch.baseline_dps[0]);
}

static void test_combined_still_never_emits(void) {
  cv2_blink_pipeline_state_t p;
  cv2_blink_pipeline_init(&p, NULL, NULL);
  uint32_t now = 0u;
  blink_tally_t t;
  tally_reset(&t);
  feed_still(&p, &now, 6000u, &t, NULL);
  TEST_ASSERT_EQUAL_UINT32(0u, t.impulses);
  TEST_ASSERT_EQUAL_UINT32(0u, p.impulses);
  TEST_ASSERT_EQUAL_UINT32(1u, t.cancels[CV2_BLINK_REASON_QUIET_GATE]);
  TEST_ASSERT_EQUAL_UINT32(0u, t.cancels[CV2_BLINK_REASON_HEAD_MOTION]);
}

static void test_combined_bipolar_impulse_emits_one(void) {
  cv2_blink_pipeline_state_t p;
  cv2_blink_pipeline_init(&p, NULL, NULL);
  uint32_t now = 0u;
  blink_tally_t t;
  tally_reset(&t);
  feed_still(&p, &now, 2100u, &t, NULL);
  feed_impulse(&p, &now, &t, NULL);
  feed_still(&p, &now, 400u, &t, NULL);
  TEST_ASSERT_EQUAL_UINT32(1u, t.impulses);
  TEST_ASSERT_EQUAL_UINT32(1u, p.impulses);
  TEST_ASSERT_TRUE(t.last_impulse.duration_ms >= 20u);
  TEST_ASSERT_TRUE(t.last_impulse.duration_ms <= 300u);
}

static void test_deterministic(void) {
  cv2_blink_event_t a[64];
  cv2_blink_event_t b[64];
  for (int run = 0; run < 2; ++run) {
    cv2_blink_dsp_state_t st;
    cv2_blink_dsp_config_t cfg;
    cv2_blink_dsp_config_defaults(&cfg);
    cv2_blink_dsp_reset(&st);
    cv2_blink_event_t *out = run == 0 ? a : b;
    uint32_t n = 0u;
    uint32_t now = 0u;
    for (int i = 0; i < 2000 && n < 64u; ++i) {
      const float k = (float)(i % 97) / 97.0f;
      const float g[3] = {(i % 400) < 8 ? 2.5f : 0.1f * k, 0.05f * k,
                          (i % 1300) == 7 ? 9.0f : 0.0f};
      const cv2_blink_event_t ev = cv2_blink_dsp_update(&st, &cfg, now, g);
      if (ev.hdr.kind != CV2_BLINK_NONE) {
        out[n++] = ev;
      }
      now += (i % 50 == 49) ? 40u : 5u;
    }
    TEST_ASSERT_TRUE(n > 4u);
    if (run == 1) {
      TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(cv2_blink_event_t) * n);
    }
  }
}

CV2_UNITY_MAIN(
  RUN_TEST(test_first_sample_opens_quiet_gate);
  RUN_TEST(test_still_never_emits);
  RUN_TEST(test_impulse_emits_once_with_duration);
  RUN_TEST(test_spike_below_minimum_is_cancelled);
  RUN_TEST(test_hold_beyond_maximum_cancels);
  RUN_TEST(test_head_motion_cancels_and_suppresses_two_seconds);
  RUN_TEST(test_impulse_refractory);
  RUN_TEST(test_external_hold_ignores_and_does_not_count);
  RUN_TEST(test_event_fields);
  RUN_TEST(test_stage0_seed_and_baseline_tracks_drift);
  RUN_TEST(test_stage0_dt_clamp_after_gap);
  RUN_TEST(test_combined_still_never_emits);
  RUN_TEST(test_combined_bipolar_impulse_emits_one);
  RUN_TEST(test_deterministic);
)
