#include "colibrino/mouse_output_policy.h"

namespace colibrino {

void MouseOutputPolicy::setMouseMode(bool in_mouse_mode) {
  if (mouse_mode_ && !in_mouse_mode) {
    forceLock(MouseLockReason::kModeExit);
  }
  mouse_mode_ = in_mouse_mode;
}

void MouseOutputPolicy::setCalibrated(bool calibrated) {
  if (calibrated_ && !calibrated) {
    forceLock(MouseLockReason::kInvalidTiming);
  }
  calibrated_ = calibrated;
}

void MouseOutputPolicy::setImuFresh(bool fresh) {
  if (imu_fresh_ && !fresh) {
    forceLock(MouseLockReason::kImuTimeout);
  }
  imu_fresh_ = fresh;
}

void MouseOutputPolicy::setTransportReadiness(bool usb_ready,
                                              bool ble_secure) {
  const MouseTransport previous = selected_transport_;
  const bool ready_transport_lost =
      (usb_ready_ && !usb_ready) || (ble_secure_ && !ble_secure);
  usb_ready_ = usb_ready;
  ble_secure_ = ble_secure;
  selected_transport_ = usb_ready_ ? MouseTransport::kUsb
                                   : (ble_secure_ ? MouseTransport::kBle
                                                  : MouseTransport::kNone);

  if (previous != selected_transport_ || ready_transport_lost) {
    forceLock(MouseLockReason::kTransportTopology, true);
  }
}

bool MouseOutputPolicy::requestArm() {
  if (!mouse_mode_ || !calibrated_ || !imu_fresh_ ||
      selected_transport_ == MouseTransport::kNone) {
    return false;
  }
  clearPendingMotion();
  armed_ = true;
  last_lock_reason_ = MouseLockReason::kNone;
  return true;
}

void MouseOutputPolicy::forceLock(MouseLockReason reason,
                                  bool release_even_if_locked) {
  const bool had_output_state =
      armed_ || ble_window_active_ || ble_pending_x_ != 0 || ble_pending_y_ != 0;
  armed_ = false;
  clearPendingMotion();
  last_lock_reason_ = reason;
  if (had_output_state || release_even_if_locked) {
    requestRelease(reason);
  }
}

void MouseOutputPolicy::noteReportFailure() {
  forceLock(MouseLockReason::kReportFailure, true);
}

void MouseOutputPolicy::noteAuthenticationFailure() {
  forceLock(MouseLockReason::kAuthentication, true);
}

bool MouseOutputPolicy::canReport() const {
  return armed_ && mouse_mode_ && calibrated_ && imu_fresh_ &&
         selected_transport_ != MouseTransport::kNone;
}

bool MouseOutputPolicy::takeReleaseRequest(MouseLockReason& reason) {
  if (!release_pending_) {
    return false;
  }
  release_pending_ = false;
  reason = pending_release_reason_;
  pending_release_reason_ = MouseLockReason::kNone;
  return true;
}

RoutedMouseMotion MouseOutputPolicy::routeMotion(int8_t x, int8_t y,
                                                 uint32_t now_ms) {
  RoutedMouseMotion result;
  if (!canReport()) {
    return result;
  }

  result.transport = selected_transport_;
  if (selected_transport_ == MouseTransport::kUsb) {
    if (x != 0 || y != 0) {
      result.ready = true;
      result.x = clampReport(x);
      result.y = clampReport(y);
    }
    return result;
  }

  if (!ble_window_active_) {
    if (x == 0 && y == 0) {
      return result;
    }
    ble_window_active_ = true;
    ble_window_started_ms_ = now_ms;
  }

  ble_pending_x_ = clampReport(static_cast<int16_t>(ble_pending_x_) + x);
  ble_pending_y_ = clampReport(static_cast<int16_t>(ble_pending_y_) + y);
  if (now_ms - ble_window_started_ms_ < kBleReportIntervalMs) {
    return result;
  }

  result.x = clampReport(ble_pending_x_);
  result.y = clampReport(ble_pending_y_);
  result.ready = result.x != 0 || result.y != 0;
  clearPendingMotion();
  return result;
}

int8_t MouseOutputPolicy::clampReport(int16_t value) {
  if (value > kMaximumReport) {
    return static_cast<int8_t>(kMaximumReport);
  }
  if (value < -kMaximumReport) {
    return static_cast<int8_t>(-kMaximumReport);
  }
  return static_cast<int8_t>(value);
}

void MouseOutputPolicy::clearPendingMotion() {
  ble_window_active_ = false;
  ble_window_started_ms_ = 0;
  ble_pending_x_ = 0;
  ble_pending_y_ = 0;
}

void MouseOutputPolicy::requestRelease(MouseLockReason reason) {
  if (release_pending_) {
    return;
  }
  release_pending_ = true;
  pending_release_reason_ = reason;
}

}  // namespace colibrino
