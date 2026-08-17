// Differential oracle (rung 5): the v2 pipeline (blink-dsp -> blink-code)
// must reproduce the sticks3 v1 ImuBlinkDetector sample for sample:
//   v1.update(t, gyro) == (gesture.kind == CLICK_CANDIDATE)
//   v1.acceptedImpulses() == pipeline.impulses      (after every sample)
//   v1.completedSequences() == pipeline.clicks       (after every sample)
// Inputs: the five sticks3 synthetic patterns, a >= 200k-sample xorshift32
// fuzz at 5 ms cadence with injected impulses, head bursts and dropout gaps,
// and a run whose timestamps start near 0xFFFFFF00 to cross the wrap.
// Both translation units are built with -ffp-contract=off.
#include <cstdint>
#include <cstring>

#include "colibrino/imu_blink_detector.h"
#include "colibrino/v2/blink_pipeline.h"
#include "support/unity_main.h"

namespace {

using colibrino::ImuBlinkDetector;
using colibrino::Vec3;

struct Pair {
  ImuBlinkDetector v1;
  cv2_blink_pipeline_state_t v2{};
  uint32_t samples = 0;
  uint32_t clicks = 0;
  uint32_t impulses = 0;

  Pair() { cv2_blink_pipeline_init(&v2, nullptr, nullptr); }

  // Feeds one sample to both and asserts equality of every observable.
  bool step(uint32_t t_ms, float gx, float gy, float gz) {
    const bool v1_click = v1.update(t_ms, Vec3{gx, gy, gz});
    cv2_imu_sample_t s{};
    s.t_ms = t_ms;
    s.gyro_dps[0] = gx;
    s.gyro_dps[1] = gy;
    s.gyro_dps[2] = gz;
    cv2_blink_event_t blink{};
    const cv2_gesture_event_t g = cv2_blink_pipeline_step(&v2, &s, &blink);
    const bool v2_click = g.hdr.kind == CV2_GESTURE_CLICK_CANDIDATE;
    ++samples;
    if (v1_click != v2_click || v1.acceptedImpulses() != v2.impulses ||
        v1.completedSequences() != v2.clicks) {
      UnityPrintNumber(static_cast<UNITY_INT>(t_ms));
      UnityPrint(" ms: v1 click=");
      UnityPrintNumber(v1_click);
      UnityPrint(" v2 click=");
      UnityPrintNumber(v2_click);
      UnityPrint(" v1 impulses=");
      UnityPrintNumber(static_cast<UNITY_INT>(v1.acceptedImpulses()));
      UnityPrint(" v2 impulses=");
      UnityPrintNumber(static_cast<UNITY_INT>(v2.impulses));
      UnityPrint(" v1 seq=");
      UnityPrintNumber(static_cast<UNITY_INT>(v1.completedSequences()));
      UnityPrint(" v2 seq=");
      UnityPrintNumber(static_cast<UNITY_INT>(v2.clicks));
      UNITY_OUTPUT_CHAR('\n');
      TEST_FAIL_MESSAGE("v1/v2 divergence");
    }
    if (v2_click) {
      ++clicks;
    }
    if (blink.hdr.kind == CV2_BLINK_IMPULSE) {
      ++impulses;
    }
    return v2_click;
  }
};

// ---- the five sticks3 synthetic patterns (mirrored from
// sticks3/test/test_imu_blink_detector/test_main.cpp) --------------------

void feedStill(Pair& p, uint32_t& now_ms, uint32_t duration_ms) {
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += 5) {
    const float noise = (elapsed % 20 == 0) ? 0.12f : -0.06f;
    p.step(now_ms, noise, -noise, 0.0f);
    now_ms += 5;
  }
}

void feedImpulse(Pair& p, uint32_t& now_ms, bool& emitted) {
  for (int index = 0; index < 12; ++index) {
    const float value = index < 4 ? -1.35f : (index < 8 ? 0.95f : 0.10f);
    emitted = p.step(now_ms, value, 0.2f * value, -0.1f * value) || emitted;
    now_ms += 5;
  }
}

void test_pattern_stillness_never_emits() {
  Pair p;
  uint32_t now = 0;
  feedStill(p, now, 6000);
  TEST_ASSERT_EQUAL_UINT32(0, p.v2.impulses);
  TEST_ASSERT_EQUAL_UINT32(0, p.v2.clicks);
}

void test_pattern_double_pause_double_emits_once() {
  Pair p;
  uint32_t now = 0;
  bool emitted = false;
  feedStill(p, now, 2100);
  feedImpulse(p, now, emitted);
  feedStill(p, now, 400);
  feedImpulse(p, now, emitted);
  feedStill(p, now, 900);
  feedImpulse(p, now, emitted);
  feedStill(p, now, 400);
  feedImpulse(p, now, emitted);
  TEST_ASSERT_TRUE(emitted);
  TEST_ASSERT_EQUAL_UINT32(4, p.v2.impulses);
  TEST_ASSERT_EQUAL_UINT32(1, p.v2.clicks);
}

