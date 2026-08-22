#if defined(COLIBRINO_WOKWI)

// Deterministic ESP32-S3 smoke firmware for Wokwi.
//
// This intentionally excludes M5Unified, board power, display, IR hardware, and
// USB HID. It compiles the real portable production sources under the pinned
// ESP32-S3 Arduino toolchain and emits machine-readable pass/fail markers.
#include <Arduino.h>

#include <cmath>

#include "colibrino/boot_health.h"
#include "colibrino/imu_blink_detector.h"
#include "colibrino/mouse_output_policy.h"
#include "colibrino/motion_controller.h"
#include "colibrino/signal_analysis.h"

namespace {

bool all_passed = true;

// One failing check is sticky so the final marker cannot be accidentally
// restored to PASS by a later successful check.
void check(const char* name, bool passed) {
  Serial.printf("CHECK,%s,%s\n", name, passed ? "PASS" : "FAIL");
  all_passed = all_passed && passed;
}

void validateGyroCalibration() {
  colibrino::GyroBiasCalibrator stable;
  for (size_t index = 0; index < 100; ++index) {
    const float noise = index % 2 == 0 ? -0.05f : 0.05f;
    stable.add({1.25f + noise, -0.75f - noise, 0.40f + noise});
  }

  colibrino::Vec3 bias;
  const bool accepted = stable.finish(bias, 0.20f);
  check("gyro_stationary_calibration",
        accepted && std::fabs(bias.x - 1.25f) < 0.01f &&
            std::fabs(bias.y + 0.75f) < 0.01f &&
            std::fabs(bias.z - 0.40f) < 0.01f);

  colibrino::GyroBiasCalibrator moving;
  for (size_t index = 0; index < 100; ++index) {
    const float swing = index % 2 == 0 ? -8.0f : 8.0f;
    moving.add({swing, 0.0f, 0.0f});
  }
  check("gyro_motion_rejected", !moving.finish(bias, 2.0f));
}

void validatePointerMotion() {
  colibrino::MotionController controller;
  controller.setBias({1.0f, -2.0f, 0.5f});

  bool stayed_still = true;
  for (size_t index = 0; index < 50; ++index) {
    const colibrino::PointerDelta delta =
        controller.update({1.2f, -1.4f, 0.5f}, 0.01f);
    stayed_still = stayed_still && delta.x == 0 && delta.y == 0;
  }
  check("pointer_deadzone", stayed_still);

  int horizontal = 0;
  int vertical = 0;
  for (size_t index = 0; index < 50; ++index) {
    const colibrino::PointerDelta delta =
        controller.update({13.0f, 18.0f, 0.5f}, 0.01f);
    horizontal += delta.x;
    vertical += delta.y;
  }
  check("pointer_direction_and_accumulation",
        horizontal < -100 && vertical > 50);

  const colibrino::PointerDelta invalid_dt =
      controller.update({100.0f, 100.0f, 100.0f}, 0.5f);
  check("pointer_invalid_timestep_rejected",
        invalid_dt.x == 0 && invalid_dt.y == 0);
}

void validateMouseOutputPolicy() {
  using colibrino::MouseLockReason;
  using colibrino::MouseOutputPolicy;
  using colibrino::MouseTransport;

  MouseOutputPolicy policy;
  check("hid_policy_boot_locked",
        !policy.armed() && !policy.canReport() &&
            policy.selectedTransport() == MouseTransport::kNone);

  policy.setMouseMode(true);
  policy.setCalibrated(true);
  policy.setImuFresh(true);
  policy.setTransportReadiness(true, true);
  MouseLockReason ignored;
  policy.takeReleaseRequest(ignored);
  const bool armed = policy.requestArm();
  const auto usb_report = policy.routeMotion(7, -4, 100);
  check("hid_policy_wired_preferred",
        armed && usb_report.ready &&
            usb_report.transport == MouseTransport::kUsb &&
            usb_report.x == 7 && usb_report.y == -4);

  policy.setTransportReadiness(false, true);
  const bool locked_on_switch = !policy.armed();
  policy.takeReleaseRequest(ignored);
  const bool rearmed = policy.requestArm();
  const bool pending = !policy.routeMotion(20, 10, UINT32_MAX - 3).ready;
  const auto ble_report = policy.routeMotion(20, 10, 4);
  check("hid_policy_ble_bounded_rollover",
        locked_on_switch && rearmed && pending && ble_report.ready &&
            ble_report.transport == MouseTransport::kBle &&
            ble_report.x == 40 && ble_report.y == 20);

  policy.setTransportReadiness(false, false);
  policy.setTransportReadiness(false, true);
  check("hid_policy_reconnect_no_stale_output",
        !policy.armed() && !policy.canReport() &&
            !policy.routeMotion(50, 50, 20).ready);
}

void validateBootHealthPolicy() {
  using colibrino::BootHealthAction;
  using colibrino::BootHealthPolicy;
  using colibrino::BootResetClass;

  BootHealthPolicy policy;
  const uint32_t cold = policy.begin(BootResetClass::kColdOrExternal, 9);
  check("boot_health_cold_reset_restarts_budget",
        cold == 1 && !policy.shouldRollbackAtBoot() &&
            policy.shouldRollbackAtDeadline());

  policy.begin(BootResetClass::kWarm, 3);
  const bool budget_exhausted = policy.shouldRollbackAtBoot();
  policy.begin(BootResetClass::kWarm, 2);
  check("boot_health_rollback_after_budget",
        budget_exhausted && !policy.shouldRollbackAtBoot());

  policy.noteSetupComplete(UINT32_MAX - 500);
  const bool early = policy.update(9498) == BootHealthAction::kNone;
  const bool confirmed = policy.update(9499) == BootHealthAction::kConfirmImage;
  const bool once = policy.update(20000) == BootHealthAction::kNone;
  check("boot_health_confirms_once_rollover",
        early && confirmed && once && !policy.shouldRollbackAtDeadline());
}

colibrino::SeparationResult makeSeparation() {
  colibrino::RunningStats open;
  colibrino::RunningStats closed;
  for (size_t index = 0; index < 50; ++index) {
    const float noise = index % 2 == 0 ? -0.01f : 0.01f;
    open.add(0.20f + noise);
    closed.add(0.80f - noise);
  }
  return colibrino::analyzeSeparation(open, closed);
}

void validateBlinkDetector() {
  const colibrino::SeparationResult separation = makeSeparation();
  check("blink_signal_separation",
        separation.passes_signal_gate && separation.closed_is_higher);

  colibrino::BlinkDetector detector;
  detector.configure(separation);
  detector.update(1000, 0.20f);
  detector.update(1100, 0.80f);
  const bool first_blink = detector.update(1200, 0.20f);
  detector.update(1250, 0.80f);
  const bool refractory_rejection = !detector.update(1300, 0.20f);
  detector.update(1500, 0.80f);
  const bool second_blink = detector.update(1580, 0.20f);
  check("blink_duration_and_refractory",
        first_blink && refractory_rejection && second_blink);
}

bool feedImuStill(colibrino::ImuBlinkDetector& detector, uint32_t& now_ms,
                  uint32_t duration_ms) {
  bool emitted = false;
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += 5) {
    const float noise = elapsed % 20 == 0 ? 0.12f : -0.06f;
    emitted = detector.update(now_ms, {noise, -noise, 0.0f}) || emitted;
    now_ms += 5;
  }
  return emitted;
}

