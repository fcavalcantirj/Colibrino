---
slug: ble-hid-pointer
date: 2026-08-21
status: approved
round: 1
author_session: Architect session (2026-08-17..21 cycle): validated wireless telemetry + fifth OTA, closed the IMU blink-click channel on evidence, validated Mac camera click live; now commissioning the cable-free pointer
---

# Handover — BLE HID pointer: the cable-free mouse build

## Mission

Give the StickS3 a Bluetooth LE HID mouse transport so the head pointer works with NO cable, completing the new architecture: StickS3 = wireless head pointer, Mac camera = blink click (validated live 2026-08-21). The USB HID path stays intact. Fail-closed semantics are non-negotiable and identical across transports. Delivery is via OTA on Felipe's explicit go (standing rule: OTA always, unless impossible). You are the BUILDER: your deliverable is PLAN-r1.md — no code until the Architect review returns APPROVED.

## Where things stand (verified by the Architect session)

- [REAL] Colibrino master = `a3c98de` == origin, clean (2026-08-21). Device firmware = `254fb7e` (fifth OTA, TCP-verified; no firmware source changes since). Read `docs/handovers/2026-08-21-blink-architecture-pivot/HANDOVER.md` for the full state; its constraints are incorporated here by reference.
- [REAL] Firmware platform: Arduino-ESP32 2.0.17 (pinned), M5Unified 0.2.19, PlatformIO env `m5stack-sticks3`, ESP32-S3-PICO-1-N8R8 (8 MB flash, PSRAM; BLE 5 capable). App partition 3,342,336 B; current build RAM 98,244 B (30.0%), Flash 1,063,597 B (31.8%) — headroom exists but BLE stack cost must be measured and reported.
- [REAL] Pointer path today (`sticks3/src/main.cpp`): `USBHIDMouse mouse` (line ~69, registered before `USB.begin()` at ~854-856); modes IR PROBE → MOTION → MOUSE via Button A (`cycleMode`); in MOUSE mode a 2 s hold toggles `mouse_armed` (requires `motion_calibrated`); motion output only while armed; blink click is hard-disabled (`imu_blink_validated` false — the IMU click channel is CLOSED on physics + industry evidence; do not touch it).
- [REAL] Loop: one cooperative pass — `M5.update() → updateOta → diagnostics.service (TelemetryMux) → handleButtons → updateImu → updateIr → logStatus → updateDisplay → delay(1)`. `ArduinoOTA.handle()` flashes synchronously; telemetry idles during update.
- [REAL] Wireless telemetry (TCP 35533, output-only) is hardware-validated incl. throughput equivalence (~170 Hz, 0 drops). It must keep working with BLE active.
- [REAL] USB data path on telemetry-era builds is UNVERIFIED on hardware (never plugged with a data cable since e1b1c48); the USB init code is intact. Not your problem to fix, but note it: your bench validation will incidentally verify or refute it if you test USB alongside BLE.
- [TEST] Native suite 13/13 (`platformio test -e native`); host tooling tests 29/29; Wokwi gate 11 checks (last full pass 2026-08-17; portable logic untouched since).
- [UNVERIFIED] BLE library choice, Wi-Fi/BLE coexistence impact on telemetry rate, RAM/Flash delta — your research subjects.

Verbatim commands (from `sticks3/`):

```sh
/Users/fcavalcanti/.platformio/penv/bin/platformio run -e m5stack-sticks3
/Users/fcavalcanti/.platformio/penv/bin/platformio test -e native
/Users/fcavalcanti/miniconda3/bin/python3 -m unittest tools/tests/test_capture_tools.py
./scripts/upload_ota.sh
```

## Blocking constraints (restate ALL in your plan before anything else)

