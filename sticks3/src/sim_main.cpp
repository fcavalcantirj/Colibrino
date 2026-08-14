#if defined(COLIBRINO_WOKWI)

#include <Arduino.h>

#include <cmath>

#include "colibrino/motion_controller.h"
#include "colibrino/signal_analysis.h"

namespace {

bool all_passed = true;

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
  Serial.begin(115200);
  delay(1000);
  Serial.println("COLIBRINO_SIM_START");

  validateGyroCalibration();
  validatePointerMotion();
  validateBlinkDetector();
  validateFeasibilityProtocol();

  Serial.println(all_passed ? "COLIBRINO_SIM_PASS" : "COLIBRINO_SIM_FAIL");
}

void loop() { delay(1000); }

#endif
