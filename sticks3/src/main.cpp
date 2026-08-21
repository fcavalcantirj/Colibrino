// StickS3 application layer.
//
// This file is intentionally the only owner of host HID side effects and the
// M5PM1 external-power decision. Portable motion and blink code can recommend a
// delta or event, but only the guarded application state may send it to a host.
#include <Arduino.h>
#include <M5Unified.h>
#include <USB.h>
#include <USBHIDMouse.h>
#include <esp_heap_caps.h>

#include <atomic>

#if __has_include("colibrino_secrets.h")
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include "colibrino_secrets.h"
#define COLIBRINO_OTA_CONFIGURED 1
#else
#define COLIBRINO_OTA_CONFIGURED 0
#endif

#include "colibrino/config.h"
#include "colibrino/imu_blink_detector.h"
#include "colibrino/mouse_output_policy.h"
#include "colibrino/motion_controller.h"
#include "colibrino/signal_analysis.h"
#include "ble_mouse_transport.h"
#include "telemetry_mux.h"
#include "ir_probe.h"

#if ARDUINO_USB_MODE != 0
#error "Colibrino USB HID requires native USB mode (ARDUINO_USB_MODE=0)"
#endif

namespace {

using colibrino::BlinkFeasibilityProtocol;
using colibrino::BlinkSignalSample;
using colibrino::FeasibilityStage;
using colibrino::GyroBiasCalibrator;
using colibrino::ImuBlinkDetector;
using colibrino::DeliberateHoldGate;
using colibrino::MouseLockReason;
using colibrino::MouseOutputPolicy;
using colibrino::MouseTransport;
using colibrino::MotionConfig;
using colibrino::MotionController;
using colibrino::PointerDelta;
using colibrino::StickS3IrProbe;
using colibrino::Vec3;

/// User-visible pages. Mode is also a safety boundary: only kMouse may emit HID.
enum class AppMode : uint8_t { kIrProbe, kMotionMonitor, kMouse };

class MouseOutputRouter {
 public:
  MouseOutputRouter(USBHIDMouse& usb, BleMouseTransport& ble)
      : usb_(usb), ble_(ble) {}

  void setSafety(bool mouse_mode, bool calibrated, bool imu_fresh) {
    policy_.setMouseMode(mouse_mode);
    policy_.setCalibrated(calibrated);
    policy_.setImuFresh(imu_fresh);
  }
  void setTransportReadiness(bool usb_ready, bool ble_secure) {
    policy_.setTransportReadiness(usb_ready, ble_secure);
  }
  bool requestArm() { return policy_.requestArm(); }
  void forceLock(MouseLockReason reason, bool release_even_if_locked = false) {
    policy_.forceLock(reason, release_even_if_locked);
  }
  void noteAuthenticationFailure() { policy_.noteAuthenticationFailure(); }
  bool armed() const { return policy_.armed(); }
  bool canReport() const { return policy_.canReport(); }
  MouseTransport selectedTransport() const {
    return policy_.selectedTransport();
  }
  bool takeReleaseRequest(MouseLockReason& reason) {
    return policy_.takeReleaseRequest(reason);
  }

  void releaseAll() {
    usb_.release(MOUSE_LEFT);
    usb_.release(MOUSE_RIGHT);
    usb_.release(MOUSE_MIDDLE);
    ble_.releaseAll();
  }

  bool move(int8_t x, int8_t y, uint32_t now_ms) {
    const auto report = policy_.routeMotion(x, y, now_ms);
    if (!report.ready) {
      return true;
    }
    if (report.transport == MouseTransport::kUsb) {
      usb_.move(report.x, report.y);
      return true;
    }
    if (report.transport == MouseTransport::kBle &&
        ble_.sendMotion(report.x, report.y)) {
      return true;
    }
    policy_.noteReportFailure();
    return false;
  }

  bool click(uint8_t button) {
    if (!policy_.canReport()) {
      return false;
    }
    if (policy_.selectedTransport() == MouseTransport::kUsb) {
      usb_.click(button);
      return true;
    }
    if (policy_.selectedTransport() == MouseTransport::kBle &&
        ble_.click(button)) {
      return true;
    }
    policy_.noteReportFailure();
    return false;
  }

