# Incident — 521bc26 boot loop and recovery (Architect record, 2026-08-21)

## What happened [REAL]

The Builder's `521bc26` (BLE pointer build, branch `agent/ble-hid-pointer`) was OTA-deployed and never verified: the device boot-looped (screen flashing on power, dark after forced power-off), never joined Wi-Fi (no mDNS, no telemetry), and never enumerated stable USB. Post-OTA verification correctly refused and stopped (evidence: `sticks3/.device-backups/post-ota-verification-retry-521bc26-20260821T195105Z.txt`).

## Recovery [REAL]

Cable flash under Felipe's explicit authorization (the "OTA impossible" clause): StickS3 download mode = USB connected, hold the SIDE reset button ~2 s until the internal green LED lights (M5 official procedure — NOT the front button). Enumerated as `/dev/cu.usbmodem101`; `esptool write_flash` wrote the archived `firmware-upload-254fb7e.bin` into BOTH app slots (0x10000 and 0x340000, `default_8MB.csv`), hashes verified. Device boots normally: `IR PROBE` screen, Wi-Fi joined, greeting `EVENT,TELEMETRY,CONNECTED,254fb7e,dropped=0`, batt 100%.

Two durable operational facts gained: a **data-capable USB cable now exists and works** (all earlier USB failures were a charge-only cable), and the download-mode procedure is proven. Serial-console debugging is therefore available for the first time in the telemetry era.

## Requirements before any further OTA of this feature (Architect, binding)

1. **Root-cause the boot loop.** Candidates to rule in/out: NimBLE init at boot (RAM/heap exhaustion — measure against the plan's own thresholds), interaction with `qio_opi` PSRAM config, init order vs `USB.begin()`/M5Unified, a panic in the new setup path. The build gates (native/Wokwi/link) could not catch a board-level init crash — say what CAN catch it.
2. **Use the cable next time, deliberately**: with Felipe's authorization, the next candidate build should be cable-flashed to app0 while the serial console is captured — a boot panic is then visible in seconds instead of a blind OTA failure. OTA remains the delivery lane for validated builds (Felipe's standing rule); diagnosis of unproven builds may use the cable.
3. **Explain the scope deviation**: the diff edits `sticks3/tools/capture_session.py` (+179) and `tools/capture_plans/round1.json` (+20), which PLAN-r1's own exclusion list (checklist item 10) put outside the edit set. Justify or revert.
4. Deliver 1–3 as **PLAN-r2** through the handover ritual before requesting any new hardware go.

Device state: recovered, `254fb7e`, healthy. Branch `agent/ble-hid-pointer` untouched — code work may continue; hardware is gated as above.
