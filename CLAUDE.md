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
