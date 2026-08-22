# AGENTS.md

## Mission and safety priority

Colibrino is an accessibility-focused head mouse. Head motion controls the
pointer and an intentional blink provides the primary click. Accidental pointer
reports, false clicks, stuck buttons, unsafe sensor power, or an upload to the
wrong device are product-safety failures, not cosmetic defects.

Favor fail-closed behavior. Sensor initialization failure, calibration failure,
invalid timing, invalid blink data, transport loss, or task failure must suppress
movement and release or avoid mouse buttons. Never weaken a physical arming gate
without explicit user approval and updated tests.

Read `PROJECT_KNOWLEDGE.md` before broad changes. It contains legacy incidents,
architecture, git history, hardware facts, and unresolved questions that do not
fit in source comments. Update it whenever verified status, architecture,
hardware assumptions, safety behavior, build commands, or acceptance gates
change.

## Current project state

Two independent firmware targets coexist.

The legacy target under `arduino project/Colibrino/` is the historical Arduino
Leonardo or ATmega32U4 Pro Micro implementation. It uses an external MPU6050 and
analog TCRT5000. Preserve it unless a request explicitly includes legacy work.
It has no reproducible manifest or automated test suite and depends on TimerOne.

The active successor under `sticks3/` targets the M5Stack StickS3. It uses the
internal BMI270, native USB composite CDC plus HID, M5Unified, and an
experimental onboard-IR feasibility probe. It is a clean successor rather than
a source-compatible port of the AVR sketch.

The StickS3 software is implemented, committed, native-tested, Wokwi-tested,
and physically exercised. Composite USB, BMI270 calibration, controls, pointer
movement, fail-closed HID, and authenticated OTA are verified on the explicitly
identified unit with MAC `AC:27:6E:D2:68:B8`. Its original 8 MB PTT image is
preserved under ignored `.device-backups/`. Never target the separate
`bedside-countdown-s3`; OTA must retain its hostname plus ARP-MAC guard.

The former evenly spaced four-blink IMU cadence is rejected. Five new worn runs
produced three still-control sequences and only two intentional sequences. The
current build instead requires double blink, 0.8-1.4 second pause, double blink
at a 1.1 dps residual entry threshold. Retained still/head captures replay with
zero events, but this new pattern remains hardware-unverified and click output
therefore remains gated off until a fresh current-boot probe passes.

The IMU blink-click channel is closed on evidence (2026-08-21): the click comes
from the macOS camera (Alternative Pointer Actions) or a future near-eye IR
proximity sensor; the BMI270 is the head pointer plus motion veto only.

BLE HID pointer (branch `agent/ble-hid-pointer`, 2026-08-22): NimBLE-Arduino
2.5.1 bonded, bounded transport behind one portable fail-closed
`MouseOutputPolicy` shared with USB (wired-preferred, never simultaneous);
hardware-validated cable-free (pairing, arming, disconnect/reconnect, Wi-Fi +
BLE coexistence within the telemetry baseline); pending the USB-cable topology
switch test, a 10-minute soak, and Stage 2 with the camera click. The 521bc26
boot loop was root-caused (`WiFi.setSleep(false)` with the BT controller
enabled aborts on IDF 4.4 coexistence) and the firmware now carries a permanent
OTA boot-health safety net with boot/crash breadcrumbs (`T16`, `T17`,
`docs/handovers/2026-08-21-ble-hid-pointer/INCIDENT-521bc26.md`).

`sticks3/PORT_PLAN.json` is the machine-readable source of truth for validated
facts, task status, simulation boundaries, and the TCRT5000 purchase decision.
Keep it syntactically valid and consistent with the prose documentation.

## Cross-repository Colibrino v2 workflow

Treat `/Users/fcavalcanti/dev/Colibrino` and
`/Users/fcavalcanti/dev/oracle-loop` as one coordinated program with separate
authority. Colibrino owns firmware, portable domain code, immutable hardware
traces, physical evidence, and release safety. Oracle Loop owns the executable
oracle map, specification, and assisted implementation loop. Its remote is
`https://github.com/fcavalcantirj/oracle-loop`.

