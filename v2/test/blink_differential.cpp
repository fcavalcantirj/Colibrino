// Differential oracle (rung 5): the v2 pipeline (blink-dsp -> blink-code)
// must reproduce the sticks3 v1 ImuBlinkDetector sample for sample:
//   v1.update(t, gyro) == (gesture.kind == CLICK_CANDIDATE)
//   v1.acceptedImpulses() == pipeline.impulses      (after every sample)
//   v1.completedSequences() == pipeline.clicks       (after every sample)
// Inputs: the five sticks3 synthetic patterns, a >= 200k-sample xorshift32
// fuzz at 5 ms cadence with injected impulses, head bursts and dropout gaps,
// fuzz runs that cross the 0xFFFFFFFF -> 0 wrap in steady state plus a
// deterministic slide of the wrap through a coded pattern, and a sweep of
// end-to-end intervals exactly on the window boundaries.
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
  uint32_t last_impulse_ms = 0;  // t_ms of the last IMPULSE event (its end)
  uint32_t last_click_ms = 0;    // t_ms of the last CLICK_CANDIDATE

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
      last_click_ms = g.hdr.t_ms;
    }
    if (blink.hdr.kind == CV2_BLINK_IMPULSE) {
      ++impulses;
      last_impulse_ms = blink.hdr.t_ms;
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

// The synthetic impulse above opens on its first sample and closes (IMPULSE
// emitted, end-to-end intervals are measured from here) on its ninth, 40 ms
// later; feedImpulse returns 20 ms after that.
constexpr uint32_t kImpulseEndOffsetMs = 40;

// Quiet samples at 5 ms cadence until now_ms == end_ms exactly; the last
// step may be shorter than 5 ms so the next impulse can start on any ms
// (the baseline dt clamp accepts 1..50 ms in both implementations).
void feedStillUntil(Pair& p, uint32_t& now_ms, uint32_t end_ms) {
  uint32_t elapsed = 0;
  while (now_ms != end_ms) {
    const float noise = (elapsed % 20 == 0) ? 0.12f : -0.06f;
    p.step(now_ms, noise, -noise, 0.0f);
    const uint32_t remaining = end_ms - now_ms;
    const uint32_t dt = remaining < 5 ? remaining : 5;
    now_ms += dt;
    elapsed += dt;
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

// Start far enough before the wrap that the generator is in steady state
// (impulses, sequences, holds and refractories all live) when the clock
// crosses 0xFFFFFFFF -> 0, at three different depths into the run.
void test_fuzz_across_timestamp_wrap() {
  run_fuzz(0x0BADF00Du, 0u - 60000u, 200000u, 20u);
  run_fuzz(0x0BADF00Du, 0u - 6000u, 200000u, 20u);
  run_fuzz(0x5EEDF00Du, 0u - 3000u, 200000u, 20u);
}

// Deterministic: the wrap point slid in 7 ms steps (coprime with the 5 ms
// cadence, so every sample phase is hit) through a script that keeps every
// stateful window of both machines live at some point: the seed quiet gate
// with an impulse inside it, coded pattern A, its click and click hold with
// an impulse inside it, coded pattern B, a head burst that reopens the quiet
// gate with an impulse inside it, and one accepted impulse after it.
// Whatever the wrap crosses, the accepted-impulse count at each checkpoint
// and both click times must be exact and both machines must agree.
void test_wrap_slides_through_coded_pattern() {
  // Script relative to its start (see feedStill / feedImpulse):
  //   0      still 1000
  //   1000   imp0 inside the 2 s seed quiet gate: not counted
  //   1060   still 1040
  //   2100   imp1 (end 2140)  still 400  imp2 (end 2600, +460 double)
  //   2620   still 900        imp3 (end 3560, +960 pause)
  //   3580   still 400        imp4 (end 4020, +460 double) -> CLICK A @4020
  //   4040   still 1000       imp5 @5040 inside the 1500 ms hold: not counted
  //   5100   still 500        imp6 (end 5640, first of pattern B)
  //   5660   still 400  imp7  still 900  imp8  still 400  imp9 -> CLICK B @7520
  //   7540   still 1500
  //   9040   head burst (CANCEL, quiet gate until 11040)
  //   9045   still 955        imp10 @10000 inside the quiet gate: not counted
  //   10060  still 1040       imp11 (end 11140) accepted
  //   11160  still 340 -> 11500
  constexpr uint32_t kSpanMs = 11500;
  constexpr uint32_t kClickA = 4020;
  constexpr uint32_t kClickB = 7520;
  constexpr uint32_t kBurst = 9040;
  uint32_t runs = 0;
  uint32_t wrapped_in_seed_gate = 0;
  uint32_t wrapped_inside_pattern_a = 0;
  uint32_t wrapped_inside_hold = 0;
  uint32_t wrapped_inside_pattern_b = 0;
  uint32_t wrapped_in_burst_gate = 0;
  for (uint32_t offset = 0; offset <= kSpanMs; offset += 7) {
    const uint32_t start = 0u - offset;
    Pair p;
    uint32_t now = start;
    bool emitted = false;
    feedStill(p, now, 1000);
    feedImpulse(p, now, emitted);  // inside the seed quiet gate
    TEST_ASSERT_EQUAL_UINT32(0, p.impulses);
    feedStill(p, now, 1040);
    feedImpulse(p, now, emitted);
    feedStill(p, now, 400);
    feedImpulse(p, now, emitted);
    feedStill(p, now, 900);
    feedImpulse(p, now, emitted);
    feedStill(p, now, 400);
    feedImpulse(p, now, emitted);
    TEST_ASSERT_EQUAL_UINT32(4, p.impulses);
    TEST_ASSERT_EQUAL_UINT32(1, p.clicks);
    TEST_ASSERT_EQUAL_UINT32(start + kClickA, p.last_click_ms);
    feedStill(p, now, 1000);
    feedImpulse(p, now, emitted);  // inside the click hold
    TEST_ASSERT_EQUAL_UINT32(4, p.impulses);
    feedStill(p, now, 500);
    feedImpulse(p, now, emitted);
    feedStill(p, now, 400);
    feedImpulse(p, now, emitted);
    feedStill(p, now, 900);
    feedImpulse(p, now, emitted);
    feedStill(p, now, 400);
    feedImpulse(p, now, emitted);
    TEST_ASSERT_EQUAL_UINT32(8, p.impulses);
    TEST_ASSERT_EQUAL_UINT32(2, p.clicks);
    TEST_ASSERT_EQUAL_UINT32(start + kClickB, p.last_click_ms);
    feedStill(p, now, 1500);
    TEST_ASSERT_EQUAL_UINT32(start + kBurst, now);
    p.step(now, 0.0f, 8.0f, 0.0f);  // head burst reopens the quiet gate
    now += 5;
    feedStill(p, now, 955);
    feedImpulse(p, now, emitted);  // inside the reopened quiet gate
    TEST_ASSERT_EQUAL_UINT32(8, p.impulses);
    feedStill(p, now, 1040);
    feedImpulse(p, now, emitted);
    TEST_ASSERT_EQUAL_UINT32(9, p.impulses);
    feedStill(p, now, 340);
    TEST_ASSERT_EQUAL_UINT32(start + kSpanMs, now);
    TEST_ASSERT_TRUE(emitted);
    TEST_ASSERT_EQUAL_UINT32(2, p.clicks);
    ++runs;
    // Phase bookkeeping (offset = ms of the script that lie before 0).
    if (offset > 0 && offset < 2000) ++wrapped_in_seed_gate;
    if (offset > 2100 && offset < kClickA) ++wrapped_inside_pattern_a;
    if (offset > kClickA && offset < kClickA + 1500) ++wrapped_inside_hold;
    if (offset > 5600 && offset < kClickB) ++wrapped_inside_pattern_b;
    if (offset > kBurst && offset < kBurst + 2000) ++wrapped_in_burst_gate;
  }
  TEST_ASSERT_GREATER_THAN_UINT32(1600, runs);
  TEST_ASSERT_GREATER_THAN_UINT32(100, wrapped_in_seed_gate);
  TEST_ASSERT_GREATER_THAN_UINT32(100, wrapped_inside_pattern_a);
  TEST_ASSERT_GREATER_THAN_UINT32(100, wrapped_inside_hold);
  TEST_ASSERT_GREATER_THAN_UINT32(100, wrapped_inside_pattern_b);
  TEST_ASSERT_GREATER_THAN_UINT32(100, wrapped_in_burst_gate);
}

void test_fuzz_many_seeds() {
  for (uint32_t seed = 1; seed <= 24; ++seed) {
    run_fuzz(seed * 2654435761u, seed * 7919u, 30000u, 1u);
  }
}

// Deterministic sweep of coded patterns whose end-to-end impulse intervals
// (what blink-code measures) land exactly on / 1 ms either side of the
// 700 / 800 / 1400 ms window boundaries, so the interval matcher parity is
// exercised at the boundaries in both implementations. The 300 ms lower
// double boundary is unreachable end to end: the 300 ms impulse refractory
// plus the 20 ms minimum impulse make 320 ms the shortest possible interval
// (340 ms with this 40 ms synthetic impulse, which is swept as the shortest
// reachable one); 299/300/301 are covered by blink_code_synth's hand-built
// events. Every combo asserts the intended intervals were really produced
// (non-vacuity) and that a click happens iff all three intervals are inside
// their windows.
void test_boundary_gap_sweep() {
  constexpr uint32_t kDoubleMin = CV2_FEEL_BLINK_DOUBLE_MINIMUM_MS;
  constexpr uint32_t kDoubleMax = CV2_FEEL_BLINK_DOUBLE_MAXIMUM_MS;
  constexpr uint32_t kPauseMin = CV2_FEEL_BLINK_PAUSE_MINIMUM_MS;
  constexpr uint32_t kPauseMax = CV2_FEEL_BLINK_PAUSE_MAXIMUM_MS;
  const uint32_t doubles[] = {340, 500, 699, 700, 701, 900};
  const uint32_t pauses[] = {700, 799, 800, 801, 1100, 1399, 1400, 1401, 1500};
  const auto in_double = [&](uint32_t i) {
    return i >= kDoubleMin && i <= kDoubleMax;
  };
  const auto in_pause = [&](uint32_t i) {
    return i >= kPauseMin && i <= kPauseMax;
  };
  uint32_t combos = 0;
  uint32_t clicking_combos = 0;
  bool seen[2048] = {};
  for (uint32_t d1 : doubles) {
    for (uint32_t p2 : pauses) {
      for (uint32_t d3 : doubles) {
        const uint32_t targets[3] = {d1, p2, d3};
        Pair p;
        uint32_t now = 0;
        bool emitted = false;
        feedStill(p, now, 2100);
        feedImpulse(p, now, emitted);
        TEST_ASSERT_EQUAL_UINT32(1, p.impulses);
        for (uint32_t k = 0; k < 3; ++k) {
          const uint32_t prev_end = p.last_impulse_ms;
          const uint32_t before = p.impulses;
          // Next impulse must END exactly targets[k] after the previous end.
          feedStillUntil(p, now, prev_end + targets[k] - kImpulseEndOffsetMs);
          feedImpulse(p, now, emitted);
          TEST_ASSERT_EQUAL_UINT32(before + 1, p.impulses);
          const uint32_t interval = p.last_impulse_ms - prev_end;
          TEST_ASSERT_EQUAL_UINT32(targets[k], interval);
          seen[interval] = true;
        }
        const bool expect_click =
            in_double(d1) && in_pause(p2) && in_double(d3);
        TEST_ASSERT_EQUAL_UINT32(expect_click ? 1 : 0, p.clicks);
        TEST_ASSERT_EQUAL(expect_click, emitted);
        // Tail: an impulse after the click refractory and one more double.
        feedStill(p, now, 1600);
        feedImpulse(p, now, emitted);
        feedStill(p, now, 400);
        feedImpulse(p, now, emitted);
        TEST_ASSERT_EQUAL_UINT32(expect_click ? 1 : 0, p.clicks);
        ++combos;
        clicking_combos += expect_click ? 1 : 0;
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT32(6 * 9 * 6, combos);
  TEST_ASSERT_EQUAL_UINT32(4 * 5 * 4, clicking_combos);
  // Non-vacuity: every boundary interval was observed end to end.
  const uint32_t required[] = {340, 699,  700,  701, 799,
                               800, 801, 1399, 1400, 1401};
  for (uint32_t r : required) {
    TEST_ASSERT_TRUE_MESSAGE(seen[r], "a boundary interval was never produced");
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
  RUN_TEST(test_wrap_slides_through_coded_pattern);
  RUN_TEST(test_fuzz_many_seeds);
  RUN_TEST(test_boundary_gap_sweep);
)