1. NEVER upload/flash without Felipe explicitly authorizing THAT upload in the moment; never target `bedside-countdown-s3`; delivery via OTA always unless impossible.
2. Fail-closed is identity: NO pointer output on any transport unless `mouse_armed`; arming requires the deliberate 2 s hold in MOUSE mode; boot state locked; any fault (transport disconnect, IMU fault, mode exit) = release-all + disarm. The BLE transport must not weaken this by one bit — including while pairing.
3. BLE security: bonding + encryption required (Just Works acceptable for a mouse, but bonded — no open re-pairable-by-anyone state); advertising only in a deliberate, bounded pairing window (never always-advertising), and the pairing-window UX proposal goes to Felipe via the Architect.
4. The telemetry mux contract stands: output-only socket; no `WiFiClient::write`/`connected()` in its path; accept gate is `fd() >= 0`. Do not touch the mux except with an explicit re-review.
5. Master never RED: build on a branch; master moves only by FF of a green integration state (native 13/13, tooling 29/29, production build with AND without `colibrino_secrets.h`, Wokwi gate green if portable logic changes).
6. Oracle Loop untouched (merge-frozen without Felipe's exact phrase). v2 core branch untouched by this feature.
7. USB HID path stays functional and unchanged in behavior; BLE is additive.
8. Report truthfully with [REAL]/[TEST]/[UNVERIFIED]; hardware claims only from hardware evidence.

## Accepted residuals / Refuted — don't fix

- The IMU blink detector stays in firmware for data collection; it actuates nothing. Closed channel — do not revive, do not tune.
- Capture tool is console-only by design (`--voice` opt-in); beeps are load-bearing. Don't touch capture tooling in this build.
- The device rides the LEFT temple; `round1.json` mount labels are wrong — separate open item, not yours.
- Mirrored sunglasses block the camera click — known, recorded, not yours.
- Battery drains ~0.7%/min with Wi-Fi + display; BLE adds more. Measure and report, don't optimize prematurely.

## Hard rules & human-reserved decisions

- Moment-of-upload OTA go IS FELIPE'S CALL. Pairing-window UX (how advertising starts) IS FELIPE'S CALL on the Architect's recommendation. Whether USB and BLE mice run simultaneously or BLE yields to USB-present IS FELIPE'S CALL after your analysis.
- The Architect (original session) is the review authority: PLAN-r1 → REVIEW-r1 ping-pong until APPROVED. Do not start coding before that.

## Acceptance checklist (the Architect approves ONLY against these)

1. Plan restates all 8 blocking constraints correctly, in the Builder's own words.
2. Library/stack decision is researched and justified (candidates: NimBLE-Arduino HID, Arduino-ESP32 built-in BLE HID, T-vK ESP32-BLE-Mouse class libraries; judge by: maintenance, NimBLE vs Bluedroid RAM cost, coexistence behavior on 2.0.17, bonding support), with a stated fallback if the first choice fails on hardware.
3. Architecture section covers: where BLE init + HID report sending hooks into the existing loop/modes WITHOUT touching arming semantics (single `mouse_armed` gate feeding both transports through one choke point — e.g., a MouseTransport abstraction over USBHIDMouse + BLE HID); release-all on disconnect/disarm/mode-exit for BOTH transports; STATUS line gains `ble=` state field; display shows BLE state in MOUSE mode.
4. Pairing design: bounded advertising window with deliberate entry, bonded+encrypted, re-pair flow, and what the screen shows — presented as a proposal for Felipe with a recommended default.
5. Coexistence test plan: telemetry TCP capture running WHILE BLE mouse is active and armed on the bench; rate/DROPPED compared against the recorded baseline (~170 Hz, 0 drops); explicit criteria.
6. Wi-Fi + BLE + display RAM/Flash budget: predicted delta, measured after first build, both reported; abort criteria if partition or RAM pressure appears.
7. Test plan: native suite untouched-and-green; what new portable logic (if any) gets native tests; bench validation script (pair → move only-when-armed → disarm kills output → disconnect auto-release → reconnect behavior → USB coexistence check); the full wireless Stage-2 experience (BLE pointer + Mac camera click) as the final acceptance, run by Felipe.
8. Rollout: branch name; commit cadence; OTA delivery steps with the runbook order (build + note id → archive payload → announce → WAIT for go → upload → post-reboot TCP verify incl. new `ble=` field → report); explicit failure branch (stop, report, no retry without Felipe).
9. Docs: PORT_PLAN.json new task entry, PROJECT_KNOWLEDGE changelog, README status table row — updated only after hardware validation actually passes.
10. Plan touches nothing in Oracle Loop, home-automations, v2/, or the capture tooling.

## next_action

Step 0 verification sweep: both repo heads, clean tree, native 13/13, tooling 29/29, production build sizes at current tip, device state (do NOT touch it). Then research (checklist item 2) and write PLAN-r1.md. Courier it back: the original session runs `/handover review docs/handovers/2026-08-21-ble-hid-pointer`.

## Pointers

- Prior handovers: `docs/handovers/2026-08-21-blink-architecture-pivot/` (state), `docs/handovers/2026-08-17-wireless-capture-session/` (constraints origin, OTA runbook precedent).
- Firmware: `sticks3/src/main.cpp` (modes, arming, USB HID, loop), `sticks3/src/telemetry_mux.{h,cpp}` (do-not-touch contract), `sticks3/src/motion_controller.*` (pointer math), `sticks3/scripts/build_id.py`, `sticks3/platformio.ini` (pinned platform).
- Repo intro: `CLAUDE.md` (lineage + architecture decision), `AGENTS.md`, `PROJECT_KNOWLEDGE.md` (changelog top = full evidence).
- Secrets/env names only: repo-root `.env`, `sticks3/include/colibrino_secrets.h`.