 private:
  USBHIDMouse& usb_;
  BleMouseTransport& ble_;
  MouseOutputPolicy policy_;
};

/// High-rate diagnostic stages used to test whether a firmly mounted BMI270
/// can distinguish deliberate blinks from stillness and ordinary head motion.
/// A complete probe can validate the conservative runtime blink-sequence gate,
/// but it never emits HID while capture is active.
enum class ImuProbeStage : uint8_t {
  kIdle,
  kPrepareStill,
  kCaptureStill,
  kPrepareBlinks,
  kCaptureBlinks,
  kPrepareHeadMotion,
  kCaptureHeadMotion,
  // Optional cable-free capture block: streams IMU rows without the guided
  // timing so 30-second rest baselines and long confounders fit one block.
  // It never contributes to the validation predicate.
  kCaptureFreeRun,
  kResult,
};

// Long-lived device services. USB interfaces are registered before USB.begin()
// so CDC diagnostics and HID mouse enumerate as one native TinyUSB composite.
USBHIDMouse usb_mouse;
USBCDC usb_cdc;
// All diagnostics go through the mux: byte-identical USB CDC output plus an
// optional read-only TCP mirror for cable-free capture (telemetry_mux.h).
TelemetryMux diagnostics(usb_cdc);
BleMouseTransport ble_mouse;
MouseOutputRouter mouse_output(usb_mouse, ble_mouse);
StickS3IrProbe ir_probe;
BlinkFeasibilityProtocol feasibility;
MotionController motion(MotionConfig{
    colibrino::config::kHorizontalGyroAxis,
    colibrino::config::kHorizontalSign,
    colibrino::config::kVerticalGyroAxis,
    colibrino::config::kVerticalSign,
    colibrino::config::kGyroDeadzoneDps,
    colibrino::config::kPointerPixelsPerDegree,
    colibrino::config::kMotionLowPassAlpha,
    colibrino::config::kMaximumMouseReport,
});
GyroBiasCalibrator gyro_calibrator;
ImuBlinkDetector probe_imu_blink_detector;
ImuBlinkDetector runtime_imu_blink_detector;

// Safety and input latches. The `*_hold_latched` flags ensure a continued
// physical hold toggles state once rather than every loop iteration.
AppMode mode = AppMode::kIrProbe;
bool ir_powered = false;
bool ir_hold_latched = false;
DeliberateHoldGate mouse_hold_gate;
bool motion_hold_latched = false;
bool motion_calibrated = false;
bool feasibility_result_logged = false;
bool imu_blink_validated = false;
uint32_t calibration_started_ms = 0;
uint32_t last_imu_timestamp_us = 0;
uint32_t last_imu_sample_ms = 0;
uint32_t last_screen_ms = 0;
uint32_t last_ir_log_ms = 0;
uint32_t last_status_log_ms = 0;
uint32_t imu_probe_stage_started_ms = 0;
uint32_t imu_probe_samples = 0;
uint32_t imu_still_sequences = 0;
uint32_t imu_blink_sequences = 0;
uint32_t imu_head_sequences = 0;
uint32_t imu_free_sequences = 0;
std::atomic<bool> usb_suspended{false};
bool coex_window_started_this_arm = false;
bool coex_window_active = false;
uint32_t coex_window_started_ms = 0;
uint32_t coex_samples = 0;
uint32_t coex_reports_started = 0;
uint32_t coex_failures_started = 0;
uint32_t coex_last_imu_us = 0;
uint32_t coex_max_gap_us = 0;
Vec3 gyro_dps{};
Vec3 gyro_bias{};
Vec3 accel_g{};
BlinkSignalSample latest_ir{};
colibrino::BlinkDetector runtime_blink_detector;

ImuProbeStage imu_probe_stage = ImuProbeStage::kIdle;

constexpr uint32_t kImuPrepareMs = 3000;
constexpr uint32_t kImuStillCaptureMs = 6000;
constexpr uint32_t kImuBlinkCaptureMs = 15000;
constexpr uint32_t kImuHeadCaptureMs = 12000;
// Free-run capture is bounded so a forgotten session cannot stream forever.
constexpr uint32_t kImuFreeRunCaptureMs = 90000;
constexpr uint32_t kBlueHoldMs = 2000;
constexpr uint32_t kRepairHoldMs = 5000;

#ifndef COLIBRINO_BUILD_ID
#define COLIBRINO_BUILD_ID "unknown"
#endif

#if COLIBRINO_OTA_CONFIGURED
constexpr char kOtaHostname[] = "sticks3-ptt";
bool ota_network_started = false;
bool ota_service_started = false;
bool ota_in_progress = false;
uint8_t ota_last_percent = 255;
uint32_t ota_network_started_ms = 0;
#endif

const char* modeName(AppMode value) {
  switch (value) {
    case AppMode::kIrProbe:
      return "IR PROBE";
    case AppMode::kMotionMonitor:
      return "MOTION";
    case AppMode::kMouse:
      return "MOUSE";
  }
  return "UNKNOWN";
}

const char* transportName(MouseTransport transport) {
  switch (transport) {
    case MouseTransport::kNone:
      return "NONE";
    case MouseTransport::kUsb:
      return "USB";
    case MouseTransport::kBle:
      return "BLE";
  }
  return "NONE";
}

const char* lockReasonName(MouseLockReason reason) {
  switch (reason) {
    case MouseLockReason::kNone:
      return "NONE";
    case MouseLockReason::kPhysical:
      return "PHYSICAL";
    case MouseLockReason::kModeExit:
      return "MODE_EXIT";
    case MouseLockReason::kOta:
      return "OTA";
    case MouseLockReason::kTransportTopology:
      return "TRANSPORT_TOPOLOGY";
    case MouseLockReason::kAdvertisingFailure:
      return "ADVERTISING_FAILURE";
    case MouseLockReason::kAuthentication:
      return "AUTHENTICATION";
    case MouseLockReason::kReportFailure:
      return "REPORT_FAILURE";
    case MouseLockReason::kImuTimeout:
      return "IMU_TIMEOUT";
    case MouseLockReason::kInvalidTiming:
      return "INVALID_TIMING";
  }
  return "UNKNOWN";
}

uint32_t freeInternalHeap() {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

uint32_t largestInternalHeapBlock() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                          MALLOC_CAP_8BIT);
}

void logHeap(const char* stage) {
  diagnostics.printf("EVENT,BLE_HEAP,%s,free=%lu,largest=%lu\n", stage,
                     static_cast<unsigned long>(freeInternalHeap()),
                     static_cast<unsigned long>(largestInternalHeapBlock()));
}

void settleMouseOutputLock() {
  MouseLockReason reason;
  if (!mouse_output.takeReleaseRequest(reason)) {
    return;
  }
  // The policy has already cleared armed state and pending BLE motion. Release
  // both transports before resetting producer state; no failover can preserve
  // an armed session or a partial click sequence.
  mouse_output.releaseAll();
  motion.reset();
  runtime_blink_detector.reset();
  runtime_imu_blink_detector.reset();
  diagnostics.printf("EVENT,MOUSE_OUTPUT,LOCKED,reason=%s\n",
                     lockReasonName(reason));
  last_screen_ms = 0;
}

void forceMouseLock(MouseLockReason reason,
                    bool release_even_if_locked = false) {
  mouse_output.forceLock(reason, release_even_if_locked);
  settleMouseOutputLock();
}

bool armMouseOutput() {
  if (!mouse_output.requestArm()) {
    return false;
  }
  motion.reset();
  runtime_blink_detector.reset();
  runtime_imu_blink_detector.reset();
  coex_window_started_this_arm = false;
  coex_window_active = false;
  diagnostics.printf("EVENT,MOUSE_OUTPUT,ARMED,hid=%s\n",
                     transportName(mouse_output.selectedTransport()));
  last_screen_ms = 0;
  return true;
}

void onUsbEvent(void*, esp_event_base_t, int32_t event_id, void*) {
  if (event_id == ARDUINO_USB_STOPPED_EVENT ||
      event_id == ARDUINO_USB_SUSPEND_EVENT) {
    usb_suspended.store(true);
  } else if (event_id == ARDUINO_USB_STARTED_EVENT ||
             event_id == ARDUINO_USB_RESUME_EVENT) {
    usb_suspended.store(false);
  }
}

bool imuFresh(uint32_t now_ms) {
  return motion_calibrated && last_imu_sample_ms != 0 &&
         now_ms - last_imu_sample_ms <= 200;
}