Oracle Loop's branch `dasbrow/build-the-transform-prompt-parse-core-fo-20260817-005512`
(tip `3c67984`) carries the engine plus the ratified v2 oracle map (`b932e38`)
and the v2 SPEC (`3c67984` itself); `agent/colibrino-v2-luos-qualification`
(`84b5bea`) adds `docs/10` and its `AGENTS.md` and revises the map, SPEC, README,
and STATE; `agent/colibrino-v2-round-one-corrections` (`c7a82ac`, stacked on
`84b5bea`) holds the round-one corrections. None of these is on `main`. Read
`AGENTS.md`, `STATE.md`,
`docs/09-colibrino-v2-multimodal-accessibility.md`,
`docs/10-colibrino-v2-luos-qualification.md`,
`docs/colibrino-v2-ORACLE-MAP.md`, `docs/colibrino-v2-SPEC.md`, and
`engine/README.md` before v2 architecture or oracle work. Fetch both repositories
before planning and record the exact heads used. Never merge, rebase, squash, or
rewrite the Oracle stack without its exact user merge grant; fetch and push do
not grant merge authority.

Round one is head motion plus blink. Fresh StickS3 traces become immutable
fixtures in Colibrino, pure domain units are implemented against them, and only
then may Oracle Loop generate within its declared file authority. Feel tuning,
trace labels, tests, board glue, HID/BLE output, and physical acceptance remain
human-owned. Voice is a round-two producer and must not block round one.

The round-one units are `imu-motion` (contract-only this round; rate-based;
`sticks3` `MotionController` is the differential reference), `blink-dsp` (ends
at `IMPULSE`/`CANCEL` events, never at clicks), `blink-code` (PROPOSED in the
Oracle map: the temporal double-pause-double matcher that alone produces a
click candidate), `access-intent` (the only action authority), and `profile`.
The v2 host core lives on the branch-only `agent/v2-core-scaffold`
(`PORT_PLAN.json` T14 records its commit) under `v2/` (C11, CMake + vendored
Unity, run from `v2/`: `cmake --preset host && cmake --build build-host &&
ctest --test-dir build-host`; the Oracle gate is the `host-oracle` preset where
zero fixtures fail). It is not on master until real fixtures are promoted and
an integration branch is green under both presets. The loop may never touch `v2/test/**`, `v2/traces/**`,
`v2/third_party/**`, `v2/tools/**`, `sticks3/tools/**`, the feel constants in
`v2/core/include/colibrino/v2/feel_defaults.h`, or anything under `sticks3/`
firmware. Fresh traces are captured with the existing firmware and the
monitor-only tooling in `sticks3/tools/` following
`docs/V2_TRACE_CAPTURE_PROTOCOL.md`; no upload is needed for capture.

Read `docs/LUOS_ARCHITECTURE_DECISION.md` before introducing Luos. Use
Luos-compatible fixed-size service contracts from scratch, but keep DSP,
`AccessIntent`, and the synchronous release-all path independent of Luos. The
first Luos experiment is a localhost-only, diagnostic-only StickS3 spike with
one task owning the runtime and no Robus. Do not add Luos to production or put
HID authorization behind it until the documented concurrency, queue, latency,
watchdog, USB, OTA, and hardware gates pass.

## Multimodal accessibility direction

Read `docs/ACCESSIBILITY_WEARABLE_STUDY.md` before voice, Bluetooth, click-sensor,
Apple accessibility, or final-hardware work. The investigated product is a
multimodal intent router: head motion, blink, offline voice or consistent vocal
sound, dwell, and external adaptive switches feed one fail-closed policy layer.
No input producer may emit HID directly.

