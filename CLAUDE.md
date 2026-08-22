# Colibrino — Claude session notes

Read `AGENTS.md` and `PROJECT_KNOWLEDGE.md` first; they are the authority on
workflow, safety gates, and current state. Non-negotiables: any device
upload/flash/OTA needs Felipe's explicit in-the-moment authorization; master's
tip is never RED; `bedside-countdown-s3` is never a target.

## Upstream lineage

- Upstream original: **https://github.com/tix-life/Colibrino** (GPL-3.0,
  created 2020, 51 stars; last code change Jan 2023, README edits to Jan 2025).
  The homepage `colibrino.com.br` redirects to that repo.
- This repo is the fork carrying the FULL upstream history. All 25 upstream
  files are present here byte-identical (legacy AVR firmware under
  `arduino project/Colibrino/`, `LimparCalibracao/`, 3 STL enclosures under
  `3d models/`, `doc/header.jpeg` + `doc/protoboard-diagram.jpg`, LICENSE)
  except `README.md`, which is deliberately extended locally.
- Upstream's sole open issue (#6, 2023, unanswered) asks for Bluetooth
  operation — independent demand validating the current cable-free direction.
- GPL-3.0 applies to derivatives: keep the license, keep sources available.

## Legacy learnings (from dissecting the upstream sources)

- **TCRT5000 blink sensing used synchronous demodulation**: 5-phase 200 µs
  timer pulses the IR LED and reads LED-on minus LED-off — ambient light
  cancels. The single biggest robustness trick in the legacy design
  (`arduino project/Colibrino/blink.cpp`).
- Blink edges = derivative of a windowed moving average with asymmetric
  rise/fall thresholds, dynamic baseline with hysteresis, stuck-blink
  timeouts, and a per-user calibration that learns from up to 4 sample blinks.
- Sensor fit is per-user: only the IMU is fixed to the frame; the TCRT hangs
  on its wire and is hand-adjusted near the eye. Sensor-to-eye distance is a
  fit parameter, not fixed geometry.
- Head→cursor mapping is the DERIVATIVE of Mahony yaw/pitch (sensitivity 30),
  with deadzones (1.0 horiz, 0.75 vert) and ±180° wrap correction so the
  derivative never jumps (`mouseIMU.cpp`).
- Gyro-bias calibration is one-time, EEPROM-persisted, done with the device
  still on a table, with a buzzer signaling completion; `LimparCalibracao` is
  the documented drift-recovery sketch (wipe, re-upload, recalibrate).
- Feedback was buzzer-first — users who cannot look at LEDs still get state.
- Alternate actuation existed: dwell-click (compiled out) and a relay/switch
  output — the blink was always intended as a general accessibility switch,
  not only a mouse click.
- Upstream roadmap that never shipped: lateral-tilt scroll and head-gesture
  commands.

## Blink-click channel (decided 2026-08-21)

Industry consensus, confirmed by worn data: blinks come from a dedicated eye
sensor or camera; the IMU is the head pointer + motion veto, never the blink
signal. The gyro click channel is closed. Active experiment: macOS
"alternative pointer actions" (webcam Eye Blink → click) with the StickS3 as
pointer; on-device future class is VCNL4040/TMD2635 IR-proximity (VL53L4CD
ToF rejected on physics). Details: PROJECT_KNOWLEDGE 2026-08-21 entry.

## BLE HID pointer + boot-loop incident (2026-08-21/22)

- Branch `agent/ble-hid-pointer` (not merged): NimBLE bonded bounded HID
  transport behind the portable fail-closed `MouseOutputPolicy` (USB +
  BLE, wired-preferred). Hardware-validated cable-free 2026-08-22: pairing,
  arming, head-pointer motion ("horizontal very good, vertical weaker" —
  tuning item), disconnect/reconnect, Wi-Fi+BLE coexistence 164.8–165.8 Hz /
  0 drops vs 170.3 Hz baseline. Pending: USB-cable topology switch, 10-min
  soak, Stage 2 (BLE pointer + Mac camera click). Record: PORT_PLAN T16/T17,
  PROJECT_KNOWLEDGE 2026-08-22 entry,
  `docs/handovers/2026-08-21-ble-hid-pointer/` (PLAN-r2, REVIEW-r2, INCIDENT).
- Root cause of the 521bc26 boot loop (flash core dump): `WiFi.setSleep(false)`
  after the BT controller was enabled aborts the Wi-Fi task on IDF 4.4
  (coexistence requires modem sleep — official manual). Never `WIFI_PS_NONE`
  with BLE on this core.
- Why it was unrecoverable and what now prevents it: Arduino 2.0.x confirms
  OTA images before `setup()` unless `extern "C" bool verifyRollbackLater()`
  returns true (C linkage mandatory); `USB.begin()` hands the USB PHY to
  TinyUSB for good (one USB-Serial/JTAG console window per hard reset). The
  firmware now self-confirms after 10 s of healthy loop, self-reports
  `EVENT,BOOT,…` / `EVENT,LAST_CRASH,…` before `USB.begin()` and on every
  attach, rolls back on a 45 s deadline or 3 crash-class boots, and every link
  runs `scripts/check_image.py`. Do not disable any of it.
- StickS3 side button: long press = download mode, single click = reset,
  double click = power off; esptool leaves the chip in download mode until a
  click. UART0 tap = HAT2-Bus pin 10 G43 / 12 G44 / 1 GND. Device at wrap:
  `ff1f7d2` on app1 (VALID), `254fb7e` on app0.