void logBleEvents(uint32_t events, uint32_t now_ms) {
  if (events & kBleEventInitialized) {
    diagnostics.printf("EVENT,BLE,INITIALIZED,bonded=%u\n",
                       ble_mouse.bonded() ? 1u : 0u);
    logHeap("INITIALIZED");
  }
  if (events & kBleEventAdvertisingStarted) {
    diagnostics.printf("EVENT,BLE,ADVERTISING,%s,seconds=%lu\n",
                       BleMouseTransport::stateName(ble_mouse.state()),
                       static_cast<unsigned long>(
                           ble_mouse.windowRemainingSeconds(now_ms)));
    logHeap("ADVERTISING");
  }
  if (events & kBleEventAdvertisingTimeout) {
    diagnostics.println("EVENT,BLE,ADVERTISING_STOPPED");
  }
  if (events & kBleEventConnected) {
    diagnostics.println("EVENT,BLE,CONNECTED,output=LOCKED");
  }
  if (events & kBleEventSecure) {
    diagnostics.printf("EVENT,BLE,SECURE,bonded=%u,output=LOCKED\n",
                       ble_mouse.bonded() ? 1u : 0u);
    logHeap("SECURE");
  }
  if (events & kBleEventDisconnected) {
    diagnostics.printf("EVENT,BLE,DISCONNECTED,reason=%d\n",
                       ble_mouse.lastDisconnectReason());
    logHeap("DISCONNECTED");
  }
  if (events & kBleEventAuthenticationFailure) {
    diagnostics.println("EVENT,BLE,AUTHENTICATION_FAILED");
  }
  if (events & kBleEventAdvertisingFailure) {
    diagnostics.println("EVENT,BLE,ADVERTISING_FAILED");
  }
  if (events & kBleEventReportFailure) {
    diagnostics.printf("EVENT,BLE,REPORT_FAILED,total=%lu\n",
                       static_cast<unsigned long>(ble_mouse.reportFailures()));
  }
  if (events & kBleEventBondCleared) {
    diagnostics.println("EVENT,BLE,BOND_CLEARED");
  }
}

void updateMouseOutputState(uint32_t now_ms) {
  ble_mouse.service(now_ms);
  const uint32_t events = ble_mouse.takeEvents();
  if (events & kBleEventAuthenticationFailure) {
    mouse_output.noteAuthenticationFailure();
  }
  if (events & kBleEventAdvertisingFailure) {
    mouse_output.forceLock(MouseLockReason::kAdvertisingFailure, true);
  }
  mouse_output.setSafety(mode == AppMode::kMouse, motion_calibrated,
                         imuFresh(now_ms));
  const bool usb_ready = static_cast<bool>(USB) && !usb_suspended.load();
  const MouseTransport previous_transport =
      mouse_output.selectedTransport();
  mouse_output.setTransportReadiness(usb_ready, ble_mouse.secure());
  if (previous_transport != mouse_output.selectedTransport() &&
      M5.BtnA.isPressed()) {
    mouse_hold_gate.blockUntilRelease();
  }
  settleMouseOutputLock();
  logBleEvents(events, now_ms);
}

void finishCoexWindow(uint32_t now_ms, const char* reason) {
  if (!coex_window_active) {
    return;
  }
  coex_window_active = false;
  diagnostics.printf(
      "EVENT,COEX_IMU,END,elapsed=%lu,samples=%lu,reports=%lu,failures=%lu,max_gap_us=%lu,reason=%s\n",
      static_cast<unsigned long>(now_ms - coex_window_started_ms),
      static_cast<unsigned long>(coex_samples),
      static_cast<unsigned long>(ble_mouse.successfulReports() -
                                 coex_reports_started),
      static_cast<unsigned long>(ble_mouse.reportFailures() -
                                 coex_failures_started),
      static_cast<unsigned long>(coex_max_gap_us), reason);
  logHeap("COEX_END");
}

void updateCoexWindow(uint32_t now_ms) {
  const bool eligible = diagnostics.clientConnected() &&
                        mouse_output.armed() && ble_mouse.secure() &&
                        mouse_output.selectedTransport() == MouseTransport::kBle;
  if (coex_window_active) {
    if (!eligible) {
      finishCoexWindow(now_ms, "CONDITION_LOST");
    } else if (now_ms - coex_window_started_ms >= 30000) {
      finishCoexWindow(now_ms, "COMPLETE");
    }
    return;
  }
  if (!eligible || coex_window_started_this_arm) {
    return;
  }

  coex_window_started_this_arm = true;
  coex_window_active = true;
  coex_window_started_ms = now_ms;
  coex_samples = 0;
  coex_reports_started = ble_mouse.successfulReports();
  coex_failures_started = ble_mouse.reportFailures();
  coex_last_imu_us = 0;
  coex_max_gap_us = 0;
  diagnostics.println(
      "EVENT,COEX_IMU,START,cap_ms=30000,reports=0,failures=0");
  diagnostics.println(
      "CSV: COEX_IMU,ms,usec,gx_dps,gy_dps,gz_dps,sample,reports,failures");
  logHeap("COEX_START");
}

const char* imuProbeStageName(ImuProbeStage value) {
  switch (value) {
    case ImuProbeStage::kIdle:
      return "IDLE";
    case ImuProbeStage::kPrepareStill:
      return "PREPARE_STILL";
    case ImuProbeStage::kCaptureStill:
      return "KEEP_HEAD_STILL";
    case ImuProbeStage::kPrepareBlinks:
      return "PREPARE_BLINKS";
    case ImuProbeStage::kCaptureBlinks:
      return "BLINK_FIRMLY";
    case ImuProbeStage::kPrepareHeadMotion:
      return "PREPARE_HEAD";
    case ImuProbeStage::kCaptureHeadMotion:
      return "MOVE_HEAD";
    case ImuProbeStage::kCaptureFreeRun:
      return "CAPTURE_FREE_RUN";
    case ImuProbeStage::kResult:
      return "CAPTURE_COMPLETE";
  }
  return "UNKNOWN";
}

bool imuProbeCapturing() {
  return imu_probe_stage == ImuProbeStage::kCaptureStill ||
         imu_probe_stage == ImuProbeStage::kCaptureBlinks ||
         imu_probe_stage == ImuProbeStage::kCaptureHeadMotion ||
         imu_probe_stage == ImuProbeStage::kCaptureFreeRun;
}

bool imuProbeActive() {
  return imu_probe_stage != ImuProbeStage::kIdle &&
         imu_probe_stage != ImuProbeStage::kResult;
}

void advanceImuProbe(ImuProbeStage next, uint32_t now_ms) {
  imu_probe_stage = next;
  imu_probe_stage_started_ms = now_ms;
  if (next == ImuProbeStage::kPrepareStill ||
      next == ImuProbeStage::kPrepareBlinks ||
      next == ImuProbeStage::kPrepareHeadMotion) {
    // Each preparation phase starts with a fresh adaptive baseline. Feeding
    // its full three seconds into the detector enforces the two-second quiet
    // gate before capture while still carrying late preparation movement into
    // the measured phase.
    probe_imu_blink_detector.reset();
  }
  diagnostics.printf("EVENT,IMU_PROBE_STAGE,%s\n", imuProbeStageName(next));
  last_screen_ms = 0;
}