void test_pattern_uniform_four_blinks_do_not_emit() {
  Pair p;
  uint32_t now = 0;
  bool emitted = false;
  feedStill(p, now, 2100);
  for (int blink = 0; blink < 4; ++blink) {
    feedImpulse(p, now, emitted);
    if (blink != 3) {
      feedStill(p, now, 400);
    }
  }
  TEST_ASSERT_FALSE(emitted);
  TEST_ASSERT_EQUAL_UINT32(0, p.v2.clicks);
}

void test_pattern_three_impulses_do_not_emit() {
  Pair p;
  uint32_t now = 0;
  bool emitted = false;
  feedStill(p, now, 2100);
  feedImpulse(p, now, emitted);
  feedStill(p, now, 400);
  feedImpulse(p, now, emitted);
  feedStill(p, now, 900);
  feedImpulse(p, now, emitted);
  TEST_ASSERT_FALSE(emitted);
  TEST_ASSERT_EQUAL_UINT32(0, p.v2.clicks);
}

void test_pattern_head_motion_cancels_partial_sequence() {
  Pair p;
  uint32_t now = 0;
  bool emitted = false;
  feedStill(p, now, 2100);
  feedImpulse(p, now, emitted);
  feedStill(p, now, 400);
  feedImpulse(p, now, emitted);
  feedStill(p, now, 900);
  p.step(now, 0.0f, 8.0f, 0.0f);
  now += 5;
  feedStill(p, now, 2100);
  feedImpulse(p, now, emitted);
  TEST_ASSERT_FALSE(emitted);
  TEST_ASSERT_EQUAL_UINT32(0, p.v2.clicks);
}

// ---- fuzz --------------------------------------------------------------

struct Rng {
  uint32_t s;
  uint32_t next() {
    uint32_t x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s = x;
    return x;
  }
  uint32_t below(uint32_t n) { return next() % n; }
  // Uniform in [-1, 1).
  float unit() { return static_cast<float>(below(20000u)) / 10000.0f - 1.0f; }
};

// A stochastic sample generator with regimes: quiet noise, blink-like
// bipolar impulses of random width, head bursts above the gate, cadence
// dropouts, and deliberately scripted double-pause-double patterns whose gaps
// straddle the 300/700/800/1400 ms windows (about half of them are valid), so
// that clicks, near-miss restarts and the "keep the newest pair" rule all
// occur many times in both machines.
struct Fuzzer {
  struct Step {
    uint32_t regime;  // 0 quiet, 1 impulse, 2 head burst, 3 clean impulse
    uint32_t count;   // samples
  };

  Rng rng;
  uint32_t t_ms;
  uint32_t regime = 0;
  uint32_t remaining = 0;
  float impulse_sign = 1.0f;
  float impulse_amp = 1.5f;
  Step queue[8] = {};
  uint32_t queued = 0;
  uint32_t queue_head = 0;

  Fuzzer(uint32_t seed, uint32_t start_ms) : rng{seed}, t_ms(start_ms) {}

  void enqueue(uint32_t r, uint32_t count) {
    queue[(queue_head + queued) % 8u] = Step{r, count};
    ++queued;
  }

  // Sample counts at the nominal 5 ms cadence.
  uint32_t msToSamples(uint32_t ms) { return ms / 5u; }

  void enqueueCodedPattern() {
    // Clean impulses 30..85 ms wide; intervals are measured end to end, so
    // the quiet gap plus the next width lands mostly inside each window and
    // sometimes just outside it (near-miss restarts are wanted too).
    for (int k = 0; k < 4; ++k) {
      enqueue(3u, 6u + rng.below(12u));
      if (k == 0 || k == 2) {
        enqueue(0u, msToSamples(230u + rng.below(420u)));  // 230..650
      } else if (k == 1) {
        enqueue(0u, msToSamples(740u + rng.below(620u)));  // 740..1360
      }
    }
  }

  void chooseRegime() {
    if (queued == 0u) {
      const uint32_t r = rng.below(100u);
      if (r < 14u) {
        // Quiet lead-in (often past the 2 s gate), then a coded pattern.
        enqueue(0u, msToSamples(300u + rng.below(2600u)));
        enqueueCodedPattern();
      } else if (r < 62u) {
        enqueue(0u, 4u + rng.below(280u));  // 20 ms .. 1.4 s of quiet
      } else if (r < 96u) {
        enqueue(1u, 1u + rng.below(80u));   // 5 ms .. 400 ms above exit
      } else {
        enqueue(2u, 1u + rng.below(60u));   // head burst
      }
    }
    const Step s = queue[queue_head];
    queue_head = (queue_head + 1u) % 8u;
    --queued;
    regime = s.regime;
    remaining = s.count == 0u ? 1u : s.count;
    if (regime == 1u || regime == 3u) {
      impulse_sign = rng.below(2u) ? 1.0f : -1.0f;
      // Clean temple impulses (1.3..2.2 dps); random regime-1 impulses are
      // weak (0.4..1.2 dps, hovering around enter/exit) 30% of the time.
      const bool weak = regime == 1u && rng.below(10u) >= 7u;
      impulse_amp = weak ? 0.4f + 0.8f * (0.5f + 0.5f * rng.unit())
                         : 1.3f + 0.9f * (0.5f + 0.5f * rng.unit());
    }
  }

