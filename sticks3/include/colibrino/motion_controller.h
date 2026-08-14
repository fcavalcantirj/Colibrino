#pragma once

#include <stddef.h>
#include <stdint.h>

namespace colibrino {

/// Three-axis angular-rate vector in degrees per second.
struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  /// Returns X for 0, Y for 1, and Z for all other indices.
  float at(size_t index) const;
};

/// One bounded relative HID movement report.
struct PointerDelta {
  int8_t x = 0;
  int8_t y = 0;
};

/// Mapping and response parameters for a particular physical mounting.
struct MotionConfig {
  /// Source axes use 0=X, 1=Y, and 2=Z.
  uint8_t horizontal_axis = 1;
  /// Signs should be exactly +1 or -1 after a mount-orientation test.
  int8_t horizontal_sign = -1;
  uint8_t vertical_axis = 0;
  int8_t vertical_sign = 1;
  /// Smooth deadzone radius in degrees per second.
  float deadzone_dps = 1.4f;
  /// Gain applied while integrating angular rate over elapsed seconds.
  float pixels_per_degree = 18.0f;
  /// Per-sample exponential filter weight, clamped to [0, 1].
  float low_pass_alpha = 0.28f;
  /// Absolute limit for each signed relative HID report.
  int8_t maximum_report = 60;
};

/// Streaming stationary-sample estimator for per-boot gyro bias.
///
/// Welford's algorithm avoids retaining a sample buffer. `finish` refuses fewer
/// than 50 samples and rejects any axis whose standard deviation indicates
/// motion, ensuring that an unsafe calibration never enables pointer output.
class GyroBiasCalibrator {
 public:
  /// Clears all streaming means and second moments.
  void reset();
  /// Adds one BMI270 angular-rate sample in degrees per second.
  void add(const Vec3& gyro_dps);
  /// Writes the mean bias only when sample count and stability gates pass.
  bool finish(Vec3& bias_dps, float maximum_stddev_dps = 2.0f) const;
  size_t count() const { return count_; }

 private:
  size_t count_ = 0;
  Vec3 mean_{};
  Vec3 m2_{};
};

/// Converts calibrated angular velocity into bounded relative pointer reports.
///
/// The controller retains filter and fractional-pixel state across calls. A
/// non-positive or greater-than-200 ms timestep returns zero and preserves
/// accumulators, preventing a scheduling gap from becoming a cursor jump.
class MotionController {
 public:
  explicit MotionController(MotionConfig config = {});

  /// Applies a newly accepted bias and clears all prior motion state.
  void setBias(const Vec3& gyro_bias_dps);
  /// Clears filters and residual pixels without changing configuration or bias.
  void reset();
  /// Processes one timestamped gyro sample into a signed HID delta.
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