void startImuProbe(uint32_t now_ms) {
  imu_probe_samples = 0;
  imu_still_sequences = 0;
  imu_blink_sequences = 0;
  imu_head_sequences = 0;
  imu_free_sequences = 0;
  // Repeating the test invalidates the previous result until every safety
  // phase passes again. Runtime state must never span a mount change.
  imu_blink_validated = false;
  runtime_imu_blink_detector.reset();
  diagnostics.println(
      "CSV: IMU,ms,usec,stage,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g");
  diagnostics.println("EVENT,IMU_PROBE_STARTED");
  advanceImuProbe(ImuProbeStage::kPrepareStill, now_ms);
}

// Ends a free-run capture block. Free-run never touches the validation gate:
// imu_blink_validated stays exactly as startImuProbe left it (false), and the
// RESULT line always reports NOT_PROVEN with the free-run sequence count as a
// trailing field that older tools tolerate and ignore.
void endFreeRunCapture(uint32_t now_ms) {
  runtime_imu_blink_detector.reset();
  advanceImuProbe(ImuProbeStage::kResult, now_ms);
  diagnostics.printf("EVENT,IMU_PROBE_COMPLETE,samples=%lu\n",
                     static_cast<unsigned long>(imu_probe_samples));
  diagnostics.printf(
      "RESULT,IMU_BLINK,NOT_PROVEN,still=%lu,blink=%lu,head=%lu,free=%lu\n",
      static_cast<unsigned long>(imu_still_sequences),
      static_cast<unsigned long>(imu_blink_sequences),
      static_cast<unsigned long>(imu_head_sequences),
      static_cast<unsigned long>(imu_free_sequences));
}

void updateImuProbeClock(uint32_t now_ms) {
  const uint32_t elapsed = now_ms - imu_probe_stage_started_ms;
  switch (imu_probe_stage) {
    case ImuProbeStage::kPrepareStill:
      if (elapsed >= kImuPrepareMs) {
        advanceImuProbe(ImuProbeStage::kCaptureStill, now_ms);
      }
      break;
    case ImuProbeStage::kCaptureStill:
      if (elapsed >= kImuStillCaptureMs) {
        advanceImuProbe(ImuProbeStage::kPrepareBlinks, now_ms);
      }
      break;
    case ImuProbeStage::kPrepareBlinks:
      if (elapsed >= kImuPrepareMs) {
        advanceImuProbe(ImuProbeStage::kCaptureBlinks, now_ms);
      }
      break;
    case ImuProbeStage::kCaptureBlinks:
      if (elapsed >= kImuBlinkCaptureMs) {
        advanceImuProbe(ImuProbeStage::kPrepareHeadMotion, now_ms);
      }
      break;
    case ImuProbeStage::kPrepareHeadMotion:
      if (elapsed >= kImuPrepareMs) {
        advanceImuProbe(ImuProbeStage::kCaptureHeadMotion, now_ms);
      }
      break;
    case ImuProbeStage::kCaptureHeadMotion:
      if (elapsed >= kImuHeadCaptureMs) {
        imu_blink_validated = imu_still_sequences == 0 &&
                              imu_blink_sequences >= 2 &&
                              imu_head_sequences == 0;
        runtime_imu_blink_detector.reset();
        advanceImuProbe(ImuProbeStage::kResult, now_ms);
        diagnostics.printf("EVENT,IMU_PROBE_COMPLETE,samples=%lu\n",
                           static_cast<unsigned long>(imu_probe_samples));
        diagnostics.printf(
            "RESULT,IMU_BLINK,%s,still=%lu,blink=%lu,head=%lu\n",
            imu_blink_validated ? "PASS" : "NOT_PROVEN",
            static_cast<unsigned long>(imu_still_sequences),
            static_cast<unsigned long>(imu_blink_sequences),
            static_cast<unsigned long>(imu_head_sequences));
      }
      break;
    case ImuProbeStage::kCaptureFreeRun:
      if (elapsed >= kImuFreeRunCaptureMs) {
        endFreeRunCapture(now_ms);
      }
      break;
    default:
      break;
  }
}

void drawHeader() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 4);
  M5.Display.print(modeName(mode));
  M5.Display.drawFastHLine(0, 24, M5.Display.width(), TFT_DARKGREY);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

