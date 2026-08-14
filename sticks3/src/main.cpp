#include <Arduino.h>
#include <M5Unified.h>
#include <USB.h>
#include <USBHIDMouse.h>

#include "colibrino/config.h"
#include "colibrino/motion_controller.h"
#include "colibrino/signal_analysis.h"
#include "ir_probe.h"

#if ARDUINO_USB_MODE != 0
#error "Colibrino USB HID requires native USB mode (ARDUINO_USB_MODE=0)"
#endif

namespace {

using colibrino::BlinkFeasibilityProtocol;
using colibrino::BlinkSignalSample;
using colibrino::FeasibilityStage;
using colibrino::GyroBiasCalibrator;
using colibrino::MotionConfig;
using colibrino::MotionController;
using colibrino::PointerDelta;
using colibrino::StickS3IrProbe;
using colibrino::Vec3;

enum class AppMode : uint8_t { kIrProbe, kMotionMonitor, kMouse };

USBHIDMouse mouse;
USBCDC diagnostics;
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

AppMode mode = AppMode::kIrProbe;
bool ir_powered = false;
bool ir_hold_latched = false;
bool mouse_armed = false;
bool mouse_hold_latched = false;
bool motion_calibrated = false;
bool feasibility_result_logged = false;
uint32_t calibration_started_ms = 0;
uint32_t last_imu_timestamp_us = 0;
uint32_t last_screen_ms = 0;
uint32_t last_ir_log_ms = 0;
Vec3 gyro_dps{};
Vec3 gyro_bias{};
BlinkSignalSample latest_ir{};
colibrino::BlinkDetector runtime_blink_detector;

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

void drawIrScreen(uint32_t now_ms) {
  drawHeader();
  M5.Display.setCursor(4, 31);
  if (!ir_powered) {
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.println("IR power is OFF");
    M5.Display.println("Disconnect external 5V");
    M5.Display.println("then hold B for 2 sec");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println();
    M5.Display.println("A: next mode");
    return;
  }

  const auto stage = feasibility.stage();
  M5.Display.printf("RX: %s  symbols:%u\n", latest_ir.valid ? "VALID" : "NONE",
                    latest_ir.symbol_count);
  M5.Display.printf("ratio: %.3f\n", latest_ir.value);
  M5.Display.printf("stage: %s\n", colibrino::feasibilityStageName(stage));

  if (stage == FeasibilityStage::kIdle) {
    M5.Display.println("B: start guided test");
    M5.Display.println("hold B 2s: IR off");
  } else if (stage == FeasibilityStage::kResult) {
    const auto& result = feasibility.result();
    M5.Display.setTextColor(result.passes ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.printf("%s sigma %.1f\n", result.passes ? "PASS" : "NOT PROVEN",
                      result.separation.separation_sigma);
    M5.Display.printf("blinks %u\n", result.detected_blinks);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println("B: repeat test");
  } else {
    M5.Display.printf("elapsed: %.1fs\n",
                      feasibility.stageElapsedMs(now_ms) / 1000.0f);
    M5.Display.println("Follow stage text");
  }
}

void drawMotionScreen() {
  drawHeader();
  M5.Display.setCursor(4, 31);
  M5.Display.printf("BMI270: %s\n", M5.Imu.isEnabled() ? "OK" : "MISSING");
  M5.Display.printf("gyro X %7.2f\n", gyro_dps.x);
  M5.Display.printf("gyro Y %7.2f\n", gyro_dps.y);
  M5.Display.printf("gyro Z %7.2f\n", gyro_dps.z);
  M5.Display.printf("bias: %s\n", motion_calibrated ? "READY" : "KEEP STILL");
  M5.Display.println("A: next mode");
}

void drawMouseScreen() {
  drawHeader();
  M5.Display.setCursor(4, 31);
  M5.Display.setTextColor(mouse_armed ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  M5.Display.printf("OUTPUT: %s\n", mouse_armed ? "ARMED" : "LOCKED");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.printf("motion: %s\n", motion_calibrated ? "READY" : "CALIBRATING");
  const bool click_ready = feasibility.result().passes && ir_powered;
  M5.Display.printf("blink click: %s\n", click_ready ? "VALIDATED" : "DISABLED");
  M5.Display.println("hold A 2s: arm/lock");
  M5.Display.println("B: test click");
  M5.Display.println("tap A when locked: IR");
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
      drawMotionScreen();
      break;
    case AppMode::kMouse:
      drawMouseScreen();
      break;
  }
}

void cycleMode() {
  if (mode == AppMode::kIrProbe) {
    if (feasibility.stage() != FeasibilityStage::kIdle &&
        feasibility.stage() != FeasibilityStage::kResult) {
      return;
    }
    mode = AppMode::kMotionMonitor;
  } else if (mode == AppMode::kMotionMonitor) {
    mode = AppMode::kMouse;
  } else if (!mouse_armed) {
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
  ir_powered = false;
  feasibility.cancel();
  runtime_blink_detector = {};
  latest_ir = {};
  M5.Power.setExtOutput(false);
  diagnostics.println("EVENT,IR_POWER,OFF");
}

void updateIr(uint32_t now_ms) {
  if (!ir_powered) {
    return;
  }
  BlinkSignalSample sample;
  if (!ir_probe.poll(now_ms, sample)) {
    return;
  }
  latest_ir = sample;
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
      runtime_blink_detector = feasibility.makeDetector();
      runtime_blink_detector.reset();
    }
  }

  if (mode == AppMode::kMouse && mouse_armed && sample.valid &&
      runtime_blink_detector.configured() &&
      runtime_blink_detector.update(now_ms, sample.value)) {
    mouse.click(MOUSE_LEFT);
    diagnostics.println("EVENT,BLINK_CLICK");
  }
}

void updateImu(uint32_t now_ms) {
  const auto mask = M5.Imu.update();
  if (!(mask & m5::IMU_Class::sensor_mask_gyro)) {
    return;
  }
  const auto data = M5.Imu.getImuData();
  gyro_dps = {data.gyro.x, data.gyro.y, data.gyro.z};

  if (!motion_calibrated) {
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
  const float dt =
      static_cast<float>(data.usec - last_imu_timestamp_us) / 1000000.0f;
  last_imu_timestamp_us = data.usec;
  const PointerDelta delta = motion.update(gyro_dps, dt);
  if (mode == AppMode::kMouse && mouse_armed && (delta.x || delta.y)) {
    mouse.move(delta.x, delta.y);
  }
}

void handleButtons(uint32_t now_ms) {
  if (mode == AppMode::kIrProbe) {
    if (M5.BtnB.pressedFor(2000) && !ir_hold_latched) {
      ir_hold_latched = true;
      if (ir_powered) {
        disableIrProbe();
      } else {
        enableIrProbe();
      }
    }
    if (!M5.BtnB.isPressed()) {
      ir_hold_latched = false;
    }
    if (ir_powered && M5.BtnB.wasClicked()) {
      feasibility.start(now_ms);
      runtime_blink_detector = {};
      feasibility_result_logged = false;
      diagnostics.println("EVENT,IR_GUIDED_TEST_STARTED");
    }
  }

  if (mode == AppMode::kMouse) {
    if (M5.BtnA.pressedFor(2000) && !mouse_hold_latched &&
        motion_calibrated) {
      mouse_hold_latched = true;
      mouse_armed = !mouse_armed;
      motion.reset();
      diagnostics.printf("EVENT,MOUSE_OUTPUT,%s\n",
                         mouse_armed ? "ARMED" : "LOCKED");
    }
    if (!M5.BtnA.isPressed()) {
      mouse_hold_latched = false;
    }
    if (mouse_armed && M5.BtnB.wasClicked()) {
      mouse.click(MOUSE_LEFT);
      diagnostics.println("EVENT,BUTTON_TEST_CLICK");
    }
  }

  if (M5.BtnA.wasClicked() && !mouse_armed) {
    cycleMode();
  }
}

}  // namespace

void setup() {
  auto m5_config = M5.config();
  m5_config.serial_baudrate = 0;
  m5_config.internal_imu = true;
  m5_config.internal_spk = false;
  m5_config.internal_mic = false;
  m5_config.output_power = false;
  M5.begin(m5_config);

  // Official StickS3 IR guidance requires the speaker power amplifier off
  // during reception. Internal speaker initialization is disabled above; this
  // explicitly clears the M5PM1 amplifier-enable latch as well.
  if (M5.getBoard() == m5::board_t::board_M5StickS3) {
    M5.Power.M5pm1.setGPIOOutput(m5::M5PM1_Class::gpio3, false);
  }

  M5.Display.setRotation(0);
  M5.Display.setBrightness(80);

  diagnostics.begin(115200);
  mouse.begin();
  USB.productName("Colibrino StickS3 Prototype");
  USB.manufacturerName("Colibrino");
  USB.begin();

  calibration_started_ms = millis();
  gyro_calibrator.reset();
  diagnostics.println("Colibrino StickS3 prototype");
  diagnostics.println(
      "CSV: IR,ms,valid,symbols,active_us,total_us,ratio,stage");
  diagnostics.printf("IMU type: %d (BMI270 expected: %d)\n",
                     M5.Imu.getType(), m5::imu_bmi270);
  drawIrScreen(millis());
}

void loop() {
  M5.update();
  const uint32_t now_ms = millis();
  handleButtons(now_ms);
  updateImu(now_ms);
  updateIr(now_ms);
  updateDisplay(now_ms);
  delay(1);
}
