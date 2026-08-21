#pragma once

#include <NimBLEServer.h>

#include <atomic>
#include <cstdint>

class NimBLEAdvertising;
class NimBLECharacteristic;
class NimBLEHIDDevice;

enum class BleMouseState : uint8_t {
  kDisabled,
  kIdle,
  kPairing,
  kReconnect,
  kAuthenticating,
  kSecure,
  kFault,
};

enum BleMouseEvent : uint32_t {
  kBleEventNone = 0,
  kBleEventInitialized = 1u << 0,
  kBleEventAdvertisingStarted = 1u << 1,
  kBleEventAdvertisingTimeout = 1u << 2,
  kBleEventConnected = 1u << 3,
  kBleEventSecure = 1u << 4,
  kBleEventDisconnected = 1u << 5,
  kBleEventAuthenticationFailure = 1u << 6,
  kBleEventAdvertisingFailure = 1u << 7,
  kBleEventReportFailure = 1u << 8,
  kBleEventBondCleared = 1u << 9,
};

class BleMouseTransport;

// NimBLE invokes these methods from its host task. They only publish atomic
// facts; the cooperative application loop owns disconnects and safety policy.
class BleMouseServerCallbacks final : public NimBLEServerCallbacks {
 public:
  explicit BleMouseServerCallbacks(BleMouseTransport& owner) : owner_(owner) {}

  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override;
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info,
                    int reason) override;
  void onAuthenticationComplete(NimBLEConnInfo& info) override;

 private:
  BleMouseTransport& owner_;
};

// Board transport only: bounded advertising, one persistent bond, encrypted
// HID notifications, and no automatic advertising on boot or disconnect.
class BleMouseTransport {
 public:
  static constexpr uint32_t kPairingWindowMs = 60000;
  static constexpr uint32_t kReconnectWindowMs = 30000;

  BleMouseTransport();

  bool begin(uint8_t battery_percent);
  void service(uint32_t now_ms);
  bool startWindow(uint32_t now_ms);
  bool forgetBondAndPair(uint32_t now_ms);
  void cancelWindow();
  void stopForOta();

  bool sendMotion(int8_t x, int8_t y);
  bool click(uint8_t button_mask);
  bool releaseAll();
  void setBatteryLevel(uint8_t percent);

  bool initialized() const { return initialized_; }
  bool connected() const { return connected_.load(); }
  bool secure() const { return secure_.load(); }
  bool bonded() const { return bonded_.load(); }
  bool repairRequired() const { return repair_required_.load(); }
  BleMouseState state() const {
    return static_cast<BleMouseState>(state_.load());
  }
  uint32_t windowRemainingSeconds(uint32_t now_ms) const;
  uint32_t takeEvents() { return events_.exchange(kBleEventNone); }
  int lastDisconnectReason() const { return last_disconnect_reason_.load(); }
  uint32_t successfulReports() const { return successful_reports_.load(); }
  uint32_t reportFailures() const { return report_failures_.load(); }

  static const char* stateName(BleMouseState state);

  void publishConnect(uint16_t connection_handle);
  void publishDisconnect(int reason);
  void publishAuthentication(bool encrypted, bool bonded,
                             uint16_t connection_handle);

 private:
  friend class BleMouseServerCallbacks;

  bool reloadBondAndWhitelist();
  bool startAdvertising(uint32_t now_ms, bool bonded_reconnect);
  bool notifyReport(uint8_t buttons, int8_t x, int8_t y, bool require_secure,
                    bool count_failure);
  void publishEvent(uint32_t event) { events_.fetch_or(event); }
  void markFault(uint32_t event);

  BleMouseServerCallbacks callbacks_;
  NimBLEServer* server_ = nullptr;
  NimBLEHIDDevice* hid_ = nullptr;
  NimBLECharacteristic* input_report_ = nullptr;
  NimBLEAdvertising* advertising_ = nullptr;
  bool initialized_ = false;
  std::atomic<bool> window_active_{false};
  uint32_t window_started_ms_ = 0;
  uint32_t window_duration_ms_ = 0;
  uint32_t repair_stop_started_ms_ = 0;
  bool repair_pending_ = false;
  uint8_t button_bits_ = 0;

  std::atomic<uint8_t> state_{static_cast<uint8_t>(BleMouseState::kDisabled)};
  std::atomic<bool> connected_{false};
  std::atomic<bool> secure_{false};
  std::atomic<bool> bonded_{false};
  std::atomic<bool> repair_required_{false};
  std::atomic<bool> connection_expected_bond_{false};
  std::atomic<bool> fault_latched_{false};
  std::atomic<bool> authentication_failure_pending_{false};
  std::atomic<bool> security_start_pending_{false};
  std::atomic<bool> disconnect_pending_{false};
  std::atomic<bool> advertising_complete_pending_{false};
  std::atomic<uint16_t> connection_handle_{0xffff};
  std::atomic<int> last_disconnect_reason_{0};
  std::atomic<uint32_t> events_{kBleEventNone};
  std::atomic<uint32_t> successful_reports_{0};
  std::atomic<uint32_t> report_failures_{0};
};