// Display routines are projections of application state. They never change
// arming, power, calibration, or feasibility decisions.
void drawIrScreen(uint32_t now_ms) {
  drawHeader();
  M5.Display.setCursor(4, 31);
  if (!ir_powered) {
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.println("IR power is OFF");
    M5.Display.println("Disconnect external 5V");
    M5.Display.println("hold BLUE for 2 sec");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println();
    M5.Display.println("tap BLUE: next mode");
    return;
  }

  const auto stage = feasibility.stage();
  M5.Display.printf("RX: %s  symbols:%u\n", latest_ir.valid ? "VALID" : "NONE",
                    latest_ir.symbol_count);
  M5.Display.printf("ratio: %.3f\n", latest_ir.value);
  M5.Display.printf("stage: %s\n", colibrino::feasibilityStageName(stage));

  if (stage == FeasibilityStage::kIdle) {
    M5.Display.println("tap BLUE: guided test");
    M5.Display.println("hold BLUE 2s: IR off");
  } else if (stage == FeasibilityStage::kResult) {
    const auto& result = feasibility.result();
    M5.Display.setTextColor(result.passes ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.printf("%s sigma %.1f\n", result.passes ? "PASS" : "NOT PROVEN",
                      result.separation.separation_sigma);
    M5.Display.printf("blinks %u\n", result.detected_blinks);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println("tap BLUE: repeat test");
  } else {
    M5.Display.printf("elapsed: %.1fs\n",
                      feasibility.stageElapsedMs(now_ms) / 1000.0f);
    M5.Display.println("Follow stage text");
  }
}

void drawMotionScreen(uint32_t now_ms) {
  drawHeader();
  M5.Display.setCursor(4, 31);
  M5.Display.printf("BMI270: %s\n", M5.Imu.isEnabled() ? "OK" : "MISSING");
  M5.Display.printf("stage: %s\n", imuProbeStageName(imu_probe_stage));
  if (imu_probe_stage == ImuProbeStage::kIdle) {
    M5.Display.println("wear glasses; face ahead");
    M5.Display.println("tap BLUE: start capture");
    M5.Display.println("hold BLUE 2s: mouse");
  } else if (imu_probe_stage == ImuProbeStage::kResult) {
    M5.Display.setTextColor(imu_blink_validated ? TFT_GREEN : TFT_RED,
                            TFT_BLACK);
    M5.Display.println(imu_blink_validated ? "IMU BLINK: PASS"
                                           : "IMU BLINK: NOT PROVEN");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.printf("still/blink/head %lu/%lu/%lu\n",
                      static_cast<unsigned long>(imu_still_sequences),
                      static_cast<unsigned long>(imu_blink_sequences),
                      static_cast<unsigned long>(imu_head_sequences));
    M5.Display.println("tap BLUE: repeat");
  } else {
    M5.Display.printf("elapsed: %.1fs\n",
                      (now_ms - imu_probe_stage_started_ms) / 1000.0f);
    if (imu_probe_stage == ImuProbeStage::kCaptureStill) {
      M5.Display.println("stay still; blink normally");
    } else if (imu_probe_stage == ImuProbeStage::kCaptureBlinks) {
      M5.Display.println("blink twice; pause 1 sec");
      M5.Display.println("blink twice; then repeat");
    } else if (imu_probe_stage == ImuProbeStage::kCaptureHeadMotion) {
      M5.Display.println("look left/right/up/down");
    } else if (imu_probe_stage == ImuProbeStage::kCaptureFreeRun) {
      M5.Display.println("FREE RUN capture");
      M5.Display.println("tap BLUE: stop");
    } else if (imu_probe_stage == ImuProbeStage::kPrepareStill) {
      M5.Display.println("get ready for next stage");
      M5.Display.println("tap BLUE: free run");
    } else {
      M5.Display.println("get ready for next stage");
    }
  }
}

void drawMouseScreen(uint32_t now_ms) {
  drawHeader();
  M5.Display.setCursor(4, 31);
  M5.Display.setTextColor(mouse_output.armed() ? TFT_GREEN : TFT_YELLOW,
                          TFT_BLACK);
  M5.Display.printf("OUTPUT: %s\n",
                    mouse_output.armed() ? "ARMED" : "LOCKED");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.printf("motion: %s\n", motion_calibrated ? "READY" : "CALIBRATING");
  M5.Display.printf("HID: %s\n",
                    transportName(mouse_output.selectedTransport()));
  const BleMouseState ble_state = ble_mouse.state();
  if (ble_state == BleMouseState::kPairing ||
      ble_state == BleMouseState::kReconnect) {
    M5.Display.printf("BLE: %s %lus\n",
                      ble_state == BleMouseState::kPairing ? "PAIR" : "RECONNECT",
                      static_cast<unsigned long>(
                          ble_mouse.windowRemainingSeconds(now_ms)));
  } else {
    M5.Display.printf("BLE: %s\n", BleMouseTransport::stateName(ble_state));
  }
  const bool optical_click_ready = feasibility.result().passes && ir_powered;
  M5.Display.printf("blink click: %s\n",
                    imu_blink_validated
                        ? "IMU READY"
                        : (optical_click_ready ? "IR READY" : "DISABLED"));
  if (mouse_output.selectedTransport() != MouseTransport::kNone) {
    M5.Display.println("hold BLUE 2s: arm/lock");
  } else if (ble_state == BleMouseState::kReconnect ||
             ble_mouse.repairRequired()) {
    M5.Display.println("hold BLUE 5s: forget+pair");
  } else if (ble_state != BleMouseState::kPairing &&
             ble_state != BleMouseState::kAuthenticating) {
    M5.Display.println("hold BLUE 2s: BLE window");
  }
  M5.Display.println("tap BLUE when locked: IR");
}

void updateDisplay(uint32_t now_ms) {
  if (now_ms - last_screen_ms < 150) {
    return;
  }
  last_screen_ms = now_ms;
  switch (mode) {
    case AppMode::kIrProbe:
      drawIrScreen(now_ms);
      break;
    case AppMode::kMotionMonitor:
      drawMotionScreen(now_ms);
      break;
    case AppMode::kMouse:
      drawMouseScreen(now_ms);
      break;
  }
}

void cycleMode() {
  // Do not abandon a guided capture midway, and never leave mouse mode while
  // armed. These constraints keep the screen aligned with active side effects.
  if (mode == AppMode::kIrProbe) {
    if (feasibility.stage() != FeasibilityStage::kIdle &&
        feasibility.stage() != FeasibilityStage::kResult) {
      return;
    }
    mode = AppMode::kMotionMonitor;
  } else if (mode == AppMode::kMotionMonitor) {
    if (imu_probe_stage != ImuProbeStage::kIdle &&
        imu_probe_stage != ImuProbeStage::kResult) {
      return;
    }
    imu_probe_stage = ImuProbeStage::kIdle;
    mode = AppMode::kMouse;
    mouse_hold_gate.blockUntilRelease();
  } else if (!mouse_output.armed()) {
    forceMouseLock(MouseLockReason::kModeExit, true);
    ble_mouse.cancelWindow();
    mode = AppMode::kIrProbe;
  }
  last_screen_ms = 0;
}

void enableIrProbe() {
  // StickS3 IR is powered by the controlled EXT_5V rail. Never enable this
  // while another supply is driving the external 5 V connector.
  M5.Power.setExtOutput(true);
  delay(20);
  ir_powered = ir_probe.begin();
  if (!ir_powered) {
    M5.Power.setExtOutput(false);
  }
  diagnostics.printf("EVENT,IR_POWER,%s\n",
                     ir_powered ? "ON" : "INIT_FAILED");
}

void disableIrProbe() {
  // A power-off invalidates all current-boot optical evidence. Reusing the old
  // thresholds after geometry or power changed could enable false clicks.
  ir_powered = false;
  feasibility.cancel();
  runtime_blink_detector = {};
  latest_ir = {};
  M5.Power.setExtOutput(false);
  diagnostics.println("EVENT,IR_POWER,OFF");
}

const char* otaStateName() {
#if COLIBRINO_OTA_CONFIGURED
  if (ota_in_progress) {
    return "UPDATING";
  }
  if (ota_service_started && WiFi.status() == WL_CONNECTED) {
    return "READY";
  }
  return ota_network_started ? "CONNECTING" : "STARTING";
#else
  return "DISABLED";
#endif
}

#if COLIBRINO_OTA_CONFIGURED
void drawOtaState(const char* title, const char* detail, uint16_t color) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(4, 8);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.println(title);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println();
  M5.Display.println(detail);
}

void lockForOta() {
  // ArduinoOTA.handle() may own the loop for the entire transfer. Remove every
  // hardware and host side effect before entering that blocking interval.
  forceMouseLock(MouseLockReason::kOta, true);
  finishCoexWindow(millis(), "OTA");
  ble_mouse.stopForOta();
  imu_blink_validated = false;
  imu_probe_stage = ImuProbeStage::kIdle;
  probe_imu_blink_detector.reset();
  runtime_imu_blink_detector.reset();
  if (ir_powered) {
    disableIrProbe();
  } else {
    M5.Power.setExtOutput(false);
  }
  mode = AppMode::kMotionMonitor;
  mouse_output.setSafety(false, motion_calibrated, false);
}

