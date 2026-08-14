#include "ir_probe.h"

#include <cstring>

#include "colibrino/config.h"

namespace colibrino {

bool StickS3IrProbe::begin() {
  if (initialized_) {
    return true;
  }

  // A 1 MHz RMT clock makes every duration unit one microsecond. M5Stack's
  // pinned PlatformIO release uses Arduino-ESP32 2.x; the conditional also
  // keeps this source compatible with the pin-oriented 3.x RMT API.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!rmtInit(config::kIrTransmitPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1,
               1000000) ||
      !rmtInit(config::kIrReceivePin, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_2,
               1000000)) {
    return false;
  }

  if (!rmtSetCarrier(config::kIrTransmitPin, true, true,
                     config::kIrCarrierHz, config::kIrCarrierDuty) ||
      !rmtSetEOT(config::kIrTransmitPin, LOW) ||
      !rmtSetRxMaxThreshold(config::kIrReceivePin, 3000) ||
      !rmtSetRxMinThreshold(config::kIrReceivePin, 30)) {
    return false;
  }
#else
  transmit_channel_ =
      rmtInit(config::kIrTransmitPin, RMT_TX_MODE, RMT_MEM_64);
  receive_channel_ =
      rmtInit(config::kIrReceivePin, RMT_RX_MODE, RMT_MEM_128);
  receive_events_ = xEventGroupCreate();
  if (transmit_channel_ == nullptr || receive_channel_ == nullptr ||
      receive_events_ == nullptr) {
    return false;
  }
  const float transmit_tick_ns = rmtSetTick(transmit_channel_, 1000.0f);
  const float receive_tick_ns = rmtSetTick(receive_channel_, 1000.0f);
  if (transmit_tick_ns < 999.0f || transmit_tick_ns > 1001.0f ||
      receive_tick_ns < 999.0f || receive_tick_ns > 1001.0f ||
      // At a 1 us RMT tick, 9 high + 17 low is approximately 38.46 kHz
      // with a 34.6 percent duty cycle.
      !rmtSetCarrier(transmit_channel_, true, true, 17, 9) ||
      !rmtSetRxThreshold(receive_channel_, 3000) ||
      !rmtSetFilter(receive_channel_, true, 30)) {
    return false;
  }
#endif

  // Eight carrier bursts, each separated by an equally long dark interval.
  // The onboard receiver only exposes a demodulated digital envelope, so this
  // is deliberately a presence/shape probe rather than an intensity reading.
  for (auto& symbol : transmit_) {
    symbol.level0 = HIGH;
    symbol.duration0 = 500;
    symbol.level1 = LOW;
    symbol.duration1 = 500;
  }

  initialized_ = true;
  return armReceiver();
}

bool StickS3IrProbe::poll(uint32_t now_ms, BlinkSignalSample& sample) {
  // Always clear caller-visible data first so an early return cannot reuse a
  // valid sample from a previous poll.
  sample = {};
  sample.timestamp_ms = now_ms;
  if (!initialized_ || now_ms - last_transmit_ms_ < config::kIrSampleIntervalMs) {
    return false;
  }
  last_transmit_ms_ = now_ms;

  if (receiver_armed_ && receiveComplete()) {
    // Consume a late completion from the previous envelope before rearming.
    fillSample(now_ms, sample);
    receiver_armed_ = false;
  }

  if (!receiver_armed_ && !armReceiver()) {
    return false;
  }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  const bool transmitted =
      rmtWrite(config::kIrTransmitPin, transmit_, kTransmitSymbolCount, 20);
#else
  const bool transmitted =
      rmtWriteBlocking(transmit_channel_, transmit_, kTransmitSymbolCount);
#endif
  if (!transmitted) {
    return false;
  }

  // Allow the 8 ms envelope plus the receiver's 3 ms idle terminator. If
  // nothing arrives, the async receive remains armed for the next burst.
  const uint32_t wait_started = millis();
  while (millis() - wait_started < 13) {
    if (receiveComplete()) {
      fillSample(now_ms, sample);
      receiver_armed_ = false;
      armReceiver();
      return true;
    }
    delayMicroseconds(100);
  }
  return true;
}

bool StickS3IrProbe::armReceiver() {
  std::memset(receive_, 0, sizeof(receive_));
  received_symbols_ = kReceiveCapacity;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  receiver_armed_ = rmtReadAsync(config::kIrReceivePin, receive_,
                                 &received_symbols_);
#else
  receiver_armed_ = rmtReadAsync(receive_channel_, receive_, kReceiveCapacity,
                                 receive_events_, false, 0);
#endif
  return receiver_armed_;
}

bool StickS3IrProbe::receiveComplete() const {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return rmtReceiveCompleted(config::kIrReceivePin);
#else
  // Arduino-ESP32 2.x sets rx_completed before it copies the RMT buffer, then
  // sets this event bit after the copy. Checking the event avoids consuming a
  // partially written frame.
  return receive_events_ != nullptr &&
         (xEventGroupGetBits(receive_events_) & RMT_FLAG_RX_DONE) != 0;
#endif
}

void StickS3IrProbe::fillSample(uint32_t now_ms,
                                BlinkSignalSample& sample) const {
  sample.timestamp_ms = now_ms;
  // Arduino-ESP32 2.x does not return the received count. The destination was
  // zeroed before arming, so trimming zero-valued tail entries works for both
  // API generations.
  size_t symbol_count = received_symbols_;
  while (symbol_count > 0 && receive_[symbol_count - 1].val == 0) {
    --symbol_count;
  }
  sample.symbol_count = static_cast<uint16_t>(symbol_count);

  for (size_t index = 0; index < symbol_count; ++index) {
    const auto& symbol = receive_[index];
    sample.total_us += symbol.duration0 + symbol.duration1;
    if (symbol.level0 == LOW) {
      sample.active_us += symbol.duration0;
    }
    if (symbol.level1 == LOW) {
      sample.active_us += symbol.duration1;
    }
  }

  // Active-low matches the integrated demodulating receiver. The ratio removes
  // absolute frame length but is not equivalent to analog light intensity.
  sample.valid = symbol_count > 0 && sample.total_us > 0;
  sample.value = sample.valid
                     ? static_cast<float>(sample.active_us) /
                           static_cast<float>(sample.total_us)
                     : 0.0f;
}

}  // namespace colibrino
