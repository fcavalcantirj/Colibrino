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
and compiled without upload. Physical StickS3 behavior remains unverified. The
device currently in service is protected and must not be flashed. Wait for the
fresh unit and explicit target-port identification before uploading.

`sticks3/PORT_PLAN.json` is the machine-readable source of truth for validated
facts, task status, simulation boundaries, and the TCRT5000 purchase decision.
Keep it syntactically valid and consistent with the prose documentation.

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

`sticks3/src/sim_main.cpp` is deterministic validation firmware, not production
application code. The Wokwi environment compiles it together with the actual
portable production sources and excludes `main.cpp` and `ir_probe.cpp`.

## Runtime and controls

The StickS3 boots in IR probe mode with external 5 V off. The internal speaker
and microphone remain disabled. BMI270 bias calibration starts immediately and
restarts whenever motion makes the sample standard deviation unsafe.

The physically verified large blue Button A owns the application workflow; the
side power/reset control is not an application input. Button A cycles IR probe,
motion monitor, and mouse mode only while mouse output is locked. In mouse mode,
holding A for two seconds after calibration toggles armed versus locked output.
The device cannot leave mouse mode while armed.

In IR mode, holding the large blue button for two seconds toggles the controlled
IR power rail. When IR is powered, tapping it starts or repeats the guided
open-eye, closed-eye, and blink sequence.

Blink-generated clicks require all of the following in the current boot: valid
IR samples, a passed guided feasibility session, mouse mode, and physically
armed output. Do not persist this pass across boots without a separately
approved calibration-integrity design.

## Hardware safety invariants

Never upload unless the user explicitly identifies the fresh device and serial
port. Builds, tests, monitors, and simulations do not authorize upload.

StickS3 IR power uses the M5PM1-controlled EXT_5V rail. Never call
`M5.Power.setExtOutput(true)` while another supply drives EXT_5V, Grove power,
or an attached accessory rail. Keep the rail off at boot, require a deliberate
two-second physical hold, and turn it off immediately when IR initialization
fails.

M5Stack specifies that the speaker amplifier interferes with onboard IR
reception. Keep internal speaker initialization disabled and preserve the
explicit M5PM1 amplifier-disable operation during startup.

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
platformio run -e m5stack-sticks3
./scripts/run_wokwi.sh
python3 -m json.tool PORT_PLAN.json >/dev/null
```

On this workstation PlatformIO may be available only at:

```sh
/Users/fcavalcanti/.platformio/penv/bin/platformio
```

The native suite currently contains eight Unity cases covering stationary
calibration, motion rejection, pointer deadzone and accumulation, signal
separation acceptance/rejection, blink duration, and the guided protocol.

The no-upload production build currently uses `espressif32@6.12.0`,
Arduino-ESP32 2.0.17, M5Unified 0.2.19, and M5GFX 0.2.26. The last verified image
used 35,064 bytes of reported RAM and 575,733 bytes of the application flash
partition. Treat sizes as observations, not permanent acceptance thresholds.

The Wokwi wrapper loads `WOKWI_CLI_TOKEN` from the process or the ignored
repository-root `.env`. `PLATFORMIO_CLI_BIN` and `WOKWI_CLI_BIN` override CLI
locations. Never print, stage, or commit the token. Success requires
`COLIBRINO_SIM_PASS`; generated `.pio/` contents remain ignored.

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
durable deep context. Update all affected layers when facts change; do not leave
contradictory claims.

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
