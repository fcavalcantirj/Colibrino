#include <unity.h>

// Native tests exercise the same portable source compiled into production and
// Wokwi. They intentionally contain no Arduino or M5Stack dependencies.
#include "colibrino/motion_controller.h"

using colibrino::GyroBiasCalibrator;
using colibrino::MotionConfig;
using colibrino::MotionController;
using colibrino::Vec3;

void test_calibration_accepts_stationary_samples() {
  GyroBiasCalibrator calibration;
  for (int index = 0; index < 100; ++index) {
    const float noise = (index % 2 == 0) ? 0.05f : -0.05f;
    calibration.add({1.0f + noise, -2.0f - noise, 0.5f + noise});
  }
  Vec3 bias;
  TEST_ASSERT_TRUE(calibration.finish(bias));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, bias.x);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -2.0f, bias.y);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, bias.z);
}
void test_calibration_rejects_motion() {
  GyroBiasCalibrator calibration;
  for (int index = 0; index < 100; ++index) {
    calibration.add({static_cast<float>(index % 10), 0.0f, 0.0f});
  }
  Vec3 bias;
  TEST_ASSERT_FALSE(calibration.finish(bias));
}

void test_pointer_deadzone_suppresses_bias_and_noise() {
  MotionConfig config;
  config.horizontal_axis = 0;
  config.horizontal_sign = 1;
  config.vertical_axis = 1;
  config.vertical_sign = 1;
  config.low_pass_alpha = 1.0f;
  MotionController motion(config);
  motion.setBias({1.0f, -1.0f, 0.0f});

  for (int index = 0; index < 100; ++index) {
    const auto delta = motion.update({1.5f, -1.4f, 0.0f}, 0.01f);
    TEST_ASSERT_EQUAL_INT8(0, delta.x);
    TEST_ASSERT_EQUAL_INT8(0, delta.y);
  }
}

void test_pointer_accumulates_fractional_motion_and_respects_sign() {
  MotionConfig config;
  config.horizontal_axis = 0;
  config.horizontal_sign = -1;
  config.vertical_axis = 1;
  config.vertical_sign = 1;
  config.deadzone_dps = 0.0f;
  config.pixels_per_degree = 10.0f;
  config.low_pass_alpha = 1.0f;
  MotionController motion(config);

  int x_total = 0;
  int y_total = 0;
  for (int index = 0; index < 10; ++index) {
    const auto delta = motion.update({2.0f, 3.0f, 0.0f}, 0.01f);
    x_total += delta.x;
    y_total += delta.y;
  }
  TEST_ASSERT_EQUAL_INT(-2, x_total);
  TEST_ASSERT_EQUAL_INT(3, y_total);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_calibration_accepts_stationary_samples);
  RUN_TEST(test_calibration_rejects_motion);
  RUN_TEST(test_pointer_deadzone_suppresses_bias_and_noise);
  RUN_TEST(test_pointer_accumulates_fractional_motion_and_respects_sign);
  return UNITY_END();
}
