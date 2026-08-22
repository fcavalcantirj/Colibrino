---
slug: ble-hid-pointer
round: 2
builder_session: Claude Code Builder session, 2026-08-21 — post-incident; read-only forensics of the retained 521bc26 ELF/payload, Arduino-core/NimBLE source audit, 7-agent adversarial verification; device observed read-only over USB only
---

# PLAN-r2 — BLE HID pointer: root cause, cable-console lane, scope clarification

## Blocking constraints, restated (HANDOVER 1–8 + INCIDENT 1–4, all binding)

1. No flash/upload/OTA without Felipe's explicit in-the-moment go for that exact attempt (port + device named); `bedside-countdown-s3` never; OTA is the delivery lane for validated builds; the cable is allowed for DIAGNOSIS of unproven builds and for recovery, each under its own go. Read-only device operations (download-mode `read_flash`) are hardware touches too and get their own go.
2. One fail-closed identity across USB and BLE: boot locked; arming only by the deliberate 2 s hold in MOUSE mode; any fault/disconnect/mode-exit/OTA/topology change = release-all + disarm; pairing never arms; no failover keeps an armed state. r2 instrumentation never touches `mouse_output`.
3. BLE bonded + encrypted, bounded deliberate windows, deliberate re-pair (DECISIONS.md). r2 fixes one flag that would have broken this at pairing time (SC-only), keeping the bonded+encrypted gate.
4. `telemetry_mux.*` frozen; r2 emits boot breadcrumbs from `main.cpp` through the existing `diagnostics.printf` and, before `USB.begin()`, through plain `printf()`.
5. Master never RED: work stays on `agent/ble-hid-pointer`; master moves only by FF of a green state (native all cases, tooling 29/29, production build with and without secrets, Wokwi green — r2 adds one small portable policy with native + Wokwi coverage).
6. Oracle Loop and `v2/` untouched.
7. USB HID unchanged; BLE additive.
8. Truthful labels: `[REAL]` only from hardware evidence; compile/test/static = `[TEST]`; else `[UNVERIFIED]`.
9. (INCIDENT-1) Root cause before any further OTA of this feature; say what gate catches board-level init crashes.
10. (INCIDENT-2) Next candidate is cable-flashed to app0 with the serial console captured, under Felipe's authorization; OTA stays the lane for validated builds.
11. (INCIDENT-3) Scope deviation explained or reverted.
12. (INCIDENT-4) All of it through this ritual before requesting any new hardware go.

## Handover discrepancies (re-verified 2026-08-21, read-only)