void beginOtaService() {
  ArduinoOTA.setHostname(kOtaHostname);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([]() {
    ota_in_progress = true;
    ota_last_percent = 255;
    lockForOta();
    diagnostics.println("EVENT,OTA,START");
    drawOtaState("OTA UPDATE", "Mouse locked; do not power off", TFT_YELLOW);
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    const uint8_t percent = total == 0 ? 0 : (done * 100U) / total;
    if (percent == ota_last_percent) {
      return;
    }
    ota_last_percent = percent;
    M5.Display.fillRect(4, 60, M5.Display.width() - 8, 12, TFT_DARKGREY);
    const int width =
        static_cast<int>((M5.Display.width() - 8) * percent / 100U);
    M5.Display.fillRect(4, 60, width, 12, TFT_GREEN);
    diagnostics.printf("EVENT,OTA,PROGRESS,%u\n", percent);
  });
  ArduinoOTA.onEnd([]() {
    diagnostics.println("EVENT,OTA,COMPLETE");
    drawOtaState("OTA COMPLETE", "Restarting", TFT_GREEN);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    ota_in_progress = false;
    diagnostics.printf("EVENT,OTA,ERROR,%u\n", static_cast<unsigned>(error));
    drawOtaState("OTA FAILED", "Mouse remains locked", TFT_RED);
    last_screen_ms = millis();
  });
  ArduinoOTA.begin();
  ota_service_started = true;
  diagnostics.printf("EVENT,OTA,READY,%s.local,%s\n", kOtaHostname,
                     WiFi.localIP().toString().c_str());
}

void startOtaNetworking(uint32_t now_ms) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kOtaHostname);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  ota_network_started = true;
  ota_network_started_ms = now_ms;
  diagnostics.printf("EVENT,OTA,WIFI_CONNECTING,%s\n", kOtaHostname);
}

