#include "ble_mouse_transport.h"

#include <NimBLEAdvertising.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <algorithm>
#include <new>

namespace {

constexpr uint16_t kInvalidConnectionHandle = 0xffff;
constexpr uint8_t kMouseReportId = 1;

// Five buttons, signed relative X/Y/wheel, and signed AC pan. Pointer movement
// currently uses only X/Y; retaining the complete bounded report makes every
// release a single zeroed five-byte value.
uint8_t kMouseReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x05,        //     Usage Maximum (5)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x01,        //     Input (Constant)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     Usage (AC Pan)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

}  // namespace

BleMouseTransport::BleMouseTransport() : callbacks_(*this) {}

bool BleMouseTransport::begin(uint8_t battery_percent) {
  state_.store(static_cast<uint8_t>(BleMouseState::kFault));
  fault_latched_.store(true);
  if (!NimBLEDevice::init("Colibrino Head Pointer")) {
    publishEvent(kBleEventAdvertisingFailure);
    return false;
  }

  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true, false, true);

  server_ = NimBLEDevice::createServer();
  if (server_ == nullptr) {
    publishEvent(kBleEventAdvertisingFailure);
    return false;
  }
  server_->setCallbacks(&callbacks_, false);
  server_->advertiseOnDisconnect(false);

  hid_ = new (std::nothrow) NimBLEHIDDevice(server_);
  if (hid_ == nullptr) {
    publishEvent(kBleEventAdvertisingFailure);
    return false;
  }
  input_report_ = hid_->getInputReport(kMouseReportId);
  if (input_report_ == nullptr) {
    publishEvent(kBleEventAdvertisingFailure);
    return false;
  }
  hid_->setManufacturer("Colibrino");
  hid_->setPnp(0x02, 0x303A, 0x4001, 0x0100);
  hid_->setHidInfo(0x00, 0x00);
  hid_->setReportMap(kMouseReportMap, sizeof(kMouseReportMap));
  hid_->setBatteryLevel(battery_percent);
  const uint8_t released_report[5] = {};
  input_report_->setValue(released_report, sizeof(released_report));
  if (!server_->start()) {
    publishEvent(kBleEventAdvertisingFailure);
    return false;
  }

  advertising_ = NimBLEDevice::getAdvertising();
  if (advertising_ == nullptr) {
    publishEvent(kBleEventAdvertisingFailure);
    return false;
  }
  advertising_->enableScanResponse(true);
  if (!advertising_->setName("Colibrino Head Pointer") ||
      !advertising_->addServiceUUID("1812") ||
      !advertising_->setAppearance(HID_MOUSE) ||
      !advertising_->setPreferredParams(6, 12)) {
    publishEvent(kBleEventAdvertisingFailure);
    return false;
  }
  advertising_->setAdvertisingCompleteCallback(
      [this](NimBLEAdvertising*) { advertising_complete_pending_.store(true); });

  if (!reloadBondAndWhitelist()) {
    publishEvent(kBleEventAdvertisingFailure);
    return false;
  }

  initialized_ = true;
  fault_latched_.store(false);
  state_.store(static_cast<uint8_t>(BleMouseState::kIdle));
  publishEvent(kBleEventInitialized);
  return true;
}

void BleMouseTransport::service(uint32_t now_ms) {
  if (!initialized_) {
    return;
  }

  if (security_start_pending_.exchange(false)) {
    const uint16_t handle = connection_handle_.load();
    if (handle == kInvalidConnectionHandle ||
        !NimBLEDevice::startSecurity(handle)) {
      publishAuthentication(false, false, handle);
    }
  }

  if (authentication_failure_pending_.exchange(false)) {
    // Best effort while the link still exists; the policy remains locked even
    // if the peer is not yet subscribed or encryption never completed.
    releaseAll();
    disconnect_pending_.store(true);
  }

  if (disconnect_pending_.exchange(false)) {
    const uint16_t handle = connection_handle_.load();
    if (server_ != nullptr && handle != kInvalidConnectionHandle) {
      server_->disconnect(handle);
    }
  }

  const bool advertising_completed = advertising_complete_pending_.exchange(false);
  if (repair_pending_ && advertising_completed) {
    repair_pending_ = false;
    if (!NimBLEDevice::deleteAllBonds() || !reloadBondAndWhitelist()) {
      markFault(kBleEventAdvertisingFailure);
      return;
    }
    publishEvent(kBleEventBondCleared);
    startAdvertising(now_ms, false);
    return;
  }
  if (repair_pending_ && now_ms - repair_stop_started_ms_ > 1000) {
    repair_pending_ = false;
    markFault(kBleEventAdvertisingFailure);
    return;
  }

  if (advertising_completed && !connected_.load() &&
      (state() == BleMouseState::kPairing ||
       state() == BleMouseState::kReconnect)) {
    window_active_ = false;
    window_duration_ms_ = 0;
    state_.store(static_cast<uint8_t>(BleMouseState::kIdle));
    publishEvent(kBleEventAdvertisingTimeout);
  }

  if (window_active_.load() &&
      now_ms - window_started_ms_ >= window_duration_ms_) {
    window_active_ = false;
    window_duration_ms_ = 0;
    state_.store(static_cast<uint8_t>(BleMouseState::kIdle));
    if (advertising_ != nullptr && !advertising_->stop()) {
      markFault(kBleEventAdvertisingFailure);
      return;
    }
    publishEvent(kBleEventAdvertisingTimeout);
  }
}

