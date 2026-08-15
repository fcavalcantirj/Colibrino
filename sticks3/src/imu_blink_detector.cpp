#include "colibrino/imu_blink_detector.h"

#include <algorithm>
#include <cmath>

namespace colibrino {

ImuBlinkDetector::ImuBlinkDetector(ImuBlinkConfig config) : config_(config) {}

void ImuBlinkDetector::reset() {
  baseline_initialized_ = false;
  baseline_ = {};
  previous_ms_ = 0;
  impulse_active_ = false;
  impulse_started_ms_ = 0;
  sequence_impulses_ = 0;
  previous_sequence_impulse_ms_ = 0;
  have_last_impulse_ = false;
  last_impulse_ms_ = 0;
  head_suppressed_ = false;
  last_head_motion_ms_ = 0;
  click_suppressed_ = false;
  last_click_ms_ = 0;
  accepted_impulses_ = 0;
  completed_sequences_ = 0;
}

float ImuBlinkDetector::magnitude(const Vec3& value) {
  return std::sqrt(value.x * value.x + value.y * value.y +
                   value.z * value.z);
}

void ImuBlinkDetector::cancelSequence() {
  impulse_active_ = false;
  sequence_impulses_ = 0;
  previous_sequence_impulse_ms_ = 0;
}

bool ImuBlinkDetector::update(uint32_t now_ms, const Vec3& gyro_dps) {
  if (!baseline_initialized_) {
    baseline_ = gyro_dps;
    baseline_initialized_ = true;
    previous_ms_ = now_ms;
    // A reset commonly follows boot, a validation-stage transition, or mouse
    // arming. Require a complete quiet interval before accepting the first
    // pulse so motion immediately before the reset cannot seed a click.
    head_suppressed_ = true;
    last_head_motion_ms_ = now_ms;
    return false;
  }

  // Bound delayed-loop influence. Normal hardware samples arrive at 200 Hz;
  // a long USB or display pause must not jump the adaptive baseline.
  const uint32_t elapsed_ms = now_ms - previous_ms_;
  previous_ms_ = now_ms;
  const float bounded_dt_ms =
      static_cast<float>(std::max<uint32_t>(1, std::min<uint32_t>(50, elapsed_ms)));
  const float alpha = bounded_dt_ms /
                      (config_.baseline_time_constant_ms + bounded_dt_ms);
  baseline_.x += alpha * (gyro_dps.x - baseline_.x);
  baseline_.y += alpha * (gyro_dps.y - baseline_.y);
  baseline_.z += alpha * (gyro_dps.z - baseline_.z);

  const Vec3 residual{gyro_dps.x - baseline_.x, gyro_dps.y - baseline_.y,
                      gyro_dps.z - baseline_.z};
  const float residual_dps = magnitude(residual);
  const float head_rate_dps = magnitude(gyro_dps);

  // Any pointer-scale motion cancels an incomplete sequence and starts a
  // quiet-time gate. This is the primary false-click safety boundary.
  if (head_rate_dps > config_.maximum_head_rate_dps) {
    cancelSequence();
    head_suppressed_ = true;
    last_head_motion_ms_ = now_ms;
    return false;
  }
  if (head_suppressed_) {
    if (now_ms - last_head_motion_ms_ < config_.head_motion_suppression_ms) {
      return false;
    }
    head_suppressed_ = false;
  }

  if (click_suppressed_) {
    if (now_ms - last_click_ms_ < config_.click_refractory_ms) {
      return false;
    }
    click_suppressed_ = false;
  }

  if (sequence_impulses_ > 0 &&
      now_ms - previous_sequence_impulse_ms_ >
          config_.sequence_maximum_ms) {
    sequence_impulses_ = 0;
  }

  if (have_last_impulse_ &&
      now_ms - last_impulse_ms_ < config_.impulse_refractory_ms) {
    return false;
  }
  have_last_impulse_ = false;

  if (!impulse_active_) {
    if (residual_dps >= config_.impulse_enter_dps) {
      impulse_active_ = true;
      impulse_started_ms_ = now_ms;
    }
    return false;
  }

  if (residual_dps > config_.impulse_exit_dps) {
    if (now_ms - impulse_started_ms_ > config_.impulse_maximum_ms) {
      cancelSequence();
    }
    return false;
  }

  const uint32_t impulse_duration_ms = now_ms - impulse_started_ms_;
  impulse_active_ = false;
  if (impulse_duration_ms < config_.impulse_minimum_ms ||
      impulse_duration_ms > config_.impulse_maximum_ms) {
    sequence_impulses_ = 0;
    return false;
  }

  ++accepted_impulses_;
  have_last_impulse_ = true;
  last_impulse_ms_ = now_ms;
  if (sequence_impulses_ > 0) {
    const uint32_t interval_ms = now_ms - previous_sequence_impulse_ms_;
    if (interval_ms < config_.sequence_minimum_ms ||
        interval_ms > config_.sequence_maximum_ms) {
      // The current impulse can still start a new deliberate sequence.
      sequence_impulses_ = 0;
    }
  }
  ++sequence_impulses_;
  previous_sequence_impulse_ms_ = now_ms;

  // Treat invalid zero/one configurations as a two-impulse minimum. A caller
  // must never be able to turn ordinary single blinks directly into clicks.
  const uint8_t required_impulses =
      std::max<uint8_t>(2, config_.required_impulses);
  if (sequence_impulses_ < required_impulses) {
    return false;
  }
  sequence_impulses_ = 0;
  previous_sequence_impulse_ms_ = 0;
  click_suppressed_ = true;
  last_click_ms_ = now_ms;
  ++completed_sequences_;
  return true;
}

}  // namespace colibrino
