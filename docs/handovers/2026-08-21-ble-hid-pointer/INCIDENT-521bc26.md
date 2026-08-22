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

## Resolution — root cause found (Builder, 2026-08-22, PLAN-r2 Step A) [REAL]

Read-only readback with the device in download mode (Felipe's go; `esptool --before no_reset read_flash`, nothing written):

- otadata (`sticks3/.device-backups/otadata-20260822T011727Z.bin`): entry0 `ota_seq=7 VALID`, entry1 `ota_seq=6 VALID` → app0 active (where 521bc26 had been installed), both slots VALID — the bootloader's NEW→PENDING_VERIFY→VALID chain demonstrably ran on this unit, and `initArduino()` had confirmed 521bc26 on its first boot (why no rollback ever happened).
- coredump partition (`sticks3/.device-backups/coredump-after-521bc26-20260822T011727Z.bin`, SHA-256 `478516de…`): a 24,420-byte ELF-format dump, app SHA `2f533da347509066` = the retained 521bc26 ELF. Decoded with `esp-coredump 1.16.0` + `xtensa-esp32s3-elf-gdb` (`…coredump-after-521bc26-20260822T011727Z.info.txt`):
  - crashed task `wifi` (prio 23), `abort() was called at PC 0x420b25a7 on core 0`
  - backtrace: `abort()` ← `pm_set_sleep_type` ← `wifi_set_ps_process` ← `ieee80211_ioctl_process` ← `ppTask`
  - `pm_set_sleep_type` disassembly: the abort is reached only for sleep type NONE when the coexistence check reports Bluetooth enabled, after `wifi_log`-ging the `libcoexist.a` string *"Error! Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled!!!!!!"*.
- Our `startOtaNetworking()` called `WiFi.setSleep(false)` (= `WIFI_PS_NONE`) after `ble_mouse.begin()` had enabled the BT controller. 254fb7e never enabled BT, so the same call was legal there. Result per boot: M5 up → USB up → BLE init OK → Wi-Fi start → Wi-Fi task aborts → panic → reboot — the observed loop (display flashing, never joined Wi-Fi, USB composite flapping). Not a NimBLE bring-up crash, not heap, not brownout, not a hang.
- Official rule, read after the decode (Felipe: "read official manual"): ESP-IDF v4.4.7 ESP32-S3 Wi-Fi driver guide (`api-guides/wifi.html`, "ESP32-S3 Wi-Fi Power-saving Mode → Station Sleep"): *"Disabling modem sleep entirely is not possible for Wi-Fi and Bluetooth coexist mode."* Espressif maintainer on esp-idf issue #9595 (2022-08-22): *"WiFi modem sleep is the essential prerequisite for coexistence now, so that AP could buffer the data for our stations"*; the requirement was lifted only on IDF v5.0+ (2024-02-27 follow-up with the commits). Our core is Arduino-ESP32 2.0.17 = IDF v4.4.7, so the rule applies in full.
- Fix (commit on `agent/ble-hid-pointer`): the BLE build keeps `WIFI_PS_MIN_MODEM` (IDF default, required by coexistence); the BLE-less build keeps the legacy `setSleep(false)`. Telemetry rate under modem sleep + BLE is a hardware measurement (PLAN-r1 Step 5.7 coexistence gate), not assumed.
- Safety net now in place (027442b): `verifyRollbackLater()` override + health-gated confirmation, boot-attempt budget, deadline rollback, boot/crash breadcrumbs, post-link image gate — a repeat of this class of failure on the OTA lane would self-revert after one boot and report why.