bool BleMouseTransport::startWindow(uint32_t now_ms) {
  if (!initialized_ || connected_.load() || advertising_ == nullptr ||
      repair_required_.load()) {
    return false;
  }
  fault_latched_.store(false);
  if (!reloadBondAndWhitelist()) {
    markFault(kBleEventAdvertisingFailure);
    return false;
  }
  return startAdvertising(now_ms, bonded_.load());
}

bool BleMouseTransport::forgetBondAndPair(uint32_t now_ms) {
  if (!initialized_ || connected_.load()) {
    return false;
  }

  if (repair_required_.load()) {
    if (!NimBLEDevice::deleteAllBonds() || !reloadBondAndWhitelist()) {
      markFault(kBleEventAdvertisingFailure);
      return false;
    }
    repair_required_.store(false);
    publishEvent(kBleEventBondCleared);
    return startAdvertising(now_ms, false);
  }

  if (!bonded_.load() || state() != BleMouseState::kReconnect) {
    return false;
  }

  window_active_ = false;
  repair_pending_ = true;
  repair_stop_started_ms_ = now_ms;
  state_.store(static_cast<uint8_t>(BleMouseState::kIdle));
  if (advertising_ != nullptr && !advertising_->stop()) {
    repair_pending_ = false;
    markFault(kBleEventAdvertisingFailure);
    return false;
  }
  return true;
}

void BleMouseTransport::cancelWindow() {
  if (!initialized_ ||
      (state() != BleMouseState::kPairing &&
       state() != BleMouseState::kReconnect)) {
    return;
  }
  window_active_ = false;
  window_duration_ms_ = 0;
  repair_pending_ = false;
  state_.store(static_cast<uint8_t>(BleMouseState::kIdle));
  if (advertising_ == nullptr || !advertising_->stop()) {
    markFault(kBleEventAdvertisingFailure);
    return;
  }
  publishEvent(kBleEventAdvertisingTimeout);
}

void BleMouseTransport::stopForOta() {
  if (!initialized_) {
    return;
  }
  window_active_ = false;
  window_duration_ms_ = 0;
  repair_pending_ = false;
  state_.store(static_cast<uint8_t>(BleMouseState::kIdle));
  if (advertising_ != nullptr) {
    advertising_->stop();
  }
  releaseAll();
  const uint16_t handle = connection_handle_.load();
  if (server_ != nullptr && handle != kInvalidConnectionHandle) {
    server_->disconnect(handle);
  }
}

bool BleMouseTransport::sendMotion(int8_t x, int8_t y) {
  return notifyReport(button_bits_, x, y, true, true);
}

bool BleMouseTransport::click(uint8_t button_mask) {
  button_bits_ |= button_mask;
  const bool pressed = notifyReport(button_bits_, 0, 0, true, true);
  button_bits_ &= static_cast<uint8_t>(~button_mask);
  // If the press failed it already latched a transport fault. Still attempt a
  // zero-button notification before the pending disconnect, without turning
  // that best-effort cleanup into a second failure count.
  const bool released =
      notifyReport(button_bits_, 0, 0, pressed, pressed);
  return pressed && released;
}

bool BleMouseTransport::releaseAll() {
  button_bits_ = 0;
  if (!connected_.load()) {
    return true;
  }
  return notifyReport(0, 0, 0, false, false);
}

void BleMouseTransport::setBatteryLevel(uint8_t percent) {
  if (initialized_ && hid_ != nullptr) {
    hid_->setBatteryLevel(std::min<uint8_t>(percent, 100));
  }
}

uint32_t BleMouseTransport::windowRemainingSeconds(uint32_t now_ms) const {
  if (!window_active_.load()) {
    return 0;
  }
  const uint32_t elapsed = now_ms - window_started_ms_;
  if (elapsed >= window_duration_ms_) {
    return 0;
  }
  return (window_duration_ms_ - elapsed + 999) / 1000;
}

const char* BleMouseTransport::stateName(BleMouseState state) {
  switch (state) {
    case BleMouseState::kDisabled:
      return "DISABLED";
    case BleMouseState::kIdle:
      return "IDLE";
    case BleMouseState::kPairing:
      return "PAIRING";
    case BleMouseState::kReconnect:
      return "RECONNECT";
    case BleMouseState::kAuthenticating:
      return "AUTH";
    case BleMouseState::kSecure:
      return "SECURE";
    case BleMouseState::kFault:
      return "FAULT";
  }
  return "FAULT";
}

void BleMouseTransport::publishConnect(uint16_t connection_handle) {
  connection_handle_.store(connection_handle);
  connected_.store(true);
  secure_.store(false);
  window_active_ = false;
  state_.store(static_cast<uint8_t>(BleMouseState::kAuthenticating));
  publishEvent(kBleEventConnected);
  security_start_pending_.store(true);
}