bool feedImuBlinkImpulse(colibrino::ImuBlinkDetector& detector,
                         uint32_t& now_ms) {
  bool emitted = false;
  for (size_t index = 0; index < 12; ++index) {
    const float value = index < 4 ? -1.35f : (index < 8 ? 0.95f : 0.10f);
    emitted =
        detector.update(now_ms, {value, 0.2f * value, -0.1f * value}) ||
        emitted;
    now_ms += 5;
  }
  return emitted;
}

void validateImuBlinkSequence() {
  colibrino::ImuBlinkDetector detector;
  uint32_t now_ms = 0;
  bool emitted = feedImuStill(detector, now_ms, 2100);
  emitted = feedImuBlinkImpulse(detector, now_ms) || emitted;
  emitted = feedImuStill(detector, now_ms, 400) || emitted;
  emitted = feedImuBlinkImpulse(detector, now_ms) || emitted;
  emitted = feedImuStill(detector, now_ms, 900) || emitted;
  emitted = feedImuBlinkImpulse(detector, now_ms) || emitted;
  emitted = feedImuStill(detector, now_ms, 400) || emitted;
  emitted = feedImuBlinkImpulse(detector, now_ms) || emitted;
  check("imu_double_pause_double_pattern",
        emitted && detector.completedSequences() == 1);

  detector.reset();
  now_ms = 0;
  emitted = feedImuStill(detector, now_ms, 2100);
  for (size_t impulse = 0; impulse < 4; ++impulse) {
    emitted = feedImuBlinkImpulse(detector, now_ms) || emitted;
    if (impulse != 3) {
      emitted = feedImuStill(detector, now_ms, 400) || emitted;
    }
  }
  check("imu_uniform_blinks_rejected",
        !emitted && detector.completedSequences() == 0);

  detector.reset();
  now_ms = 0;
  emitted = feedImuStill(detector, now_ms, 2100);
  emitted = feedImuBlinkImpulse(detector, now_ms) || emitted;
  emitted = feedImuStill(detector, now_ms, 400) || emitted;
  emitted = feedImuBlinkImpulse(detector, now_ms) || emitted;
  emitted = feedImuStill(detector, now_ms, 900) || emitted;
  // Pointer-scale motion cancels all partial state and restarts the two-second
  // quiet gate. A following pulse must not complete the old pattern.
  emitted = detector.update(now_ms, {0.0f, 8.0f, 0.0f}) || emitted;
  now_ms += 5;
  emitted = feedImuStill(detector, now_ms, 2100) || emitted;
  emitted = feedImuBlinkImpulse(detector, now_ms) || emitted;
  check("imu_short_and_motion_patterns_rejected",
        !emitted && detector.completedSequences() == 0);
}