| Claim | Verified now |
| --- | --- |
| master `a3c98de` | master = origin/master = `c24a6fe` = merge-base with the branch; branch tip `1a6fa19` = master + `f29a72b` + `521bc26` + `1a6fa19`; tree clean |
| device `254fb7e` healthy | from INCIDENT `[REAL]` (Architect); confirmed read-only over USB this session (next row) |
| "USB data path on telemetry-era builds UNVERIFIED" (HANDOVER) | **now `[REAL]` 2026-08-21**: with the data cable, the TinyUSB composite enumerates as "Colibrino StickS3 Prototype" (VID 0x303A, PID 0x1001, serial `AC276ED268B8`); CDC `/dev/cu.usbmodemAC276ED268B82` @115200 streams `STATUS,…,build=254fb7e,armed=0,imu=1,calibrated=1,imu_blink=0,ir=0,ota=READY,batt=100,tele=0` at 1 Hz (6 s read-only monitor capture, the documented README method; no download mode, no esptool, nothing written). Composite CDC name pattern `usbmodem<serial>2` vs the ROM USB-Serial/JTAG `usbmodem101`-style — the serial logger opens both |
| "diff edits capture_session.py (+179) / round1.json (+20)" | **not on the branch**: `git diff --stat master...agent/ble-hid-pointer` = 9 files, zero under `sticks3/tools/`; the numbers reproduce with `git diff --stat 254fb7e agent/ble-hid-pointer -- sticks3/tools` and come from master commits `7970a26` + `02249a5` (pre-branch) |
| baseline sizes 98,244 / 1,063,597 | consistent; 521bc26: `.dram0.data` 24,584 + `.dram0.bss` 79,336 = 103,920 B (31.7 %), payload 1,251,120 B (37.4 %). True internal-DRAM cost of BLE ≈ 17.7 KB (IRAM shadows DRAM 1:1) — PIO's "RAM" hides `.dram0.dummy` |
| retained artifacts | `sticks3/.pio/build/m5stack-sticks3/firmware.bin` SHA-256 == `firmware-upload-521bc26.bin` == OTA-attempt record (`c19e4a72…`); `firmware.elf` SHA-256 (`2f533da3…`) == payload app-descriptor ELF hash → authoritative symbols for the boot-loop build |
| native 13/13, tooling 29/29, Wokwi 11 checks | branch: 24 native cases (13 + 11 policy), Wokwi 15 checks (11 + 4 policy); not re-run in this read-only planning turn — execution Step 0 re-runs; AGENTS/README "thirteen"/"eleven" are stale on the branch |
| NimBLE 2.5.1 / M5GFX 0.2.26 | installed (`.pio/libdeps/.../integrity.dat`) |
| PLAN-r1 runbook expects `ble=IDLE,hid=NONE` post-reboot | with a USB data host attached, wired-preferred selects `hid=USB`; r2 states both |
| PLAN-r1 "SC-only" flag | `-DCONFIG_BT_NIMBLE_SM_SC_ONLY=1` is functionally wrong for Just-Works (Root cause § functional defects) — r2 removes it |

## Acceptance checklist — point-by-point

HANDOVER 1–10: PLAN-r1's answers stand (APPROVED r1); r2 changes only what the incident requires — 1 restated incl. incident items; 2 stack unchanged, Bluedroid fallback reachable only through the decision table; 3/4 unchanged + one flag fix; 5/7 bench plans unchanged, re-sequenced (cable diagnosis boot first, BLE bench cable-free on battery); 6 predicted vs measured: static known, runtime from the `BLE_HEAP` prints on the cable boot, stop thresholds unchanged; 8 rollout rewritten (Steps A–D below, explicit failure branches); 9 docs after hardware pass + INCIDENT resolution note; 10 scope fence kept, explicit amendments listed.
INCIDENT 1 → "Root cause"; 2 → "Cable-console lane"; 3 → "Scope deviation"; 4 → this document.

## Root cause (INCIDENT-1)

