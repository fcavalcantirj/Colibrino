#pragma once

#include <stdint.h>

namespace colibrino {

/// Sensor-neutral observation consumed by blink analysis.
///
/// Producers may use demodulated digital timing, analog reflectance, or another
/// sensing method. `value` must be internally consistent for one capture
/// session, but its polarity is deliberately unspecified: calibration learns
/// whether a closed eye produces a higher or lower value.
struct BlinkSignalSample {
  /// Monotonic acquisition time. Unsigned subtraction makes wraparound safe.
  uint32_t timestamp_ms = 0;
  /// False when the producer has no measurement suitable for classification.
  bool valid = false;
  /// Normalized producer-specific signal; the onboard RMT probe uses [0, 1].
  float value = 0.0f;
  /// Raw evidence retained for diagnostics; analog producers may leave it zero.
  uint16_t symbol_count = 0;
  /// Receiver-active time represented by this sample, in microseconds.
  uint32_t active_us = 0;
  /// Total captured time represented by this sample, in microseconds.
  uint32_t total_us = 0;
};

/// Hardware adapter boundary for click sensors.
///
/// Keeping this interface independent from USB and classification allows an
/// external TCRT5000 adapter to replace the onboard probe without changing
/// click policy or motion code.
class BlinkInput {
 public:
  virtual ~BlinkInput() = default;

  /// Initializes hardware without enabling application-level click output.
  virtual bool begin() = 0;

  /// Advances acquisition and optionally updates `sample`.
  ///
  /// Returns false when no acquisition cycle was due or when the producer could
  /// not run. A true return may still contain `sample.valid == false`; that is a
  /// completed observation with no usable signal, not a transport failure.
  virtual bool poll(uint32_t now_ms, BlinkSignalSample& sample) = 0;
};

}  // namespace colibrino