void validateFeasibilityProtocol() {
  colibrino::FeasibilityConfig config;
  config.preparation_ms = 10;
  config.open_capture_ms = 20;
  config.closed_capture_ms = 20;
  config.blink_capture_ms = 1000;
  config.minimum_blinks = 2;

  colibrino::BlinkFeasibilityProtocol protocol(config);
  protocol.start(0);
  protocol.tick(10);
  for (size_t index = 0; index < 25; ++index) {
    protocol.observe(11 + index % 10, true, 0.20f + (index % 2) * 0.01f);
  }
  protocol.tick(30);
  protocol.tick(40);
  for (size_t index = 0; index < 25; ++index) {
    protocol.observe(41 + index % 10, true, 0.80f - (index % 2) * 0.01f);
  }
  protocol.tick(60);
  protocol.tick(70);

  protocol.observe(71, true, 0.20f);
  protocol.observe(100, true, 0.80f);
  protocol.observe(170, true, 0.20f);
  protocol.observe(400, true, 0.80f);
  protocol.observe(470, true, 0.20f);
  protocol.tick(1070);

  const colibrino::FeasibilityResult& result = protocol.result();
  check("guided_blink_protocol",
        protocol.stage() == colibrino::FeasibilityStage::kResult &&
            result.passes && result.detected_blinks == 2 &&
            result.open_frames == 25 && result.closed_frames == 25);
}

}  // namespace

void setup() {
  // UART0 is connected to Wokwi's serial monitor in diagram.json. The startup
  // delay lets the virtual monitor attach before the first marker is emitted.
  Serial.begin(115200);
  delay(1000);
  Serial.println("COLIBRINO_SIM_START");

  validateGyroCalibration();
  validatePointerMotion();
  validateMouseOutputPolicy();
  validateBootHealthPolicy();
  validateBlinkDetector();
  validateImuBlinkSequence();
  validateFeasibilityProtocol();

  Serial.println(all_passed ? "COLIBRINO_SIM_PASS" : "COLIBRINO_SIM_FAIL");
}

void loop() { delay(1000); }

#endif
