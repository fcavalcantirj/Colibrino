---
slug: ble-pointer-acceptance
date: 2026-08-22
status: approved
round: 0
author_session: Builder session of the BLE HID pointer (PLAN-r2 executor, 2026-08-21/22): root-caused the 521bc26 boot loop, shipped the OTA boot-health safety net, ran Steps A/B/C and the first cable-free BLE bench with Felipe
---

# Handover — BLE HID pointer: finish acceptance and land it

## Mission

The StickS3 BLE HID pointer exists, is safe by construction, and is hardware-validated for pairing, arming, motion, disconnect/reconnect, and Wi-Fi+BLE coexistence. What remains is the tail of PLAN-r1 Step 5 (USB-cable topology switch, dedicated soak, unknown-peer / forget-re-pair flows), Step 6 (Stage 2: BLE pointer + macOS camera Eye-Blink click, Felipe's usability verdict), a vertical pointer-feel tuning item, and the master FF-merge decision. You are the next session: read this directory plus `docs/handovers/2026-08-21-ble-hid-pointer/` (PLAN-r2, REVIEW-r2, INCIDENT-521bc26 with its resolution, BENCH-2026-08-22), then either continue the bench with Felipe or, if asked, write PLAN-r1 for this handover.

## Where things stand (verified by the author session)

- [REAL] Branch `agent/ble-hid-pointer` at `3de9417` == `origin/agent/ble-hid-pointer`; master = origin/master = `c24a6fe` (unmerged on purpose — acceptance incomplete). Tree clean at handover.
- [REAL] Device (MAC `AC:27:6E:D2:68:B8`, `sticks3-ptt.local`, 192.168.0.194 on 2026-08-22): running `ff1f7d2` (BLE build) on **app1**, otadata VALID and self-confirmed; **app0 holds `254fb7e`** (the pre-BLE telemetry build) — the rollback target. Left armed over BLE at wrap unless Felipe locked/powered it; a boot starts locked regardless.
- [REAL] 521bc26 root cause: `WiFi.setSleep(false)` (`WIFI_PS_NONE`) after the BT controller is enabled → Wi-Fi task `abort()` in `pm_set_sleep_type` (libcoexist: "Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled"; ESP-IDF v4.4.7 ESP32-S3 Wi-Fi guide: "Disabling modem sleep entirely is not possible for Wi-Fi and Bluetooth coexist mode"). Fixed in `a961cef` (BLE build keeps `WIFI_PS_MIN_MODEM`).
- [REAL] Safety net live: OTA image boots `PENDING_VERIFY` → `EVENT,OTA_IMAGE,CONFIRMED,reason=HEALTHY` after 10 s of loop; `EVENT,BOOT,reset=,raw=,slot=,ota_state=,attempts=,last_stage=,last_action=,build=` and `EVENT,LAST_CRASH,…` printed before `USB.begin()` and replayed as `*_REPLAY` on CDC/TCP attach; `EVENT,BOOT,DEADLINE,armed=1,ms=45000`; `scripts/check_image.py` gate (`CHECK_IMAGE,PASS`). Not exercised: the bootloader ABORTED rollback and the deadline/attempt-budget rollback paths (canary deferred, Felipe's call).
- [REAL] Bench 2026-08-22 (details `…/2026-08-21-ble-hid-pointer/BENCH-2026-08-22.md`): pairing 60 s Just-Works → `SECURE,bonded=1` locked; arm → cursor follows head ("horizontal very good, vertical not that much"); physical lock; Mac BT off while armed → `LOCKED,reason=TRANSPORT_TOPOLOGY` + `DISCONNECTED,reason=531`, no self-advertising; 30 s whitelist reconnect → secure, locked until re-arm ("perfect"); coexistence 164.8–165.8 Hz, p50 5.0 ms, p99 ~31 ms, max 35–37 ms, 0 drops, 0 BLE failures (baseline 170.3 Hz / p99 28.1 / max 30.9 ms); heap 112–137 KB; battery 100→90 % in ~15 min streaming.
- [TEST] Gates at `3de9417`: native 33/33, tooling 29/29, Wokwi 18 checks, production builds with/without secrets + `m5stack-sticks3-noble`, all `CHECK_IMAGE,PASS`. Sizes BLE build: RAM 104,788 B (32.0 %), Flash 1,255,389 B (37.6 %), internal-DRAM static 176,720 B.
- [UNVERIFIED] USB-cable topology switch while BLE secure; dedicated 10-min armed soak; unknown-peer rejection; 5 s forget+re-pair; Stage 2 with the camera click; vertical feel after tuning; battery runtime.

Verbatim commands (from `sticks3/`):

```sh
/Users/fcavalcanti/.platformio/penv/bin/platformio test -e native
/Users/fcavalcanti/miniconda3/bin/python3 -m unittest tools/tests/test_capture_tools.py
/Users/fcavalcanti/.platformio/penv/bin/platformio run -e m5stack-sticks3
/Users/fcavalcanti/.platformio/penv/bin/platformio run -e m5stack-sticks3-noble
WOKWI_CLI_BIN=/Users/fcavalcanti/dev/Colibrino/sticks3/.device-backups/tools/wokwi-cli-v0.26.1 PLATFORMIO_CLI_BIN=/Users/fcavalcanti/.platformio/penv/bin/platformio ./scripts/run_wokwi.sh
PLATFORMIO_CLI_BIN=/Users/fcavalcanti/.platformio/penv/bin/platformio ./scripts/upload_ota.sh
COLIBRINO_PIO_ENV=m5stack-sticks3-noble PLATFORMIO_CLI_BIN=/Users/fcavalcanti/.platformio/penv/bin/platformio ./scripts/upload_ota.sh
/Users/fcavalcanti/miniconda3/bin/python3 tools/capture_session.py --tcp --tcp-give-up 0 --quiet
/Users/fcavalcanti/.platformio/penv/bin/python scripts/serial_console.py --log .device-backups/logs/serial-<what>-$(date -u +%Y%m%dT%H%M%SZ).log
```

Cable lane (only on Felipe's go; device in download mode = side button long press until the green LED; port e.g. `/dev/cu.usbmodem101`):

```sh
/Users/fcavalcanti/.platformio/penv/bin/python -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 --baud 460800 --before no_reset --after no_reset read_flash 0xe000 0x2000 .device-backups/otadata-<utc>.bin
/Users/fcavalcanti/.platformio/penv/bin/python -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 --baud 460800 --before no_reset --after no_reset read_flash 0x7F0000 0x10000 .device-backups/coredump-<utc>.bin
/Users/fcavalcanti/.platformio/penv/bin/esp-coredump --chip esp32s3 info_corefile -t raw -c .device-backups/coredump-<utc>.bin --gdb /Users/fcavalcanti/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gdb .device-backups/firmware-upload-<id>.elf
/Users/fcavalcanti/.platformio/penv/bin/python -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 --baud 460800 --before no_reset --after no_reset write_flash <active-slot-offset: 0x10000 app0 | 0x340000 app1> .device-backups/firmware-upload-<id>.bin
```

After any esptool session the chip stays in download mode: **single click** the side button to boot (double click = power off). Stop `serial_console.py` before esptool (it holds the port).

## Blocking constraints (builder: restate these before planning)

1. No flash/upload/OTA without Felipe's explicit in-the-moment go for that exact attempt; `bedside-countdown-s3` never; OTA is the lane for validated builds, the cable is for diagnosis/recovery; read-only download-mode reads are hardware touches too.
2. Fail-closed identity is not negotiable: `MouseOutputPolicy` is the only output authority; pairing never arms; any fault/disconnect/mode exit/OTA/topology change = release-all + disarm; USB wins over BLE, never simultaneous.
3. Never request `WIFI_PS_NONE` (`WiFi.setSleep(false)`) while BLE is compiled in (IDF 4.4 coexistence abort = the incident).
4. Never weaken the safety net: `extern "C" bool verifyRollbackLater()` override (C linkage mandatory), health-gated confirm, deadline + attempt budget, breadcrumbs, `check_image.py`; any board-level-init change gets a cable smoke boot before its first OTA.
5. `telemetry_mux.*` frozen (output-only, `fd() >= 0` gate, no `WiFiClient::write/connected()`).
6. Master never RED; FF only of a green state (native 33, tooling 29, both production builds + noble with `CHECK_IMAGE,PASS`, Wokwi 18); the merge itself is Felipe's/the Architect's call after acceptance.
7. Oracle Loop and `v2/` untouched; `sticks3/tools/**` capture tooling untouched.
8. Truthful labels: `[REAL]` only from hardware evidence.

## Accepted residuals / Refuted — don't fix

- `EVENT,BLE,SECURE,…,output=LOCKED` can repeat mid-session (Mac re-encryption); the `output=LOCKED` text is a static label — cosmetic, armed state is kept. Don't "fix" by touching the policy.
- The 1 s `COLIBRINO_BOOT_CONSOLE_MS` hold is deliberate (Architect keeps the right to set 0 after validation).
- Coexistence max gap 35–37 ms vs 31 ms baseline was flagged, not failed; rate/p99 within bounds. Don't tune the mux.
- `-DCONFIG_BT_NIMBLE_SM_SC_ONLY=1` was removed on purpose (SC-only refuses Just-Works HID reads) — don't reintroduce.
- The attempt budget counts only PANIC/WDT resets on purpose (USB-JTAG reset = `ESP_RST_UNKNOWN` on IDF 4.4; brownout/side button/esp_restart restart the budget) — don't widen.
- The USB CDC read on the Mac was flaky right after killing the logger once; the sanctioned TCP path is the verification lane.
- PLAN-r1's expected post-reboot `hid=NONE` is `hid=USB` whenever a data cable is attached.

## Hard rules & human-reserved decisions

- Every hardware touch IS FELIPE'S CALL in the moment (OTA go, download mode, button presses).
- Vertical pointer-feel tuning (`kVerticalGyroAxis=0`, `kVerticalSign=1`, deadzone 1.4 dps, 18 px/deg, low-pass 0.28) IS A DECISION for Felipe/Architect after a measured look; don't change feel silently.
- The rollback canary (deliberately bad OTA image) IS FELIPE'S CALL.
- FF-merge of `agent/ble-hid-pointer` to master IS THE ARCHITECT'S/FELIPE'S CALL after the pending items.

## Acceptance checklist (the author approves the plan ONLY against these)

1. Plan restates the 8 blocking constraints in its own words.
2. Remaining bench items scheduled with observable pass criteria: USB-cable switch (plug while BLE secure → `LOCKED,reason=TRANSPORT_TOPOLOGY`, `hid=USB`, one head motion moves the cursor once after re-arm; unplug → lock, `hid=BLE` after re-arm), 10-min armed soak (no reset, heap stable, 0 BLE failures, 0 drops), unknown-peer rejection in a reconnect window, 5 s forget+re-pair, battery start/end.
3. Stage 2 procedure for Felipe (BLE pointer + macOS Alternative Pointer Actions Eye Blink click; stationary control + natural-blink control) with his verdict as the only `[REAL]`.
4. Vertical feel: a measured proposal (what to change, why, how verified) — not a silent edit.
5. Docs only change to reflect hardware facts; `PORT_PLAN` T16 status moves to `implemented_and_hardware_validated` only when all items pass.
6. Merge step described as FF of a green tip with the exact gate list, executed only on the explicit call.
7. Nothing in Oracle Loop, `v2/`, `sticks3/tools/**`, `telemetry_mux.*`.

## next_action

Step 0: `git -C /Users/fcavalcanti/dev/Colibrino fetch --all && git status`, re-run the gate commands above, confirm the device reports `build=ff1f7d2` over `tools/capture_session.py --tcp` (read-only). Then run the USB-cable switch test with Felipe (cable in hand), recording with the capture tool.

## Open questions

1. Does Felipe have a 3.3 V USB-TTL adapter for the UART0 tap (HAT2-Bus pin 10 G43, 12 G44, 1 GND)? Nice-to-have, not blocking.
2. Run the rollback canary before merging?

## Pointers

- `docs/handovers/2026-08-21-ble-hid-pointer/` — HANDOVER, PLAN-r1/REVIEW-r1, DECISIONS, PLAN-r2/REVIEW-r2, INCIDENT-521bc26 (with resolution + Step A/B/C + manual facts), BENCH-2026-08-22.
- `PROJECT_KNOWLEDGE.md` 2026-08-22 entry; `sticks3/PORT_PLAN.json` T16/T17; `AGENTS.md` (new standing rules); `CLAUDE.md` (BLE section).
- Evidence (local, ignored): `sticks3/.device-backups/` — `forensics-521bc26/`, `coredump-after-521bc26-*.bin/.info.txt`, `otadata-*.bin`, `firmware-upload-{ff1f7d2,ff1f7d2-noble,a961cef*,027442b*}.{bin,elf}` + `SHA256SUMS-*`, `logs/capture-20260821T230348/` (bench), `logs/serial-step{B,C}-*.log`.
- Public write-up: https://solvr.dev/blog/esp32-s3-arduino-2017-a-blewi-fi-boot-loop-its-flash-core-dump-root-cause-and-the-ota-safety-net-that-should-have-existed
- Reference to review: https://github.com/Teapot174/AirMouseS3 (older S3 air-mouse; not yet read).
- Secrets/env names only: repo-root `.env` (`COLIBRINO_OTA_SECRETS`, `COLIBRINO_OTA_HOST`, `COLIBRINO_OTA_EXPECTED_MAC`, `WOKWI_CLI_TOKEN`), `sticks3/include/colibrino_secrets.h`.