The StickS3 audio hardware is real and M5's Xiaozhi build proves its wake-word
path, but current Colibrino uses Arduino-ESP32 2.0.17, has no speech-model flash
partition, keeps Wi-Fi awake for OTA, and has no BLE HID. Begin speech work in
a separate environment. A partition-table change requires an authorized cable
flash and must preserve a viable recovery and OTA plan.

Voice commands require a wake phrase, short command window, confidence and
cooldown checks, and pointer freeze while listening. A bare command word must
never click. Timeout, uncertainty, audio-task failure, transport loss, or low
battery must do nothing and release held buttons. Voice is supplementary; keep
a blink, dwell, or switch fallback.

Do not equate the rejected onboard IR pair with a requirement to buy a bulky
TCRT5000. Compact candidates such as VCNL36828P and Class 1 VL53L4CD are research
leads only. Near-eye optical safety, mounting, and user accuracy require a
separate physical validation before connection or capability claims. Arduino
Nicla Voice is likewise a candidate, not a selected target; its BLE HID,
high-rate motion, personalized model, and battery behavior remain unproven.

## StickS3 architecture

`sticks3/src/main.cpp` owns setup, the cooperative loop, display pages, buttons,
physical arming, M5PM1 power, BMI270 acquisition, USB CDC diagnostics, and USB
HID reports. Board-specific behavior belongs here or in another explicit
adapter, never in portable analysis code.

`sticks3/src/ir_probe.*` drives GPIO46 and captures GPIO42 with RMT. It supports
the object-oriented Arduino-ESP32 2.x RMT API bundled by
`espressif32@6.12.0` and the pin-oriented 3.x API. Preserve both paths unless
the pinned platform changes deliberately.

`sticks3/include/colibrino/blink_input.h` defines the sensor-neutral blink sample
boundary. A future analog TCRT5000 adapter must implement this contract so that
USB, motion, and classification code remain unchanged.

`motion_controller.*` contains streaming stationary bias estimation, axis/sign
mapping, low-pass filtering, a smooth deadzone, fractional pixel accumulation,
invalid-timestep rejection, and bounded HID reports. It has no Arduino or
M5Stack dependency.

`signal_analysis.*` contains stable streaming statistics, open/closed separation
analysis, a polarity-independent hysteretic blink detector, human-duration and
refractory gates, and the guided feasibility state machine. It has no hardware
dependency.

`imu_blink_detector.*` contains the allocation-free double-pause-double timing
pattern, impulse duration gates, pointer-scale motion cancellation, and quiet
and click refractory periods. Do not reduce it to evenly spaced impulses; the
live control data disproves that design.

`mouse_output_policy.*` is the single output authority for every HID transport
(USB and BLE): boot locked, deliberate-hold arming, release-all + disarm on any
fault/topology change, bounded BLE cadence. `boot_health.*` is the portable OTA
confirm/rollback decision (attempt budget, healthy window, deadline); the IDF
calls live in `main.cpp`. `ble_mouse_transport.*` (board code) owns NimBLE:
callbacks publish atomic facts; the cooperative loop makes every safety
transition.

`sticks3/src/sim_main.cpp` is deterministic validation firmware, not production
application code. The Wokwi environment compiles it together with the actual
portable production sources and excludes `main.cpp` and `ir_probe.cpp`.

## Runtime and controls

The StickS3 boots in IR probe mode with external 5 V off. The internal speaker
and microphone remain disabled. BMI270 bias calibration starts immediately and
restarts whenever motion makes the sample standard deviation unsafe.

