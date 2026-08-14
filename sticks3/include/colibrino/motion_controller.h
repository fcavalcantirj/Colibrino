#pragma once

#include <stddef.h>
#include <stdint.h>

namespace colibrino {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  float at(size_t index) const;
};

struct PointerDelta {
  int8_t x = 0;
  int8_t y = 0;
};

struct MotionConfig {
  uint8_t horizontal_axis = 1;
  int8_t horizontal_sign = -1;
  uint8_t vertical_axis = 0;
  int8_t vertical_sign = 1;
  float deadzone_dps = 1.4f;
  float pixels_per_degree = 18.0f;
  float low_pass_alpha = 0.28f;
  int8_t maximum_report = 60;
};

class GyroBiasCalibrator {
 public:
  void reset();
  void add(const Vec3& gyro_dps);
  bool finish(Vec3& bias_dps, float maximum_stddev_dps = 2.0f) const;
  size_t count() const { return count_; }

 private:
  size_t count_ = 0;
  Vec3 mean_{};
  Vec3 m2_{};
};

class MotionController {
 public:
  explicit MotionController(MotionConfig config = {});

  void setBias(const Vec3& gyro_bias_dps);
  void reset();
  PointerDelta update(const Vec3& gyro_dps, float dt_seconds);

 private:
  static float applyDeadzone(float value, float deadzone);
  static int8_t takeReport(float& accumulator, int8_t maximum_report);

  MotionConfig config_;
  Vec3 bias_{};
  float horizontal_filtered_ = 0.0f;
  float vertical_filtered_ = 0.0f;
  float horizontal_accumulator_ = 0.0f;
  float vertical_accumulator_ = 0.0f;
};

}  // namespace colibrino