void updateOta(uint32_t now_ms) {
  if (!ota_network_started) {
    startOtaNetworking(now_ms);
  }
  if (WiFi.status() == WL_CONNECTED && !ota_service_started) {
    beginOtaService();
  }
  if (ota_service_started) {
    ArduinoOTA.handle();
  } else if (now_ms - ota_network_started_ms >= 30000) {
    // Remain non-blocking: sensor calibration and the locked UI still work
    // when the access point is absent. Retry without persisting credentials.
    WiFi.disconnect(false, false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    ota_network_started_ms = now_ms;
    diagnostics.println("EVENT,OTA,WIFI_RETRY");
  }
}
#else
void startOtaNetworking(uint32_t) {}
void updateOta(uint32_t) {}
#endif

void updateIr(uint32_t now_ms) {
  if (!ir_powered) {
    return;
  }
  BlinkSignalSample sample;
  if (!ir_probe.poll(now_ms, sample)) {
    return;
  }
  latest_ir = sample;
  // Advance the clock before observing so a sample at a stage boundary is
  // attributed to the newly active stage rather than the expired one.
  feasibility.tick(now_ms);
  feasibility.observe(now_ms, sample.valid, sample.value);

  if (now_ms - last_ir_log_ms >= 100) {
    last_ir_log_ms = now_ms;
    diagnostics.printf(
        "IR,%lu,%u,%u,%lu,%lu,%.5f,%s\n",
        static_cast<unsigned long>(sample.timestamp_ms), sample.valid,
        sample.symbol_count, static_cast<unsigned long>(sample.active_us),
        static_cast<unsigned long>(sample.total_us), sample.value,
        colibrino::feasibilityStageName(feasibility.stage()));
  }

  if (feasibility.stage() == FeasibilityStage::kResult &&
      !feasibility_result_logged) {
    feasibility_result_logged = true;
    const auto& result = feasibility.result();
    diagnostics.printf(
        "RESULT,%s,open=%.5f,closed=%.5f,sigma=%.2f,change=%.2f,open_frames=%lu,closed_frames=%lu,blink_frames=%lu,blinks=%u\n",
        result.passes ? "PASS" : "NOT_PROVEN",
        result.separation.open_mean, result.separation.closed_mean,
        result.separation.separation_sigma,
        result.separation.relative_change,
        static_cast<unsigned long>(result.open_frames),
        static_cast<unsigned long>(result.closed_frames),
        static_cast<unsigned long>(result.blink_frames),
        result.detected_blinks);
    if (result.passes) {
      // Copy the calibrated polarity and thresholds, then clear only temporal
      // state so guided blinks cannot leak into runtime click detection.
      runtime_blink_detector = feasibility.makeDetector();
      runtime_blink_detector.reset();
    }
  }

  if (!imu_blink_validated && mode == AppMode::kMouse &&
      mouse_output.armed() &&
      sample.valid &&
      runtime_blink_detector.configured() &&
      runtime_blink_detector.update(now_ms, sample.value)) {
    if (mouse_output.click(MOUSE_LEFT)) {
      diagnostics.println("EVENT,BLINK_CLICK");
    }
    settleMouseOutputLock();
  }
}

void updateImu(uint32_t now_ms) {
  const auto mask = M5.Imu.update();
  if (!(mask & m5::IMU_Class::sensor_mask_gyro)) {
    return;
  }
  const auto data = M5.Imu.getImuData();
  gyro_dps = {data.gyro.x, data.gyro.y, data.gyro.z};
  accel_g = {data.accel.x, data.accel.y, data.accel.z};
  last_imu_sample_ms = now_ms;

  if (coex_window_active) {
    ++coex_samples;
    if (coex_last_imu_us != 0) {
      const uint32_t gap_us = data.usec - coex_last_imu_us;
      if (gap_us > coex_max_gap_us) {
        coex_max_gap_us = gap_us;
      }
    }
    coex_last_imu_us = data.usec;
    diagnostics.printf(
        "COEX_IMU,%lu,%lu,%.4f,%.4f,%.4f,%lu,%lu,%lu\n",
        static_cast<unsigned long>(now_ms),
        static_cast<unsigned long>(data.usec), gyro_dps.x, gyro_dps.y,
        gyro_dps.z, static_cast<unsigned long>(coex_samples),
        static_cast<unsigned long>(ble_mouse.successfulReports() -
                                   coex_reports_started),
        static_cast<unsigned long>(ble_mouse.reportFailures() -
                                   coex_failures_started));
  }

  updateImuProbeClock(now_ms);
  if (mode == AppMode::kMotionMonitor && imuProbeActive()) {
    const bool sequence_detected =
        probe_imu_blink_detector.update(now_ms, gyro_dps);
    if (imuProbeCapturing()) {
      ++imu_probe_samples;
      if (sequence_detected) {
        uint32_t* stage_sequences = nullptr;
        if (imu_probe_stage == ImuProbeStage::kCaptureStill) {
          stage_sequences = &imu_still_sequences;
        } else if (imu_probe_stage == ImuProbeStage::kCaptureBlinks) {
          stage_sequences = &imu_blink_sequences;
        } else if (imu_probe_stage == ImuProbeStage::kCaptureHeadMotion) {
          stage_sequences = &imu_head_sequences;
        } else if (imu_probe_stage == ImuProbeStage::kCaptureFreeRun) {
          stage_sequences = &imu_free_sequences;
        }
        if (stage_sequences != nullptr) {
          ++*stage_sequences;
          diagnostics.printf("EVENT,IMU_BLINK_SEQUENCE_CANDIDATE,%s,%lu\n",
                             imuProbeStageName(imu_probe_stage),
                             static_cast<unsigned long>(*stage_sequences));
        }
      }
      diagnostics.printf(
          "IMU,%lu,%lu,%s,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f\n",
          static_cast<unsigned long>(now_ms),
          static_cast<unsigned long>(data.usec),
          imuProbeStageName(imu_probe_stage), gyro_dps.x, gyro_dps.y,
          gyro_dps.z, accel_g.x, accel_g.y, accel_g.z);
    }
  }

  if (!motion_calibrated) {
    // Calibration is deliberately per boot. Motion restarts the entire window
    // instead of accepting a noisy bias that could cause pointer drift.
    gyro_calibrator.add(gyro_dps);
    if (now_ms - calibration_started_ms >= 2500) {
      if (gyro_calibrator.finish(gyro_bias)) {
        motion.setBias(gyro_bias);
        motion_calibrated = true;
        diagnostics.printf("EVENT,IMU_CALIBRATED,%.4f,%.4f,%.4f\n",
                           gyro_bias.x, gyro_bias.y, gyro_bias.z);
      } else {
        gyro_calibrator.reset();
        calibration_started_ms = now_ms;
        diagnostics.println(
            "EVENT,IMU_CALIBRATION_RESTARTED,MOTION_DETECTED");
      }
    }
    last_imu_timestamp_us = data.usec;
    return;
  }

  if (last_imu_timestamp_us == 0) {
    last_imu_timestamp_us = data.usec;
    return;
  }
  const uint32_t dt_us = data.usec - last_imu_timestamp_us;
  last_imu_timestamp_us = data.usec;
  if (dt_us == 0 || dt_us > 200000) {
    forceMouseLock(MouseLockReason::kInvalidTiming);
    return;
  }
  const float dt = static_cast<float>(dt_us) / 1000000.0f;
  const PointerDelta delta = motion.update(gyro_dps, dt);
  if (mode == AppMode::kMouse && mouse_output.armed()) {
    mouse_output.move(delta.x, delta.y, now_ms);
    settleMouseOutputLock();
  }
  if (imu_blink_validated && mode == AppMode::kMouse &&
      mouse_output.armed() &&
      runtime_imu_blink_detector.update(now_ms, gyro_dps)) {
    if (mouse_output.click(MOUSE_LEFT)) {
      diagnostics.println("EVENT,IMU_BLINK_SEQUENCE_CLICK");
    }
    settleMouseOutputLock();
  }
}

void logStatus(uint32_t now_ms) {
  // Boot messages can precede host CDC attachment. A low-rate status record
  // makes current safety and sensor state observable after any reconnect.
  if (now_ms - last_status_log_ms < 1000) {
    return;
  }
  last_status_log_ms = now_ms;
  const long battery_level = static_cast<long>(M5.Power.getBatteryLevel());
  if (battery_level >= 0 && battery_level <= 100) {
    ble_mouse.setBatteryLevel(static_cast<uint8_t>(battery_level));
  }
  diagnostics.printf(
      "STATUS,%lu,board=%s,mode=%s,armed=%u,imu=%u,calibrated=%u,imu_blink=%u,ir=%u,ota=%s,ir_stage=%s,imu_stage=%s,gyro_x=%.3f,gyro_y=%.3f,gyro_z=%.3f,build=%s,batt=%ld,tele=%u,ble=%s,hid=%s,bonded=%u,repair=%u,ble_reports=%lu,ble_failures=%lu,heap=%lu,heap_largest=%lu\n",
      static_cast<unsigned long>(now_ms),
      M5.getBoard() == m5::board_t::board_M5StickS3 ? "StickS3" : "UNKNOWN",
      modeName(mode), mouse_output.armed(), M5.Imu.isEnabled(), motion_calibrated,
      imu_blink_validated, ir_powered, otaStateName(),
      colibrino::feasibilityStageName(feasibility.stage()),
      imuProbeStageName(imu_probe_stage),
      gyro_dps.x, gyro_dps.y, gyro_dps.z,
      COLIBRINO_BUILD_ID,
      battery_level, diagnostics.clientConnected() ? 1u : 0u,
      BleMouseTransport::stateName(ble_mouse.state()),
      transportName(mouse_output.selectedTransport()),
      ble_mouse.bonded() ? 1u : 0u,
      ble_mouse.repairRequired() ? 1u : 0u,
      static_cast<unsigned long>(ble_mouse.successfulReports()),
      static_cast<unsigned long>(ble_mouse.reportFailures()),
      static_cast<unsigned long>(freeInternalHeap()),
      static_cast<unsigned long>(largestInternalHeapBlock()));
}

void handleButtons(uint32_t now_ms) {
  // The physically verified large blue Button A owns the complete workflow.
  // This avoids relying on an ambiguous side control near reset/power. Long
  // holds control IR power, mode transitions, or mouse arming; taps navigate or
  // start a guided probe. Mouse HID remains locked throughout both probes.
  // M5Unified owns debouncing and click/hold timing, while local latches make
  // the state toggles edge-like.
  const bool blue_clicked = M5.BtnA.wasClicked();
  if (mode == AppMode::kIrProbe) {
    if (M5.BtnA.pressedFor(kBlueHoldMs) && !ir_hold_latched) {
      ir_hold_latched = true;
      if (ir_powered) {
        disableIrProbe();
      } else {
        enableIrProbe();
      }
    }
    if (ir_powered && blue_clicked && !ir_hold_latched) {
      feasibility.start(now_ms);
      runtime_blink_detector = {};
      feasibility_result_logged = false;
      diagnostics.println("EVENT,IR_GUIDED_TEST_STARTED");
    }
    if (!M5.BtnA.isPressed()) {
      ir_hold_latched = false;
    }
    if (!ir_powered && blue_clicked && !ir_hold_latched) {
      cycleMode();
    }
    return;
  }

  if (mode == AppMode::kMotionMonitor) {
    const bool may_leave_probe = imu_probe_stage == ImuProbeStage::kIdle ||
                                 imu_probe_stage == ImuProbeStage::kResult;
    if (may_leave_probe && M5.BtnA.pressedFor(kBlueHoldMs) &&
        !motion_hold_latched) {
      motion_hold_latched = true;
      cycleMode();
    }
    if (!M5.BtnA.isPressed()) {
      motion_hold_latched = false;
    }
    if (blue_clicked && !motion_hold_latched &&
        (imu_probe_stage == ImuProbeStage::kIdle ||
         imu_probe_stage == ImuProbeStage::kResult)) {
      startImuProbe(now_ms);
    } else if (blue_clicked && !motion_hold_latched &&
               imu_probe_stage == ImuProbeStage::kPrepareStill) {
      // Second tap during the preparation screen converts the guided start
      // into a free-run capture. No second STARTED line is emitted: the
      // EVENT,IMU_PROBE_STAGE,CAPTURE_FREE_RUN transition is the marker, so
      // older tools keep seeing exactly one session.
      advanceImuProbe(ImuProbeStage::kCaptureFreeRun, now_ms);
    } else if (blue_clicked && !motion_hold_latched &&
               imu_probe_stage == ImuProbeStage::kCaptureFreeRun) {
      endFreeRunCapture(now_ms);
    }
    return;
  }

  if (mode == AppMode::kMouse) {
    const bool blue_pressed = M5.BtnA.isPressed();
    const bool hold_was_blocked = mouse_hold_gate.blocked();
    mouse_hold_gate.observe(blue_pressed);
    const BleMouseState ble_state = ble_mouse.state();

    // A hold begun while pairing or authenticating must be fully released; a
    // secure transition can never reinterpret it as an arming gesture.
    if (blue_pressed && (ble_state == BleMouseState::kPairing ||
                         ble_state == BleMouseState::kAuthenticating)) {
      mouse_hold_gate.blockUntilRelease();
    } else if (mouse_output.armed() &&
               mouse_hold_gate.trigger(
                   blue_pressed, M5.BtnA.pressedFor(kBlueHoldMs))) {
      forceMouseLock(MouseLockReason::kPhysical);
    } else if (mouse_output.selectedTransport() != MouseTransport::kNone &&
               mouse_hold_gate.trigger(
                   blue_pressed, M5.BtnA.pressedFor(kBlueHoldMs))) {
      armMouseOutput();
    } else if ((ble_state == BleMouseState::kReconnect ||
                ble_mouse.repairRequired()) &&
               mouse_output.selectedTransport() == MouseTransport::kNone &&
               mouse_hold_gate.trigger(
                   blue_pressed, M5.BtnA.pressedFor(kRepairHoldMs))) {
      forceMouseLock(MouseLockReason::kPhysical, true);
      if (!ble_mouse.forgetBondAndPair(now_ms)) {
        forceMouseLock(MouseLockReason::kAdvertisingFailure, true);
      }
    } else if (mouse_output.selectedTransport() == MouseTransport::kNone &&
               (ble_state == BleMouseState::kIdle ||
                ble_state == BleMouseState::kFault ||
                ble_state == BleMouseState::kDisabled) &&
               !ble_mouse.repairRequired() &&
               mouse_hold_gate.trigger(
                   blue_pressed, M5.BtnA.pressedFor(kBlueHoldMs))) {
      forceMouseLock(MouseLockReason::kPhysical, true);
      if (!ble_mouse.startWindow(now_ms)) {
        forceMouseLock(MouseLockReason::kAdvertisingFailure, true);
      }
    }

    if (blue_clicked && !mouse_output.armed() && !hold_was_blocked &&
        !mouse_hold_gate.blocked()) {
      cycleMode();
    }
  }
}

}  // namespace