The physically verified large blue Button A owns the application workflow; the
side power/reset control is not an application input, and the USB CDC channel
is write-only (the firmware reads nothing from the host). The Wi-Fi telemetry
mirror on TCP port 35533 is equally output-only: the device reads and discards
every inbound byte, so the socket can never become a command channel. Worn
trace capture is cable-free by decision (a USB cable is a physical anchor that
distorts the measured blink impulses); the free-run capture stage
(CAPTURE_FREE_RUN) never sets the blink-validation gate and never counts
toward worn acceptance. Button A cycles IR probe,
motion monitor, and mouse mode only while mouse output is locked. In mouse mode with a
ready transport (USB data mount, or bonded + encrypted BLE), holding A for two
seconds after calibration toggles armed versus locked output; with no ready
transport the same hold opens a bounded BLE window (60 s public when bondless,
30 s whitelist-only when bonded) and a 5 s hold in a bonded reconnect window
forgets the bond and re-pairs. Pairing never arms. The device cannot leave
mouse mode while armed.

In IR mode, holding the large blue button for two seconds toggles the controlled
IR power rail. When IR is powered, tapping it starts or repeats the guided
open-eye, closed-eye, and blink sequence.

IMU blink-generated clicks require a passed still/pattern/head guided session,
mouse mode, and physically armed output in the current boot. The alternative
optical path requires valid powered IR samples and its own guided feasibility
pass. Do not persist either pass across boots without a separately approved
calibration-integrity design.

## Hardware safety invariants

Never upload unless the user explicitly identifies the device and authorizes
deployment. USB uploads require the resolved serial port and hardware identity;
OTA requires `sticks3-ptt.local` plus the saved MAC. Builds, tests, monitors,
and simulations do not authorize upload by themselves.

StickS3 IR power uses the M5PM1-controlled EXT_5V rail. Never call
`M5.Power.setExtOutput(true)` while another supply drives EXT_5V, Grove power,
or an attached accessory rail. Keep the rail off at boot, require a deliberate
two-second physical hold, and turn it off immediately when IR initialization
fails.

M5Stack specifies that the speaker amplifier interferes with onboard IR
reception. Keep internal speaker initialization disabled and preserve the
explicit M5PM1 amplifier-disable operation during startup.

Wi-Fi + Bluetooth coexistence on this core (Arduino-ESP32 2.0.17 = IDF 4.4.7):
never request `WIFI_PS_NONE` (`WiFi.setSleep(false)`) while the BT controller
is or will be enabled — the Wi-Fi task aborts (`pm_set_sleep_type`), which was
the 521bc26 boot loop. The BLE build keeps `WIFI_PS_MIN_MODEM`; throughput
under that setting is a measured number, never assumed.

OTA boot-health is a standing requirement for every firmware image: keep the
`extern "C" bool verifyRollbackLater()` override (C linkage is mandatory),
the health-gated confirmation, the deadline and attempt budget, the boot and
crash breadcrumbs, and the `scripts/check_image.py` post-link gate; never
disable them to make a build pass. Any build that changes board-level init
(new RF stack or tasks, USB mode, partition table, core or library upgrade)
gets a cable smoke boot with the serial console before its first OTA. Read the
flash core dump (`esptool read_flash 0x7F0000 0x10000`, `esp-coredump` against
the retained ELF) before guessing at a boot loop; a cable recovery that rewrites
only the app slots leaves it intact.

StickS3 side button semantics (official manual): long press = download mode,
single click = power on / reset, double click = power off; an esptool session
leaves the chip in the latched download mode until a single click. The USB-C
console exists only from a hard reset until `USB.begin()`; panic text needs the
UART0 tap on the HAT2-Bus (pin 10 = G43 TX, pin 12 = G44 RX, pin 1 = GND).

USB mouse output must default to locked. Preserve bounded signed-byte reports,
stationary calibration, measured BMI270 timestamps, invalid gaps producing no
movement, and `motion.reset()` when the arming state changes.

Gyro axes and signs are mounting-specific. Confirm all three raw axes using the
motion monitor on the final glasses mount. Do not copy the legacy AVR axis
permutation or treat current defaults as physically validated.

## Onboard IR evidence standard

The onboard receiver is a demodulating digital remote-control receiver, not an
analog reflectance sensor. RMT compilation, received pulses, a Wokwi pass, or a
single display `PASS` does not prove reliable eyelid detection.