void BleMouseTransport::publishDisconnect(int reason) {
  connected_.store(false);
  secure_.store(false);
  security_start_pending_.store(false);
  disconnect_pending_.store(false);
  connection_handle_.store(kInvalidConnectionHandle);
  last_disconnect_reason_.store(reason);
  state_.store(static_cast<uint8_t>(fault_latched_.load()
                                        ? BleMouseState::kFault
                                        : BleMouseState::kIdle));
  publishEvent(kBleEventDisconnected);
}

void BleMouseTransport::publishAuthentication(bool encrypted, bool bonded,
                                              uint16_t connection_handle) {
  if (encrypted && bonded && connected_.load() &&
      connection_handle_.load() == connection_handle) {
    secure_.store(true);
    bonded_.store(true);
    repair_required_.store(false);
    fault_latched_.store(false);
    state_.store(static_cast<uint8_t>(BleMouseState::kSecure));
    publishEvent(kBleEventSecure);
    return;
  }

  secure_.store(false);
  if (connection_expected_bond_.load()) {
    // NimBLE's server retries repeat pairing by deleting the old key. Bonding
    // is disabled during reconnect, so that retry cannot become report-ready;
    // require the explicit five-second repair gesture before public pairing.
    repair_required_.store(true);
  }
  fault_latched_.store(true);
  state_.store(static_cast<uint8_t>(BleMouseState::kFault));
  authentication_failure_pending_.store(true);
  publishEvent(kBleEventAuthenticationFailure);
}

bool BleMouseTransport::reloadBondAndWhitelist() {
  while (NimBLEDevice::getWhiteListCount() > 0) {
    const NimBLEAddress address = NimBLEDevice::getWhiteListAddress(0);
    if (!NimBLEDevice::whiteListRemove(address)) {
      return false;
    }
  }

  const int bonds = NimBLEDevice::getNumBonds();
  if (bonds < 0 || bonds > 1) {
    return false;
  }
  bonded_.store(bonds == 1);
  return bonds == 0 ||
         NimBLEDevice::whiteListAdd(NimBLEDevice::getBondedAddress(0));
}

bool BleMouseTransport::startAdvertising(uint32_t now_ms,
                                         bool bonded_reconnect) {
  // A reconnect window may use an existing key but may not create a new one.
  // This neutralizes NimBLE's automatic repeat-pairing retry: a peer that lost
  // its key fails the bonded gate and must use the physical repair gesture.
  NimBLEDevice::setSecurityAuth(!bonded_reconnect, false, true);
  connection_expected_bond_.store(bonded_reconnect);
  advertising_->setScanFilter(bonded_reconnect, bonded_reconnect);
  window_started_ms_ = now_ms;
  window_duration_ms_ =
      bonded_reconnect ? kReconnectWindowMs : kPairingWindowMs;
  window_active_ = true;
  state_.store(static_cast<uint8_t>(bonded_reconnect
                                        ? BleMouseState::kReconnect
                                        : BleMouseState::kPairing));
  if (!advertising_->start(window_duration_ms_)) {
    window_active_ = false;
    window_duration_ms_ = 0;
    markFault(kBleEventAdvertisingFailure);
    return false;
  }
  publishEvent(kBleEventAdvertisingStarted);
  return true;
}

bool BleMouseTransport::notifyReport(uint8_t buttons, int8_t x, int8_t y,
                                     bool require_secure,
                                     bool count_failure) {
  if (input_report_ == nullptr || !connected_.load() ||
      (require_secure && !secure_.load())) {
    if (count_failure) {
      markFault(kBleEventReportFailure);
    }
    return false;
  }

  const uint8_t report[5] = {
      buttons, static_cast<uint8_t>(x), static_cast<uint8_t>(y), 0, 0};
  if (!input_report_->notify(report, sizeof(report), connection_handle_.load())) {
    if (count_failure) {
      markFault(kBleEventReportFailure);
    }
    return false;
  }
  successful_reports_.fetch_add(1);
  return true;
}

void BleMouseTransport::markFault(uint32_t event) {
  if (event == kBleEventReportFailure) {
    report_failures_.fetch_add(1);
  }
  secure_.store(false);
  fault_latched_.store(true);
  state_.store(static_cast<uint8_t>(BleMouseState::kFault));
  disconnect_pending_.store(connected_.load());
  publishEvent(event);
}

void BleMouseServerCallbacks::onConnect(NimBLEServer*, NimBLEConnInfo& info) {
  owner_.publishConnect(info.getConnHandle());
}

void BleMouseServerCallbacks::onDisconnect(NimBLEServer*, NimBLEConnInfo&,
                                           int reason) {
  owner_.publishDisconnect(reason);
}

void BleMouseServerCallbacks::onAuthenticationComplete(NimBLEConnInfo& info) {
  owner_.publishAuthentication(info.isEncrypted(), info.isBonded(),
                               info.getConnHandle());
}
