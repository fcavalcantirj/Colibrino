#pragma once

#include <stdint.h>

namespace colibrino::config {

// StickS3 internal peripherals. These are schematic-defined connections, not
// spare expansion GPIOs. Changing them selects the wrong internal circuit.
inline constexpr int kIrTransmitPin = 46;
inline constexpr int kIrReceivePin = 42;

// The first hardware session must confirm these axes for the intended glasses
// mount. The motion monitor prints every gyro axis so this can be changed
// without touching the motion algorithm.
inline constexpr uint8_t kHorizontalGyroAxis = 1;
inline constexpr int8_t kHorizontalSign = -1;
inline constexpr uint8_t kVerticalGyroAxis = 0;
inline constexpr int8_t kVerticalSign = 1;

// Motion tuning uses degrees per second from M5Unified. The controller
// integrates rate over the measured BMI270 sample interval to produce relative
// pixels; it does not estimate an absolute head angle.
inline constexpr float kGyroDeadzoneDps = 1.4f;
inline constexpr float kPointerPixelsPerDegree = 18.0f;
inline constexpr float kMotionLowPassAlpha = 0.28f;
// USBHIDMouse uses signed relative reports. Keeping margin below INT8_MAX makes
// abnormal samples bounded before they reach the host pointer.
inline constexpr int8_t kMaximumMouseReport = 60;

// The demodulating receiver expects a remote-control carrier. The sample period
// includes the 8 ms transmit envelope and receiver idle completion time.
inline constexpr uint32_t kIrCarrierHz = 38000;
inline constexpr float kIrCarrierDuty = 0.33f;
inline constexpr uint32_t kIrSampleIntervalMs = 35;

}  // namespace colibrino::config