The firmware's minimum signal gate requires at least 20 valid frames in each
open and closed baseline, at least four pooled standard deviations of
separation, at least 15 percent relative change, and at least two plausible
40-650 millisecond blink transitions.

The product decision is stricter. At the intended glasses-to-eye geometry,
perform two runs under different ambient lighting. Attempt five deliberate
blinks in each run and retain the complete CDC CSV. Rely on onboard IR only if
each run detects at least four of five and a 30-second open-eye control produces
no false click.

Reposition and repeat when fewer than 80 percent of frames are valid or the
geometry moved during capture. Add an external analog TCRT5000 only when
separation remains below the gate, recall remains below four of five after
positioning trials, or false clicks occur.

## Build and validation commands

Run from `sticks3/`:

```sh
platformio test -e native
platformio run -e m5stack-sticks3            # runs scripts/check_image.py post-link
platformio run -e m5stack-sticks3-noble      # BLE bring-up compiled out, id suffix -noble
./scripts/run_wokwi.sh
python3 -m json.tool PORT_PLAN.json >/dev/null
```

On this workstation PlatformIO may be available only at:

```sh
/Users/fcavalcanti/.platformio/penv/bin/platformio
```

The native suite currently contains 33 Unity cases (13 below plus 11 in
`test_mouse_output_policy` and 9 in `test_boot_health`); the authoritative
list is the `RUN_TEST` names in `sticks3/test/*/test_main.cpp`: motion
`calibration_accepts_stationary_samples`, `calibration_rejects_motion`,
`pointer_deadzone_suppresses_bias_and_noise`,
`pointer_accumulates_fractional_motion_and_respects_sign`; optical
`separation_rejects_identical_signals`,
`separation_accepts_stable_open_closed_difference`,
`blink_detector_requires_human_blink_duration`,
`guided_protocol_requires_signal_and_two_blinks`; IMU `stillness_never_emits`,
`double_pause_double_pattern_emits_once`, `uniform_four_blinks_do_not_emit`,
`three_pattern_impulses_do_not_emit`, `head_motion_cancels_partial_sequence`.
Refractory rejection is asserted only by the Wokwi gate.

The no-upload production build currently uses `espressif32@6.12.0`,
Arduino-ESP32 2.0.17, M5Unified 0.2.19, M5GFX 0.2.26, and NimBLE-Arduino 2.5.1.
The last hardware-validated BLE image (`ff1f7d2`, 2026-08-22) used 104,788 B
of reported RAM (32.0 %), 1,255,389 B of application flash (37.6 %), and
176,720 B of true internal-DRAM static footprint (`.dram0.dummy` + data + bss;
the gate ceiling is 200 KB); free internal heap on hardware ≈ 145 KB with BLE
idle and ≈ 112–137 KB while armed and streaming. Treat sizes as observations,
not permanent acceptance thresholds.

The Wokwi wrapper loads `WOKWI_CLI_TOKEN` from the process or the ignored
repository-root `.env`. `PLATFORMIO_CLI_BIN` and `WOKWI_CLI_BIN` override CLI
locations. Never print, stage, or commit the token. Success requires
`COLIBRINO_SIM_PASS`; the current gate emits 18 passing checks. Neither Wokwi
nor Espressif QEMU implements Bluetooth, so no simulator can reach BLE
bring-up; the cable smoke boot and the boot-health safety net are the gates. Generated
`.pio/` contents remain ignored.

## Simulator truth

No public tool currently provides a complete StickS3 simulator.

Wokwi models a generic ESP32-S3 and runs the portable production logic. It does
not model the complete BMI270, M5PM1 power path, onboard IR optics, display,
buttons, or exact composite TinyUSB behavior.

