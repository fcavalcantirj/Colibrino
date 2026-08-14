#pragma once

#include <Arduino.h>
#include <esp32-hal-rmt.h>
#include <esp_arduino_version.h>

#if ESP_ARDUINO_VERSION_MAJOR < 3
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#endif

#include "colibrino/blink_input.h"

namespace colibrino {

/// StickS3 onboard-IR timing probe backed by the ESP32-S3 RMT peripheral.
///
/// The receiver exposes only a demodulated digital envelope. This adapter sends
/// a known 38 kHz burst pattern, measures active/total receive time, and returns
/// a normalized ratio. It cannot measure analog reflectance intensity and must
/// remain described as an experiment until physical eyelid tests pass.
class StickS3IrProbe final : public BlinkInput {
 public:
  /// Configures transmit/receive channels; safe to call more than once.
  bool begin() override;
  /// Runs at most once per configured interval and preserves raw timing evidence.
  bool poll(uint32_t now_ms, BlinkSignalSample& sample) override;
  bool initialized() const { return initialized_; }

 private:
  /// Clears the destination before async capture so a zero tail marks its end.
  bool armReceiver();
  /// Uses the API-specific completion primitive that guarantees copied data.
  bool receiveComplete() const;
  /// Converts captured low-level timing into the sensor-neutral sample contract.
  void fillSample(uint32_t now_ms, BlinkSignalSample& sample) const;

  static constexpr size_t kTransmitSymbolCount = 8;
  static constexpr size_t kReceiveCapacity = 64;

  rmt_data_t transmit_[kTransmitSymbolCount]{};
  rmt_data_t receive_[kReceiveCapacity]{};
  size_t received_symbols_ = kReceiveCapacity;
#if ESP_ARDUINO_VERSION_MAJOR < 3
  // Arduino-ESP32 2.x identifies RMT channels by objects and signals async
  // receive completion with a FreeRTOS event group. Version 3.x uses GPIO pins.
  rmt_obj_t* transmit_channel_ = nullptr;
  rmt_obj_t* receive_channel_ = nullptr;
  EventGroupHandle_t receive_events_ = nullptr;
#endif
  bool initialized_ = false;
  bool receiver_armed_ = false;
  uint32_t last_transmit_ms_ = 0;
};

}  // namespace colibrino
