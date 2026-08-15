#include <unity.h>

#include "colibrino/imu_blink_detector.h"

namespace {

using colibrino::ImuBlinkDetector;
using colibrino::Vec3;

void feedStill(ImuBlinkDetector& detector, uint32_t& now_ms,
               uint32_t duration_ms) {
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += 5) {
    const float noise = (elapsed % 20 == 0) ? 0.12f : -0.06f;
    TEST_ASSERT_FALSE(detector.update(now_ms, {noise, -noise, 0.0f}));
    now_ms += 5;
  }
}

void feedImpulse(ImuBlinkDetector& detector, uint32_t& now_ms,
                 bool& emitted) {
  // A bounded bipolar temple impulse returns below the exit threshold within
  // a human-blink-scale window. The adaptive baseline sees the same physical
  // units and timing as the 200 Hz BMI270 stream.
  for (int index = 0; index < 12; ++index) {
    const float value = index < 4 ? -1.35f : (index < 8 ? 0.95f : 0.10f);
    emitted = detector.update(now_ms, {value, 0.2f * value, -0.1f * value}) ||
              emitted;
    now_ms += 5;
  }
}

void test_stillness_never_emits() {
  ImuBlinkDetector detector;
  uint32_t now_ms = 0;
  feedStill(detector, now_ms, 6000);
  TEST_ASSERT_EQUAL_UINT32(0, detector.acceptedImpulses());
  TEST_ASSERT_EQUAL_UINT32(0, detector.completedSequences());
}

void feedSequenceGap(ImuBlinkDetector& detector, uint32_t& now_ms) {
  feedStill(detector, now_ms, 500);
}

void test_four_deliberate_impulses_emit_once() {
  ImuBlinkDetector detector;
  uint32_t now_ms = 0;
  bool emitted = false;
  // The detector intentionally requires two quiet seconds after reset.
  feedStill(detector, now_ms, 2100);
  feedImpulse(detector, now_ms, emitted);
  feedSequenceGap(detector, now_ms);
  feedImpulse(detector, now_ms, emitted);
  feedSequenceGap(detector, now_ms);
  feedImpulse(detector, now_ms, emitted);
  feedSequenceGap(detector, now_ms);
  feedImpulse(detector, now_ms, emitted);

  TEST_ASSERT_TRUE(emitted);
  TEST_ASSERT_EQUAL_UINT32(4, detector.acceptedImpulses());
  TEST_ASSERT_EQUAL_UINT32(1, detector.completedSequences());
}

void test_three_impulses_and_slow_sequence_do_not_emit() {
  ImuBlinkDetector detector;
  uint32_t now_ms = 0;
  bool emitted = false;
  feedStill(detector, now_ms, 2100);
  feedImpulse(detector, now_ms, emitted);
  feedSequenceGap(detector, now_ms);
  feedImpulse(detector, now_ms, emitted);
  feedSequenceGap(detector, now_ms);
  feedImpulse(detector, now_ms, emitted);
  feedStill(detector, now_ms, 1300);
  feedImpulse(detector, now_ms, emitted);

  TEST_ASSERT_FALSE(emitted);
  TEST_ASSERT_EQUAL_UINT32(0, detector.completedSequences());
}

void test_head_motion_cancels_partial_sequence() {
  ImuBlinkDetector detector;
  uint32_t now_ms = 0;
  bool emitted = false;
  feedStill(detector, now_ms, 2100);
  feedImpulse(detector, now_ms, emitted);
  feedSequenceGap(detector, now_ms);
  feedImpulse(detector, now_ms, emitted);
  feedSequenceGap(detector, now_ms);
  feedImpulse(detector, now_ms, emitted);
  feedStill(detector, now_ms, 300);

  // Pointer-scale rotation exceeds the 2.5 dps safety gate.
  TEST_ASSERT_FALSE(detector.update(now_ms, {0.0f, 8.0f, 0.0f}));
  now_ms += 5;
  feedStill(detector, now_ms, 2100);
  feedImpulse(detector, now_ms, emitted);

  TEST_ASSERT_FALSE(emitted);
  TEST_ASSERT_EQUAL_UINT32(0, detector.completedSequences());
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_stillness_never_emits);
  RUN_TEST(test_four_deliberate_impulses_emit_once);
  RUN_TEST(test_three_impulses_and_slow_sequence_do_not_emit);
  RUN_TEST(test_head_motion_cancels_partial_sequence);
  return UNITY_END();
}
