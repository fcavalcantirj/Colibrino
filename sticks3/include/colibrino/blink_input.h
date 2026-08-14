#pragma once

#include <stdint.h>

namespace colibrino {

// Sensor-neutral sample consumed by the blink classifier. A future analog
// TCRT5000 implementation can provide the same normalized value without
// changing the application or USB click path.
struct BlinkSignalSample {
  uint32_t timestamp_ms = 0;
  bool valid = false;
  float value = 0.0f;
  uint16_t symbol_count = 0;
  uint32_t active_us = 0;
  uint32_t total_us = 0;
};

class BlinkInput {
 public:
  virtual ~BlinkInput() = default;
  virtual bool begin() = 0;
  virtual bool poll(uint32_t now_ms, BlinkSignalSample& sample) = 0;
};

}  // namespace colibrino
