#include <unity.h>

// These cases protect the minimum optical evidence and human-timing gates that
// must pass before the application can configure runtime blink clicks.
#include "colibrino/signal_analysis.h"

using colibrino::BlinkDetector;
using colibrino::BlinkFeasibilityProtocol;
using colibrino::FeasibilityConfig;
using colibrino::FeasibilityStage;
using colibrino::RunningStats;
using colibrino::analyzeSeparation;

void test_separation_rejects_identical_signals() {
  RunningStats open;
  RunningStats closed;
  for (int index = 0; index < 30; ++index) {
    open.add(0.5f + (index % 2) * 0.01f);
    closed.add(0.5f - (index % 2) * 0.01f);
  }
  const auto result = analyzeSeparation(open, closed);
  TEST_ASSERT_FALSE(result.passes_signal_gate);
}
void test_separation_accepts_stable_open_closed_difference() {
  RunningStats open;
  RunningStats closed;
  for (int index = 0; index < 30; ++index) {
    const float noise = (index % 2) * 0.005f;
    open.add(0.20f + noise);
    closed.add(0.70f + noise);
  }
  const auto result = analyzeSeparation(open, closed);
  TEST_ASSERT_TRUE(result.passes_signal_gate);
  TEST_ASSERT_TRUE(result.closed_is_higher);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.45f, result.threshold);
}

void test_blink_detector_requires_human_blink_duration() {
  RunningStats open;
  RunningStats closed;
  for (int index = 0; index < 30; ++index) {
    open.add(0.2f);
    closed.add(0.8f);
  }
  BlinkDetector detector;
  detector.configure(analyzeSeparation(open, closed));
  TEST_ASSERT_FALSE(detector.update(1000, 0.8f));
  TEST_ASSERT_FALSE(detector.update(1020, 0.2f));
  TEST_ASSERT_FALSE(detector.update(1300, 0.8f));
  TEST_ASSERT_TRUE(detector.update(1420, 0.2f));
  TEST_ASSERT_FALSE(detector.update(1500, 0.8f));
  TEST_ASSERT_FALSE(detector.update(2300, 0.2f));
}

void test_guided_protocol_requires_signal_and_two_blinks() {
  const FeasibilityConfig config{10, 30, 30, 500, 2};
  BlinkFeasibilityProtocol protocol(config);
  protocol.start(0);
  protocol.tick(10);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FeasibilityStage::kCaptureOpen),
                        static_cast<int>(protocol.stage()));
  for (uint32_t time = 10; time < 40; ++time) {
    protocol.observe(time, true, 0.2f + (time % 2) * 0.001f);
  }
  protocol.tick(40);
  protocol.tick(50);
  for (uint32_t time = 50; time < 80; ++time) {
    protocol.observe(time, true, 0.8f + (time % 2) * 0.001f);
  }
  protocol.tick(80);
  protocol.tick(90);

  protocol.observe(100, true, 0.8f);
  protocol.observe(180, true, 0.2f);
  protocol.observe(400, true, 0.8f);
  protocol.observe(480, true, 0.2f);
  protocol.tick(590);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(FeasibilityStage::kResult),
                        static_cast<int>(protocol.stage()));
  TEST_ASSERT_TRUE(protocol.result().passes);
  TEST_ASSERT_EQUAL_UINT8(2, protocol.result().detected_blinks);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_separation_rejects_identical_signals);
  RUN_TEST(test_separation_accepts_stable_open_closed_difference);
  RUN_TEST(test_blink_detector_requires_human_blink_duration);
  RUN_TEST(test_guided_protocol_requires_signal_and_two_blinks);
  return UNITY_END();
}