### Facts
- `[REAL]` Boot loop after OTA of 521bc26: display re-initialising, no Wi-Fi/mDNS, no stable USB; cable recovery (INCIDENT).
- `[TEST]` Crash window = after `M5.begin()` (`main.cpp:1245`) and before `startOtaNetworking()` (`:1288`); in between: `USB.begin()` (`:1268`), `EVENT,BUILD` + `BLE_HEAP,BEFORE_INIT` (`:1273-1275`), `ble_mouse.begin()` (`:1280` → `NimBLEDevice::init` → controller init/enable → `esp_nimble_hci_init` → `nimble_port_init` → host task → sync wait → server/HID/advertising → bond/whitelist), `updateMouseOutputState()`.
- `[TEST]` Config delta vs the validated build is exactly NimBLE + M5GFX pin + five `-DCONFIG_BT_NIMBLE_*` flags (`git diff 254fb7e 521bc26 -- sticks3/platformio.ini`); partition table, `qio_opi`, USB mode, core unchanged. No NimBLE object is built at static-init time (`nm`: `ble_mouse` is a 72-byte plain global).
- `[TEST]` A clean `NimBLEDevice::init()==false` (e.g. controller init returning `ESP_ERR_INVALID_ARG`) does NOT loop: `begin()` returns false, output is force-locked, setup continues to Wi-Fi → telemetry would have shown `ble=FAULT`. Not observed ⇒ the failure was a panic, a hang, or a hardware reset.
- `[TEST]` Internal DRAM heap at reset for 521bc26 ≈ 282,896 B (458,752 total − 175,856 below `_heap_start` 0x3FCB2EF0); expected ≈ 235–250 KB at `BEFORE_INIT`; BLE bring-up ≤ 80 KB (pools ≈ 9.9 KB + two task stacks + controller state + GATT objects); Wi-Fi ≤ 70 KB. A Wi-Fi OOM after BLE cannot loop (`WiFiGeneric.cpp` only `log_e`s).
- `[TEST]` Asserts are live (`CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE`, no `-DNDEBUG`; `__assert_func` in the map); C++ exceptions are ON with a zero emergency pool and NimBLE has no try/catch ⇒ any OOM in NimBLE's `new` → `std::terminate` → abort. The project's `std::nothrow` on `NimBLEHIDDevice` protects only the outer allocation.
- `[TEST]` Core-dump-to-flash ON (ELF, CRC32; partition `coredump` 0x7F0000/64 KB, byte-identical table on the device per the pre-Colibrino backup); the panic handler dumps unconditionally for every panic class (abort/Guru/INT-WDT/TWDT — ELF disassembly); brownout has no ISR → no dump; hang → no dump; 254fb7e boots never erase it (`esp_core_dump_init` only checks). `[UNVERIFIED]` whether a dump is present until read.
- `[TEST]` Rollback: ArduinoOTA → `Update.end()` → `esp_ota_set_boot_partition` writes `ESP_OTA_IMG_NEW` (linked `libapp_update.a` disassembly); the prebuilt 4.4.7 bootloader does NEW→PENDING_VERIFY and PENDING_VERIFY→ABORTED (`bootloader_qio_80m.elf` disassembly); but `initArduino()` marks VALID before `setup()` (`esp32-hal-misc.c:222-239`, weak `verifyRollbackLater()`=false) ⇒ a `setup()`-stage crash can never roll back today. The pre-Colibrino otadata already showed `VALID(2)` entries (the device's bootloader has run NEW→PENDING→VALID); the active-entry readback settles it for the installed bootloader.
- `[TEST]` Console: `USB.begin()` → `usb_hal_init` hands the single internal USB PHY to OTG via RTC-domain bits (`RTC_CNTL_SW_HW_USB_PHY_SEL`/`SW_USB_PHY_SEL`) that survive software resets (Arduino's own `usb_switch_to_cdc_jtag()` must clear them before a restart-to-bootloader) ⇒ in a panic loop the USB-Serial/JTAG port does NOT reappear per cycle: one console window per HARD reset (power-on / side button), ending at `USB.begin()`. Panic text goes to UART0 (+ a JTAG FIFO nobody can read); the TinyUSB CDC can never carry panic text (`USBCDC::write` returns 0 unless connected). The operator's "never enumerated stable USB" is the OTG composite flapping — consistent.

### Differential diagnosis (after adversarial review)
| # | Hypothesis | Plausibility | Discriminator |
| --- | --- | --- | --- |
| H1 | panic in NimBLE host/controller bring-up on this target (live asserts: `ble_hs_init` ×12 `SYSINIT_PANIC_ASSERT`, `os_msys_init_once` ×2, `ble_transport_init` ×5, `ESP_ERROR_CHECK(esp_timer_create)` in `npl_freertos_callout_init`; or inside the precompiled controller) | HIGH | dump present; `exc_pc`/backtrace in `NimBLEDevice::init`/`nimble_port_*`/`ble_hs_init`/`esp_bt_controller_*`; `esp_reset_reason()==ESP_RST_PANIC` |
| H2 | internal-heap exhaustion → `new`/`esp_timer_create` failure → abort | LOW (≤ 80 KB needed vs ≈ 235–250 KB free) | `BLE_HEAP,BEFORE_INIT,free=…` ≥ 200 000 kills it; dump PC in `heap_caps_malloc`/`__cxa_throw`/`abort` from `npl_os_freertos.c:445` |
| H3 | task/interrupt WDT panic | LOW (loopTask not TWDT-subscribed; TWDT watches idle CPU0 only) | `ESP_RST_TASK_WDT`/`ESP_RST_INT_WDT`; dump task IDLE0/ISR |
| H4 | brownout | LOW (level 7 = 2.44 V trip, most permissive; BLE TX peak 193 mA < Wi-Fi 283–340 mA already sustained on 254fb7e) | `ESP_RST_BROWNOUT` AND empty coredump partition |
| H5 | hang in `NimBLEDevice::init()` `while(!m_synced)` (no timeout; TWDT-invisible; yields on core 1) | LOW-MED | no dump; reset reason POWERON/EXT only (operator); `BLE,INITIALIZED` never printed |
| H6 | crash in new `main.cpp` glue | LOW | dump PC in `main.cpp` symbols |
| — | "`ble_max_act=2` rejected by the precompiled controller" (NimBLE issue #643 signature on 2.0.x platforms) | not a loop cause (clean `init()==false`) | would show `ble=FAULT` + Wi-Fi up — check on the cable boot anyway |

### Evidence plan (ordered; each step is Felipe's go)
A. **Read-only readback** (download mode = side button ~2 s → green LED; port as enumerated, e.g. `/dev/cu.usbmodem101`):
   `esptool.py --chip esp32s3 --port <port> read_flash 0xe000 0x2000 .device-backups/otadata-<utc>.bin` and
   `esptool.py --chip esp32s3 --port <port> read_flash 0x7F0000 0x10000 .device-backups/coredump-after-521bc26.bin`; then
   `esp-coredump --chip esp32s3 info_corefile -t raw -c <bin> --gdb ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gdb sticks3/.pio/build/m5stack-sticks3/firmware.elf`
   (`esp-coredump 1.16.0` in the PIO penv supports the 4.4 `ELF_CRC32` 0x0102 format and refuses a stale dump by app-SHA mismatch).
   Outcomes: decoding dump ⇒ panic reason + task + PC + backtrace (H1/H2/H3/H6 settled); "Invalid application image for coredump" or no dump ⇒ 521bc26 never panicked ⇒ H4/H5 (decided by C). otadata: active slot (expected app0) + `ota_state` (VALID proves the bootloader rollback chain on this unit).
B. **Instrumentation-only build over the normal OTA lane** (BLE init compiled out by `COLIBRINO_BLE_ENABLED=0`, everything else identical): proves the safety net and the self-reporting on a known-good behaviour set, cable-free. Expected: `EVENT,BOOT,reset=SW,slot=<other>,ota_state=PENDING_VERIFY,attempts=1,last_stage=0` → `EVENT,OTA_IMAGE,CONFIRMED` ~10 s later → `EVENT,LAST_CRASH,…` if a 521bc26 dump exists (summary read on-device: task, pc, exccause, 16-PC backtrace → `xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf` against the retained ELF) — i.e. **B also delivers the root cause without download mode** if it was a panic; plus `BLE_HEAP`-style heap prints before/after Wi-Fi.
C. **BLE candidate over the cable with console** (INCIDENT-2 lane; procedure below). Expected: `EVENT,BOOT,…`, stage markers, `BLE_HEAP,BEFORE_INIT`, `BLE,INITIALIZED`, `BLE_HEAP,INITIALIZED`, Wi-Fi, greeting on TCP, STATUS `ble=IDLE,hid=USB` (cable attached). A crash now yields a decoded `LAST_CRASH` on the next hard boot plus a fresh dump.
D. **Decision table** (no threshold relaxation, no silent stack swap; each branch returns to the Architect before a new hardware go): H1 in controller/HCI/host init → config-level fix if the backtrace points at one, else the declared Bluedroid fallback (new round); H2 → gate BLE on headroom + footprint knobs (Architect threshold); H3 → remove the blocking call; H4 → power-path question to Felipe; H5 → transport/sync config or fallback; H6 → fix + native test where possible.

### Functional defects found (fix in the candidate regardless of the loop cause)
- `[TEST]` `-DCONFIG_BT_NIMBLE_SM_SC_ONLY=1` ⇒ `ble_hs_cfg.sm_sc_only=1` ⇒ `ble_att_svr.c:329-343` rejects every `READ_ENC`/`AUTHEN` attribute unless the link is authenticated (level 4). Just-Works with `BLE_HS_IO_NO_INPUT_OUTPUT` is unauthenticated ⇒ HID input reports unreadable ⇒ the mouse never works with macOS. Remove the flag; keep `setSecurityAuth(bond, false, true)` (SC on, not SC-only) and the `isEncrypted() && isBonded()` gate.
- `[TEST]` NimBLE C++ INFO logging is on (`CONFIG_NIMBLE_CPP_LOG_LEVEL` ← `CORE_DEBUG_LEVEL=3`) and runs newlib `printf` on the 4 KB host task inside callbacks; hardening option: `-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192` (4 KB internal) — Architect yes/no.
- `[TEST]` Latent library trap noted, not triggered by our code: `NimBLEHIDDevice::locateReportCharacteristicByIdAndType` dereferences the 0x2908 descriptor without a null check; we only ever create input reports through `getInputReport()`.

### What gate can catch this class — honest answer
No simulation gate: Wokwi and Espressif QEMU implement no Bluetooth for ESP32-S3, and the repo's Wokwi env excludes `main.cpp`/`ble_mouse_transport.cpp`; the native suite compiles portable sources only. What CAN catch it, layered:
1. **Process:** cable console smoke boot before the first OTA of any build that changes board-level init (new RF stack/tasks, USB mode, partition, core/lib upgrade) — now feasible; rule goes into AGENTS.md after validation.
2. **Static gates (post-build `scripts/check_image.py`; would have FAILED 521bc26):** (a) `verifyRollbackLater` must resolve to a project object, not `libFrameworkArduino.a` (map check); (b) true internal-DRAM budget = `.dram0.dummy + .dram0.data + .dram0.bss` from the map, printed, with a ceiling (proposed 200 KB; today 175,856 B); (c) observability: `esp_reset_reason` linked and the `EVENT,LAST_CRASH` string present in the image.
3. **Firmware self-protection (permanent):** `extern "C" bool verifyRollbackLater(){return true;}` (C linkage is mandatory — no header declares it; a C++ definition mangles and silently no-ops) + health-gated `esp_ota_mark_app_valid_cancel_rollback()`; RTC-noinit boot-attempt counter (3 consecutive non-power-on boots without reaching healthy → `esp_ota_mark_app_invalid_rollback_and_reboot()` when `esp_ota_check_rollback_is_possible()`); a 45 s boot-health deadline (`esp_timer` one-shot) for hangs; OTA guard: in `ArduinoOTA.onStart`, if the running image is still PENDING_VERIFY, confirm it first (Arduino's `Update` never calls `esp_ota_begin`, so IDF's own guard never fires). Confirmation never depends on Wi-Fi. Converts a boot-looping OTA into one failed boot + automatic return to the previous slot.
4. **Firmware self-reporting (permanent):** `EVENT,BOOT,reset=<esp_reset_reason>,slot=<label>,ota_state=<state>,attempts=<n>,last_stage=<k>` and, when `esp_core_dump_image_check()==ESP_OK`, `EVENT,LAST_CRASH,task=…,pc=0x…,cause=…,vaddr=0x…,sha=<16>,bt=0x…;…` from a heap-allocated `esp_core_dump_summary_t` (never erased); stage markers in RTC memory (1 M5.begin, 2 USB.begin, 3 BLE, 4 after BLE, 5 Wi-Fi started, 6 setup done); printed with `printf()` BEFORE `USB.begin()` (reaches UART0 + USB-Serial/JTAG) and again through `diagnostics` after; re-emitted once when a telemetry client attaches (`clientConnected()` edge observed from `main.cpp`; mux untouched); STATUS tails `reset=`, `lc=`.
5. **Measured-budget gate on hardware:** the `BLE_HEAP` stages vs PLAN-r1 stop thresholds (free < 80 KiB / largest < 40 KiB ⇒ stop).

## Cable-console lane (INCIDENT-2) — what the console can and cannot show, and the procedure
- **Full console = UART0 tap:** HAT2-Bus pin 10 = G43 (TX), pin 12 = G44 (RX), pin 1 = GND (M5Stack StickS3 docs), 3.3 V USB-TTL adapter @115200; carries ROM banner, bootloader, `log_*`, `printf`, the full panic backtrace, immune to `USB.begin()`. **Question for Felipe: is a 3.3 V USB-TTL adapter and access to the HAT header available?** If yes, this is the primary capture.
- **Without the tap:** the USB-C port gives one USB-Serial/JTAG window per hard reset (power-cycle or side button), closing at `USB.begin()`. The candidate therefore prints the breadcrumbs (reset reason, attempts, last stage, LAST_CRASH summary, heap) with `printf()` before `USB.begin()`, preceded by a 1 s console window (`COLIBRINO_BOOT_CONSOLE_MS`, default 1000, Architect may set 0) so the host reader catches it; after `USB.begin()` the composite CDC shows `diagnostics` until a crash. Every crash is thus visible on the NEXT hard boot even cable-free.
- `scripts/serial_console.py` (new; pyserial; reconnecting; opens every `/dev/cu.usbmodem*` + an optional `--uart <port>`; tags lines by port; writes `.device-backups/logs/serial-<id>-<utc>.log`); `platformio.ini` gains `monitor_filters = esp32_exception_decoder`.
- Procedure: 1) gates green, payload archived `firmware-upload-<id>.bin` + SHA-256; reader running. 2) Announce; **WAIT** for Felipe's go naming port + device. 3) Download mode; read otadata first (flash into the ACTIVE slot it names — expected app0; if it names app1, stop and report); `esptool.py --chip esp32s3 --port <port> --baud 460800 write_flash 0x10000 <bin>`; esptool MD5 verify must pass. 4) Felipe power-cycles; watch ≥ 3 min; TCP receive-only verification (`tools/capture_session.py --tcp`): greeting id, STATUS `armed=0 … ble=IDLE, hid=USB` (cable) / `hid=NONE` (unplugged), `reset=`, `ota_state=VALID` (cable images are VALID), heap numbers logged. 5) Pass → continue to PLAN-r1 Step 5 cable-free on battery; fail → stop, archive the log, decode, report; recovery = `firmware-upload-254fb7e.bin` to both slots on Felipe's go. The cable-flashed candidate relies on the attempt counter + deadline (otadata stays VALID); the OTA-armed rollback is exercised by Step B and every later OTA.

## Scope deviation (INCIDENT-3) — explained, no revert
The capture-tool and `round1.json` changes are master commits `7970a26` and `02249a5` (Architect session, 2026-08-20/21), present because the diff baseline was the device build `254fb7e` (8 commits below master) instead of the branch base; `git diff --stat master...agent/ble-hid-pointer` contains no `sticks3/tools/**` change and `git merge-base master agent/ble-hid-pointer` = `c24a6fe` = master's tip. PLAN-r1's exclusion list was honoured.

## Scope amendments for r2 (explicit; Architect yes/no)
1. `sticks3/src/main.cpp` board code: boot breadcrumbs + LAST_CRASH, stage markers, `verifyRollbackLater` override (`extern "C"`), health-gated confirmation, attempt counter + deadline, OTA-during-PENDING guard, STATUS tails; `COLIBRINO_BLE_ENABLED` compile switch for Step B.
2. Portable `include/colibrino/boot_health.h` + `src/boot_health.cpp` (`BootHealthPolicy`: inputs = reset class, attempts, elapsed, setup-done; outputs = confirm / rollback / wait) with native tests (≥ 6) and a Wokwi check — decision logic tested natively, IDF calls in the adapter.
3. `sticks3/platformio.ini`: remove `-DCONFIG_BT_NIMBLE_SM_SC_ONLY=1`; add `monitor_filters = esp32_exception_decoder`; add `post:scripts/check_image.py`; optional `-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192`.
4. New `sticks3/scripts/check_image.py`, `sticks3/scripts/serial_console.py` (scripts, not capture tooling).
None touches `telemetry_mux.*`, `sticks3/tools/**`, capture plans, detector constants, Oracle Loop, `v2/`.

## Steps
0. Re-run and record gates (native 24 expected, tooling 29/29, production build with/without secrets + sizes incl. the true internal-DRAM figure, Wokwi 15 checks); tree clean; no device contact.
1. Implement amendments → commit `Add boot-health rollback, crash breadcrumbs, and SC-only fix`; gates green; map check shows `verifyRollbackLater` from `main.cpp.o`.
2. Step A readback (Felipe's go) → decode → root-cause note appended to INCIDENT (resolution section) — may be skipped in favour of B's on-device summary if Felipe prefers no download mode.
3. Step B: instrumentation-only OTA (runbook = PLAN-r1 Step 4 + new expected BOOT/CONFIRMED/LAST_CRASH lines; WAIT/STOP rules unchanged).
4. Fix per decision table → commit → gates.
5. Step C: BLE candidate over cable + console; heap/stack report vs thresholds.
6. PLAN-r1 Steps 5–7 (physical acceptance, Stage 2 with camera click, docs: PORT_PLAN T16, PROJECT_KNOWLEDGE, READMEs incl. download-mode + console operations, AGENTS rules: cable smoke boot, never disable the OTA confirmation, true internal-DRAM accounting, stale counts).

## Concerns
- [HIGH] Root cause still `[UNVERIFIED]`; this plan is the evidence path. No hardware go requested until A/B/C produce it.
- [MEDIUM] Without the UART0 tap, crash text is only recoverable post-hoc (dump summary on the next hard boot); the 1 s console window is the mitigation. The tap removes the limitation entirely.
- [MEDIUM] Rollback arms only for OTA-delivered images; cable-flashed candidates rely on the attempt counter + deadline (`esp_ota_mark_app_invalid_rollback_and_reboot` on a VALID image — `[TEST]` header semantics, observed on hardware only if it ever fires).
- [MEDIUM] A reset during the 10 s health window (power-cycle, side button) rolls a good OTA image back — benign (device returns to the previous image; a second OTA re-delivers), documented.
- [LOW] `esp-coredump` vs the 4.4 dump format verified at the version-table level; the SHA check refuses stale dumps loudly.
- [LOW] Wired-preferred selects USB while the data cable is attached; the BLE bench is cable-free by construction.

## Questions for the author
1. Authorize (via Felipe) Step A as the first hardware touch, or go straight to Step B (cable-free, delivers the same summary if a dump exists)?
2. Accept scope amendments 1–4 and the SC-only flag removal?
3. Step B (instrumentation-only OTA) before Step C — yes, or single BLE build over the cable?
4. Health numbers (healthy = setup + 10 s loop; deadline 45 s; 3 attempts; 1 s boot console window)?
5. Hardware question for Felipe: 3.3 V USB-TTL adapter + HAT2-Bus access for the UART0 tap?
6. Confirm INCIDENT-3 is closed by the diff-base explanation.
7. Optional later: a deliberate rollback canary (OTA of an image that aborts in setup) to make the safety net `[REAL]` — Felipe's call.
