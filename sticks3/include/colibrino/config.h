#pragma once

#include <stdint.h>

namespace colibrino::config {

// StickS3 internal peripherals. These are board-specific, not free GPIOs.
inline constexpr int kIrTransmitPin = 46;
inline constexpr int kIrReceivePin = 42;

// The first hardware session must confirm these axes for the intended glasses
// mount. The motion monitor prints every gyro axis so this can be changed
// without touching the motion algorithm.
inline constexpr uint8_t kHorizontalGyroAxis = 1;
inline constexpr int8_t kHorizontalSign = -1;
inline constexpr uint8_t kVerticalGyroAxis = 0;
inline constexpr int8_t kVerticalSign = 1;

inline constexpr float kGyroDeadzoneDps = 1.4f;
inline constexpr float kPointerPixelsPerDegree = 18.0f;
inline constexpr float kMotionLowPassAlpha = 0.28f;
inline constexpr int8_t kMaximumMouseReport = 60;

inline constexpr uint32_t kIrCarrierHz = 38000;
inline constexpr float kIrCarrierDuty = 0.33f;
inline constexpr uint32_t kIrSampleIntervalMs = 35;

}  // namespace colibrino::config