void setup() {
  // Disable every unused internal subsystem that could interfere with IR or
  // draw power. `output_power=false` keeps EXT_5V in its safe input/off mode.
  auto m5_config = M5.config();
  m5_config.serial_baudrate = 0;
  m5_config.internal_imu = true;
  m5_config.internal_spk = false;
  m5_config.internal_mic = false;
  m5_config.output_power = false;
  M5.begin(m5_config);

  // M5Unified otherwise classifies a 500 ms press as a hold, creating an
  // unresponsive gap before Colibrino's intentional two-second hold action.
  // Matching both thresholds makes every shorter release a reliable tap.
  M5.BtnA.setHoldThresh(kBlueHoldMs);

  // Official StickS3 IR guidance requires the speaker power amplifier off
  // during reception. Internal speaker initialization is disabled above; this
  // explicitly clears the M5PM1 amplifier-enable latch as well.
  if (M5.getBoard() == m5::board_t::board_M5StickS3) {
    M5.Power.M5pm1.setGPIOOutput(m5::M5PM1_Class::gpio3, false);
  }

  M5.Display.setRotation(0);
  M5.Display.setBrightness(80);

  diagnostics.begin(115200);
  usb_mouse.begin();
  // Interface descriptors must be configured before starting the USB device.
  USB.productName("Colibrino StickS3 Prototype");
  USB.manufacturerName("Colibrino");
  USB.onEvent(onUsbEvent);
  USB.begin();

  calibration_started_ms = millis();
  gyro_calibrator.reset();
  diagnostics.println("Colibrino StickS3 prototype");
  diagnostics.printf("EVENT,BUILD,%s\n", COLIBRINO_BUILD_ID);
  logHeap("BEFORE_INIT");
  const long initial_battery = static_cast<long>(M5.Power.getBatteryLevel());
  const uint8_t ble_battery =
      initial_battery >= 0 && initial_battery <= 100
          ? static_cast<uint8_t>(initial_battery)
          : 0;
  if (!ble_mouse.begin(ble_battery)) {
    mouse_output.forceLock(MouseLockReason::kAdvertisingFailure, true);
  }
  updateMouseOutputState(millis());
  diagnostics.println(
      "CSV: IR,ms,valid,symbols,active_us,total_us,ratio,stage");
  diagnostics.printf("IMU type: %d (BMI270 expected: %d)\n",
                     M5.Imu.getType(), m5::imu_bmi270);
  startOtaNetworking(millis());
  drawIrScreen(millis());
}

void loop() {
  // One cooperative pass: sample physical inputs, update safety state, process
  // sensors, then render. The 1 ms yield also services Arduino/USB background
  // work; motion integration uses sensor timestamps rather than this delay.
  M5.update();
  const uint32_t now_ms = millis();
  updateOta(now_ms);
#if COLIBRINO_OTA_CONFIGURED
  if (ota_in_progress) {
    delay(1);
    return;
  }
#endif
  // After the OTA early-return on purpose: ArduinoOTA.handle() runs a whole
  // transfer synchronously, so no telemetry work can or should run mid-flash.
  diagnostics.service(now_ms);
  updateMouseOutputState(now_ms);
  handleButtons(now_ms);
  updateCoexWindow(now_ms);
  updateImu(now_ms);
  updateIr(now_ms);
  settleMouseOutputLock();
  updateCoexWindow(now_ms);
  logStatus(now_ms);
  updateDisplay(now_ms);
  delay(1);
}
