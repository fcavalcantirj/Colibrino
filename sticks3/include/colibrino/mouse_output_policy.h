#pragma once

#include <cstdint>

namespace colibrino {

enum class MouseTransport : uint8_t { kNone, kUsb, kBle };

enum class MouseLockReason : uint8_t {
  kNone,
  kPhysical,
  kModeExit,
  kOta,
  kTransportTopology,
  kAdvertisingFailure,
  kAuthentication,
  kReportFailure,
  kImuTimeout,
  kInvalidTiming,
};

struct RoutedMouseMotion {
  bool ready = false;
  MouseTransport transport = MouseTransport::kNone;
  int8_t x = 0;
  int8_t y = 0;
};

// Portable safety authority for pointer output. Hardware transports may emit a
// report only when this policy returns one or canReport() authorizes a click.
// It deliberately contains no Arduino, USB, BLE, Wi-Fi, or RTOS dependency.
class MouseOutputPolicy {
 public:
  static constexpr uint32_t kBleReportIntervalMs = 8;
  static constexpr int16_t kMaximumReport = 60;

  void setMouseMode(bool in_mouse_mode);
  void setCalibrated(bool calibrated);
  void setImuFresh(bool fresh);

  // USB is intentionally preferred when both transports are ready. Any
  // selected-transport change or ready-transport loss requests release-all
  // and leaves output locked; reconnection never restores arming.
  void setTransportReadiness(bool usb_ready, bool ble_secure);

  bool requestArm();
  void forceLock(MouseLockReason reason, bool release_even_if_locked = false);
  void noteReportFailure();
  void noteAuthenticationFailure();

  bool armed() const { return armed_; }
  bool canReport() const;
  MouseTransport selectedTransport() const { return selected_transport_; }
  MouseLockReason lastLockReason() const { return last_lock_reason_; }

  // Returns one pending both-sides release request. Multiple observations of
  // the same already-locked fault collapse until the application consumes it.
  bool takeReleaseRequest(MouseLockReason& reason);

  // USB motion is returned immediately. BLE motion is accumulated only inside
  // the current short cadence window, clamped to the signed safety bound, then
  // cleared in full so overflow can never become delayed cursor movement.
  RoutedMouseMotion routeMotion(int8_t x, int8_t y, uint32_t now_ms);

 private:
  static int8_t clampReport(int16_t value);
  void clearPendingMotion();
  void requestRelease(MouseLockReason reason);

  bool mouse_mode_ = false;
  bool calibrated_ = false;
  bool imu_fresh_ = false;
  bool usb_ready_ = false;
  bool ble_secure_ = false;
  bool armed_ = false;
  bool release_pending_ = false;
  MouseTransport selected_transport_ = MouseTransport::kNone;
  MouseLockReason last_lock_reason_ = MouseLockReason::kNone;
  MouseLockReason pending_release_reason_ = MouseLockReason::kNone;
  bool ble_window_active_ = false;
  uint32_t ble_window_started_ms_ = 0;
  int16_t ble_pending_x_ = 0;
  int16_t ble_pending_y_ = 0;
};

// A hold that began on another screen is blocked until a complete release.
// After an action fires, the same continuous hold cannot fire it again.
class DeliberateHoldGate {
 public:
  void blockUntilRelease() { blocked_ = true; }
  void observe(bool pressed) {
    if (!pressed) {
      blocked_ = false;
    }
  }
  bool trigger(bool pressed, bool threshold_reached) {
    if (blocked_ || !pressed || !threshold_reached) {
      return false;
    }
    blocked_ = true;
    return true;
  }
  bool blocked() const { return blocked_; }

 private:
  bool blocked_ = false;
};

}  // namespace colibrino