  void sample(float& gx, float& gy, float& gz) {
    if (remaining == 0u) {
      chooseRegime();
    }
    --remaining;
    const float n0 = 0.15f * rng.unit();
    const float n1 = 0.15f * rng.unit();
    const float n2 = 0.15f * rng.unit();
    switch (regime) {
      case 1:
      case 3: {
        gx = impulse_sign * impulse_amp + 0.3f * n0;
        gy = 0.2f * gx + 0.3f * n1;
        gz = -0.1f * gx + 0.3f * n2;
        break;
      }
      case 2: {
        const float amp = 2.0f + 30.0f * (0.5f + 0.5f * rng.unit()); // 2..32
        gx = n0;
        gy = (rng.below(2u) ? amp : -amp) + n1;
        gz = 0.3f * amp + n2;
        break;
      }
      default:
        gx = n0;
        gy = n1;
        gz = n2;
        break;
    }
    // Cadence: nominal 5 ms with bursty dropouts and occasional long gaps
    // (USB / display stalls), including gaps that defeat the dt clamp.
    uint32_t dt = 5u;
    const uint32_t d = rng.below(20000u);
    if (d < 1200u) {
      dt = 6u + rng.below(30u);          // bursty dropout (~165 Hz effective)
    } else if (d < 1210u) {
      dt = 60u + rng.below(400u);        // stall past the dt clamp
    } else if (d == 1210u) {
      dt = 2500u + rng.below(3000u);     // long stall past the quiet gate
    }
    t_ms += dt;
  }
};

void run_fuzz(uint32_t seed, uint32_t start_ms, uint32_t samples,
              uint32_t min_clicks) {
  Pair p;
  Fuzzer f(seed, start_ms);
  bool wrapped = false;
  for (uint32_t i = 0; i < samples; ++i) {
    float gx, gy, gz;
    const uint32_t before = f.t_ms;
    f.sample(gx, gy, gz);
    p.step(before, gx, gy, gz);
    if (f.t_ms < before) {
      wrapped = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(samples, p.samples);
  // Non-vacuity: the generator must reach clicks and impulses in both.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(min_clicks, p.clicks);
  TEST_ASSERT_GREATER_THAN_UINT32(p.clicks * 4u, p.impulses);
  if (start_ms > 0xF0000000u) {
    TEST_ASSERT_TRUE_MESSAGE(wrapped, "timestamps did not cross the wrap");
  }
}

void test_fuzz_200k_samples_5ms_cadence() { run_fuzz(0x1234ABCDu, 0u, 200000u, 20u); }

void test_fuzz_second_seed() { run_fuzz(0xDEADBEEFu, 1000u, 200000u, 20u); }

void test_fuzz_across_timestamp_wrap() {
  run_fuzz(0x0BADF00Du, 0xFFFFFF00u, 200000u, 20u);
}

void test_fuzz_many_seeds() {
  for (uint32_t seed = 1; seed <= 24; ++seed) {
    run_fuzz(seed * 2654435761u, seed * 7919u, 30000u, 1u);
  }
}

// Deterministic sweep of coded patterns with every gap on / just outside its
// window boundary, so the interval matcher parity is exercised exactly at
// 300 / 700 / 800 / 1400 ms in both implementations.
void test_boundary_gap_sweep() {
  const uint32_t gaps1[] = {240, 299, 300, 301, 500, 699, 700, 701, 900};
  const uint32_t gaps2[] = {700, 799, 800, 801, 1100, 1399, 1400, 1401, 1500};
  for (uint32_t g1 : gaps1) {
    for (uint32_t g2 : gaps2) {
      for (uint32_t g3 : gaps1) {
        Pair p;
        uint32_t now = 0;
        bool emitted = false;
        feedStill(p, now, 2100);
        feedImpulse(p, now, emitted);
        feedStill(p, now, g1);
        feedImpulse(p, now, emitted);
        feedStill(p, now, g2);
        feedImpulse(p, now, emitted);
        feedStill(p, now, g3);
        feedImpulse(p, now, emitted);
        feedStill(p, now, 1600);
        feedImpulse(p, now, emitted);
        feedStill(p, now, 400);
        feedImpulse(p, now, emitted);
      }
    }
  }
}

}  // namespace

CV2_UNITY_MAIN(
  RUN_TEST(test_pattern_stillness_never_emits);
  RUN_TEST(test_pattern_double_pause_double_emits_once);
  RUN_TEST(test_pattern_uniform_four_blinks_do_not_emit);
  RUN_TEST(test_pattern_three_impulses_do_not_emit);
  RUN_TEST(test_pattern_head_motion_cancels_partial_sequence);
  RUN_TEST(test_fuzz_200k_samples_5ms_cadence);
  RUN_TEST(test_fuzz_second_seed);
  RUN_TEST(test_fuzz_across_timestamp_wrap);
  RUN_TEST(test_fuzz_many_seeds);
  RUN_TEST(test_boundary_gap_sweep);
)
