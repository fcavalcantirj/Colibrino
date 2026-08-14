#include "colibrino/motion_controller.h"

#include <algorithm>
#include <cmath>

namespace colibrino {

float Vec3::at(size_t index) const {
  switch (index) {
    case 0:
      return x;
    case 1:
      return y;
    default:
      return z;
  }
}

void GyroBiasCalibrator::reset() {
  count_ = 0;
  mean_ = {};
  m2_ = {};
}

void GyroBiasCalibrator::add(const Vec3& gyro_dps) {
  ++count_;
  const float n = static_cast<float>(count_);

  const Vec3 delta{gyro_dps.x - mean_.x, gyro_dps.y - mean_.y,
                   gyro_dps.z - mean_.z};
  mean_.x += delta.x / n;
  mean_.y += delta.y / n;
  mean_.z += delta.z / n;
  const Vec3 delta2{gyro_dps.x - mean_.x, gyro_dps.y - mean_.y,
                    gyro_dps.z - mean_.z};
  m2_.x += delta.x * delta2.x;
  m2_.y += delta.y * delta2.y;
  m2_.z += delta.z * delta2.z;
}

bool GyroBiasCalibrator::finish(Vec3& bias_dps,
                                float maximum_stddev_dps) const {
  if (count_ < 50) {
    return false;
  }
  const float divisor = static_cast<float>(count_ - 1);
  const Vec3 stddev{std::sqrt(m2_.x / divisor), std::sqrt(m2_.y / divisor),
                    std::sqrt(m2_.z / divisor)};
  if (stddev.x > maximum_stddev_dps || stddev.y > maximum_stddev_dps ||
      stddev.z > maximum_stddev_dps) {
    return false;
  }
  bias_dps = mean_;
  return true;
}

MotionController::MotionController(MotionConfig config) : config_(config) {}

void MotionController::setBias(const Vec3& gyro_bias_dps) {
  bias_ = gyro_bias_dps;
  reset();
}

void MotionController::reset() {
  horizontal_filtered_ = 0.0f;
  vertical_filtered_ = 0.0f;
  horizontal_accumulator_ = 0.0f;
  vertical_accumulator_ = 0.0f;
}

PointerDelta MotionController::update(const Vec3& gyro_dps,
                                      float dt_seconds) {
  if (!(dt_seconds > 0.0f) || dt_seconds > 0.2f) {
    return {};
  }

  const Vec3 corrected{gyro_dps.x - bias_.x, gyro_dps.y - bias_.y,
                       gyro_dps.z - bias_.z};
  const float horizontal =
      corrected.at(config_.horizontal_axis) * config_.horizontal_sign;
  const float vertical =
      corrected.at(config_.vertical_axis) * config_.vertical_sign;

  const float alpha = std::clamp(config_.low_pass_alpha, 0.0f, 1.0f);
  horizontal_filtered_ += alpha * (horizontal - horizontal_filtered_);
  vertical_filtered_ += alpha * (vertical - vertical_filtered_);

  horizontal_accumulator_ +=
      applyDeadzone(horizontal_filtered_, config_.deadzone_dps) *
      config_.pixels_per_degree * dt_seconds;
  vertical_accumulator_ +=
      applyDeadzone(vertical_filtered_, config_.deadzone_dps) *
      config_.pixels_per_degree * dt_seconds;

  return {takeReport(horizontal_accumulator_, config_.maximum_report),
          takeReport(vertical_accumulator_, config_.maximum_report)};
}

float MotionController::applyDeadzone(float value, float deadzone) {
  const float magnitude = std::fabs(value);
  if (magnitude <= deadzone) {
    return 0.0f;
  }
  return std::copysign(magnitude - deadzone, value);
}

int8_t MotionController::takeReport(float& accumulator,
                                    int8_t maximum_report) {
  const int maximum = std::max(1, static_cast<int>(maximum_report));
  // Nudge only at the integer boundary so values such as -1.9999999, produced
  // by repeated fractional steps, do not lose a pixel because truncation moves
  // toward zero.
  constexpr float kBoundaryEpsilon = 0.000001f;
  int report = static_cast<int>(std::trunc(
      accumulator + std::copysign(kBoundaryEpsilon, accumulator)));
  report = std::clamp(report, -maximum, maximum);
  accumulator -= static_cast<float>(report);
  return static_cast<int8_t>(report);
}

}  // namespace colibrino