M5Stack's official `lv_m5_emulator` is a native SDL/M5GFX/LVGL UI tool. Its
current target list includes Core, Core2, CoreS3, StickC Plus, StickC Plus2,
Dial, and Tab5, but not StickS3. It does not emulate the ESP32-S3 CPU or board
peripherals.

UiFlow2's UI simulator is a visual editor. StickS3 `Run Once` requires a
connected physical device. Espressif QEMU emulates the ESP32-S3 CPU, memory, and
selected SoC peripherals, but not the complete StickS3 external devices or
eyelid-reflection path.

Use simulation for portable logic and UI work only. The fresh board remains the
authority for hardware integration and the TCRT5000 decision.

## Coding rules

Keep portable headers self-contained and usable under C++17 native builds. Do
not introduce Arduino, M5Unified, USB, GPIO, RMT, or FreeRTOS dependencies into
`motion_controller.*`, `signal_analysis.*`, or the `BlinkInput` contract.

Use monotonic unsigned timestamps and preserve wraparound-safe subtraction.
Motion `dt` is measured from BMI270 sample timestamps; gaps over 200
milliseconds intentionally return zero movement.

Keep streaming algorithms allocation-free. The current statistics use Welford's
method and the controller retains fractional pixel residuals. If either
algorithm changes, add focused native edge cases and update the Wokwi harness
when the behavior is safety-relevant.

Comments should explain intent, units, safety gates, state transitions, and
non-obvious API compatibility. Do not narrate obvious syntax. Label assumptions
and unverified hardware behavior explicitly.

Do not turn the experimental onboard result into a permanent capability claim
until the physical decision gate passes. Names such as `NOT_PROVEN`,
`feasibility`, and `probe` are deliberate.

## Legacy constraints

The AVR code has known hazards recorded in `PROJECT_KNOWLEDGE.md`, including
case-sensitive include problems, fixed-rate Mahony assumptions, ISR/main data
races, weak EEPROM integrity, direct HID side effects, possible held-button
behavior, and unclear blink-threshold configuration. Do not opportunistically
repair or modernize it while working on StickS3.

If legacy work is requested, first establish an Arduino AVR plus TimerOne build
baseline and test on a non-critical host session. Preserve EEPROM compatibility
or version any replacement layout explicitly.

## Documentation discipline

The root `README.md` is the project landing page and must keep both targets
discoverable. `sticks3/README.md` is the operating and validation guide.
`PORT_PLAN.json` is the machine-readable status. `PROJECT_KNOWLEDGE.md` is the
durable deep context. `docs/ACCESSIBILITY_WEARABLE_STUDY.md` is the product and
hardware research record. Update all affected layers when facts change; do not
leave contradictory claims.

Do not leave substantial research, hardware evidence, rejected alternatives,
or architectural decisions only in chat context. When the user has authorized
repository publishing, write those findings into the appropriate durable files,
validate them, and commit and push before handoff. Clearly separate sourced
facts, assumptions, proposals, implemented behavior, and physical validation.

Before handing off a change, run relevant tests and builds, validate JSON and
shell syntax, inspect the final diff, confirm `.env` and `.pio/` remain ignored,
and report every hardware-dependent claim separately from software evidence.

## Git and release discipline

Preserve unrelated user changes and stage explicit paths. Do not commit
credentials, generated firmware, local PlatformIO state, device-specific serial
logs, or editor files.

Use terse commits that describe the delivered outcome. Before merging, refresh
remote refs and require the topic branch to contain the remote default branch.
Prefer a fast-forward merge when history is linear. Never force-push or rewrite
published history unless the user explicitly requests it.

Repository invariant: master's tip is never RED. Every commit that becomes
master's tip is green under the native suite and, once `v2/` exists on master,
under the `v2` `host` and `host-oracle` presets. Fixture-less v2 core work
therefore stays branch-only (`agent/v2-core-scaffold`) and lands on master only
through an integration branch whose tip has been verified green under both
presets after real fixtures are promoted; that merge is an ordinary merge, and a
squash requires the user's explicit approval.
