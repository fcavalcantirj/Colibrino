#pragma once

#include <stdint.h>

#include "colibrino/motion_controller.h"

namespace colibrino {

/// Conservative thresholds for recognizing a deliberate IMU blink sequence.
///
/// One or two temple impulses are too easy to confuse with natural blinking or
/// pauses during ordinary motion. The default therefore requires four bounded
/// impulses at a deliberate rhythm, after two seconds of head stillness.
/// Values are expressed in physical units from M5Unified.
struct ImuBlinkConfig {
  float baseline_time_constant_ms = 350.0f;
  float impulse_enter_dps = 1.0f;
  float impulse_exit_dps = 0.6f;
  float maximum_head_rate_dps = 2.5f;
  uint32_t head_motion_suppression_ms = 2000;
  uint32_t impulse_minimum_ms = 20;
  uint32_t impulse_maximum_ms = 300;
  uint32_t impulse_refractory_ms = 300;
  uint32_t sequence_minimum_ms = 350;
  uint32_t sequence_maximum_ms = 1100;
  uint32_t click_refractory_ms = 1500;
  uint8_t required_impulses = 4;
};

/// Allocation-free detector for a deliberate, low-motion blink sequence.
///
/// `update` returns true only after the configured number of accepted impulses.
/// Head motion above the gate immediately cancels partial sequences. This class
/// recommends an event but performs no USB or other side effect.
class ImuBlinkDetector {
 public:
  explicit ImuBlinkDetector(ImuBlinkConfig config = {});

  void reset();
  bool update(uint32_t now_ms, const Vec3& gyro_dps);

  uint32_t acceptedImpulses() const { return accepted_impulses_; }
  uint32_t completedSequences() const { return completed_sequences_; }

 private:
  static float magnitude(const Vec3& value);
  void cancelSequence();

  ImuBlinkConfig config_;
  bool baseline_initialized_ = false;
  Vec3 baseline_{};
  uint32_t previous_ms_ = 0;

  bool impulse_active_ = false;
  uint32_t impulse_started_ms_ = 0;
  uint8_t sequence_impulses_ = 0;
  uint32_t previous_sequence_impulse_ms_ = 0;
  bool have_last_impulse_ = false;
  uint32_t last_impulse_ms_ = 0;
  bool head_suppressed_ = false;
  uint32_t last_head_motion_ms_ = 0;
  bool click_suppressed_ = false;
  uint32_t last_click_ms_ = 0;

  uint32_t accepted_impulses_ = 0;
  uint32_t completed_sequences_ = 0;
};

}  // namespace colibrino
