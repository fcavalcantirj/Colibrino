---
slug: blink-architecture-pivot
date: 2026-08-21
status: open
round: 0
author_session: wireless-capture Builder session (2026-08-17..21): fifth OTA validated, bench equivalence passed, worn Session A run, IMU blink-click channel closed on evidence, Mac camera click validated live
---

# Handover — blink architecture pivot: camera click + StickS3 pointer

## Mission

Colibrino is an accessibility head mouse (StickS3, BMI270, fail-closed HID). This session cycle proved the wireless telemetry path, captured worn data, and — decisively — closed the IMU blink-click channel and validated the replacement architecture live: **StickS3 = head pointer + motion veto; blink click = camera** (macOS native today; miniature IR-proximity IC for the standalone wearable future). "Done" for the successor = Stage 2 combo test (device pointer + camera click), remaining fixture work (C/HS runs, Session B, mount-label fix, promotion → v2 integration), and the §8 sensor evaluation continued per Felipe's pace.

## Where things stand (verified)

- [REAL] Colibrino master = `02249a5` == origin, clean (verified 2026-08-21, committed+pushed this session). Recent: `02249a5` console-only capture guidance; `3dd3688` blink-channel verdict docs; `ff83936` upstream lineage CLAUDE.md; `7970a26` wireless validation records + capture UX; `254fb7e` telemetry accept-gate FIN-race fix.
- [REAL] Device firmware = `254fb7e` (fifth authorized OTA, verified end-to-end by the sanctioned `--tcp` tool: greeting sha match, STATUS `armed=0 ir=0 calibrated=1 imu_blink=0 ota=READY tele=1`). Wireless equivalence acceptance PASSED (rates +1.9–2.4% vs USB baseline, DROPPED 0, parity 4/4, 40 s free-run) — recorded in PORT_PLAN T15 (`implemented_and_hardware_validated`) and PROJECT_KNOWLEDGE 2026-08-18 entry.
- [REAL] The fourth OTA (`6fff220`) exposed a real firmware bug: `if (incoming)` on the accepted telemetry client is `WiFiClient::connected()`, which latches false on the capture host's immediate SHUT_WR FIN → client closed un-greeted. Fixed with `incoming.fd() >= 0` (`254fb7e`), A/B-proven on hardware, triple-lens reviewed vs installed 2.0.17 core.
- [REAL] Worn Session A (`sticks3/.device-backups/logs/capture-20260820T173559`): R0/HB1/U valid clean controls; V1 and V2 `blink=0` (missed), V2 also `head=1` (control false positive); C and HS never ran; 5 of 8 run slots used. Earlier same-day dirs `231024` (BOOT done + R0 valid) and `232438` (R0 valid) also exist.
- [REAL] Detector-eye-view analysis of those traces: stillness noise floor median 1.04 dps vs the 1.1 dps impulse threshold (zero margin; 7 impulse-shaped events inside the still control); his firm blinks peak 30–49 dps, far above the 2.5 dps head-motion veto, which was active 24–31% of blink stages. The gyro click window excludes his real blinks. Full numbers in PROJECT_KNOWLEDGE 2026-08-21 entry.
- [REAL] Protocol §8 evaluation OPENED (trigger: V2 control-stage sequence — a listed §8 condition). VL53L4CD-class ToF REJECTED on datasheet physics (±7 mm accuracy, ~70 mm floor vs 2–5 mm lid excursion); viable on-device class = integrated IR-proximity ICs (VCNL4040 / TMD2635 — Google Glass's LTR-506ALS class).
- [REAL] Research verdict (6 web agents, sources in workflow outputs + PROJECT_KNOWLEDGE): NO shipping product detects blinks from motion alone; consensus split everywhere is eye-sensor/camera = blink, IMU = head + veto.
- [REAL] **Mac camera click VALIDATED LIVE 2026-08-21** by Felipe on macOS Tahoe 26: Accessibility → Pointer Control → Alternate pointer actions → add trigger via the **`+` button → Facial Expression → Eye Blink → Left Click** (the Reassign button captures only keys/switches), Camera Options → Eye Blink → **Exaggerated**. The recognized gesture is a deliberate LONG FIRM CLOSURE of both eyes; verdict "works decent once you get the hang". Sensitivity (Slight/Default/Exaggerated) is the per-user calibration dial. Head Pointer stays OFF.
- [REAL] Upstream lineage recorded in repo `CLAUDE.md` (created this session): `github.com/tix-life/Colibrino`, GPL-3.0; local fork carries full history, all 25 upstream files byte-identical except the extended README; upstream issue #6 = unanswered Bluetooth ask. Legacy learnings (TCRT5000 sync demodulation, per-user calibration, Mahony derivative mapping, etc.) distilled there.
- [REAL] Capture tool UX rebuilt and committed: stage-synchronized banners (GET READY at prepare, `NOW (n SECONDS):` at stage start, RUN DONE, humane results), operator-first status line with countdown truncated to terminal width, mount announcements on every probe run, `--voice` opt-in (silent by default; cue BEEPS kept — HB/U/C runs are timed by them), `--verbose-status` restores the old line. Tooling tests 29/29 throughout.
- [TEST] Session A's captured V-runs as *fixtures*: pipeline-clean (30 candidates written, parity 4/4 on the bench session) but review_required and NOT promoted; free-run fixtures additionally blocked by FREE_RUN_SCHEMA_NOTE until `v2/traces/labels.schema.json` (core branch) gains `CAPTURE_FREE_RUN`.
- [UNVERIFIED] StickS3 head-pointer path on `254fb7e` specifically: composite USB HID + head pointer were hardware-validated on earlier firmware (2026-08-15 era) and the pointer code is untouched by all telemetry work (native 13/13 every build), but nobody has plugged the current build in as a mouse yet. Stage 2 verifies it.

Verbatim commands (from `sticks3/`):

```sh
/Users/fcavalcanti/miniconda3/bin/python3 tools/capture_session.py --tcp --plan tools/capture_plans/round1.json --plan-session A
/Users/fcavalcanti/miniconda3/bin/python3 -m unittest tools/tests/test_capture_tools.py
/Users/fcavalcanti/.platformio/penv/bin/platformio run -e m5stack-sticks3
./scripts/upload_ota.sh
```

## Blocking constraints (unchanged from the 2026-08-17 handover — restate before planning)

All 8 constraints of `docs/handovers/2026-08-17-wireless-capture-session/HANDOVER.md` stand verbatim: (1) no upload/flash without Felipe's explicit in-the-moment go, never `bedside-countdown-s3`; (2) Oracle Loop merge-frozen without the exact phrase; (3) master never RED, v2 core branch-only until fixtures + green integration; (4) telemetry output-only, guards stay; (5) blink clicking gated off — `imu_blink_validated` untouched (now doubly so: the channel is closed); (6) fixture privacy + Felipe clearance lines; (7) session caps + acceptance by run ID; (8) no `WiFiClient::write`/`connected()` in the telemetry path (the accept gate is `fd() >= 0` — deliberate).

## Accepted residuals / Refuted — don't fix

- The IMU coded blink detector stays in firmware for data collection; it actuates nothing. Do NOT tune its thresholds chasing acceptance — the channel is closed on physics + industry evidence; re-litigation needs new hardware, not new constants.
- Voice guidance was REMOVED from the capture tool after repeated operator frustration (speech queue lags stages). Do not re-enable by default; `--voice` exists.
- The `n`-navigation flow and 3 s free-run double-tap window confused the operator repeatedly; banners now cover it. Don't re-add voice to fix UX.
- `round1.json` mount labels say `glasses_right_temple` but the device rides the LEFT temple — fix the labels (and any captured-session metadata) BEFORE any fixture promotion. Deliberate open item, not yet done.
- Mirrored sunglasses (the capture rig frames) block camera blink detection — expected, recorded; it is the argument for the on-device IR sensor lane later, not a bug to fix now.
- V1 redo was skipped mid-session (operator flow); acceptance path if ever needed is V2+V3 — but with the channel closed, worn ACCEPTANCE is moot; the runs' value is fixtures.
- Wokwi untouched this whole cycle (portable logic unchanged); handover-era residuals (`.dasbrow/`, third-OTA CDC supersession, IR pair rejection) all stand.

## Hard rules & human-reserved decisions

- Stage 2 arming (MOUSE mode, 2 s hold) IS FELIPE'S physical act; USB plug-in for pointer use is fine (cable-free was a CAPTURE decision, not a mouse-use decision).
- Sensor purchases (VCNL4040/TMD2635 class) — FELIPE'S CALL; §8 evaluation is open, not a purchase order.
- C/HS runs, Session B, fixture clearance/promotion — FELIPE'S CALL on timing.
- Custom Mac blink helper (MediaPipe blendshapes + CGEventPost; EyeCommander as zero-code alt) only if the native path proves insufficient — do not build unasked.
- Report truthfully; label [REAL]/[TEST]/[UNVERIFIED].

## next_action

Stage 2 combo test when Felipe says go: plug StickS3 in via USB-C → confirm composite HID enumerates (`ls /dev/cu.usbmodem*`) → Felipe taps to MOTION, long-holds 2 s to MOUSE, long-holds 2 s to ARM (screen: `OUTPUT: ARMED`, `blink click: DISABLED`) → head moves cursor, Mac camera long-blink clicks. Record the outcome in PROJECT_KNOWLEDGE as the first full-architecture experience.

## Pointers

- PROJECT_KNOWLEDGE.md 2026-08-21 + 2026-08-18 entries (full evidence); PORT_PLAN.json T15; protocol §8 (evaluation-opened paragraph).
- Research reports: workflow outputs `wjxsuksz8` (blink sensing industry/literature/sensors) and `wpjgm2323` (Apple-native/projects/helper-spec) under the session task dir — distilled conclusions live in PROJECT_KNOWLEDGE.
- Prior handover (still authoritative for constraints): `docs/handovers/2026-08-17-wireless-capture-session/`.
- v2 core branch `agent/v2-core-scaffold` (`00c1285`); Oracle Loop main `325c3ea`, two stacked branches unmerged, merge-frozen.
