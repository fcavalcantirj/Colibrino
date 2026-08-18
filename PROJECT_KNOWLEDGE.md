# Project Knowledge

# Changelog

## 2026-08-18T01:03:29Z — HEAD 1a5fa9e

Decided with the user that worn trace capture is cable-free: a USB cable is a physical anchor on the roughly 20 g head mount and distorts the very blink impulses being measured, so no capture session will ever use a cable (not even a bench comparison). Implemented on `agent/v2-wireless-capture`: a `TelemetryMux` diagnostics fan-out (byte-identical USB CDC output plus a read-only TCP mirror on port 35533; one client; inbound bytes read and discarded so the socket can never become a command channel; fixed 32 KB ring drained with bounded non-blocking sends because `WiFiClient::write` in Arduino-ESP32 2.0.17 can stall the loop task about ten seconds; oldest whole lines dropped on overflow with an explicit `EVENT,TELEMETRY,DROPPED` counter, so wireless transport loss is visible where USB CDC dropped silently), the `CAPTURE_FREE_RUN` stage (second Button A tap during `PREPARE_STILL`; stop by tap or a 90 second cap; streams ordinary IMU rows for 30 second rest baselines and long confounders; never sets `imu_blink_validated`; reports `RESULT,IMU_BLINK,NOT_PROVEN,...,free=N`), and a compile-time git build id (boot `EVENT,BUILD`, `STATUS` gains trailing `build=`, `batt=`, `tele=` fields; the host STATUS parser tail was already free-form). Wireless readings are as trustworthy as USB because every IMU row is timestamped on the device before transport and label refinement works on signal peaks in device time; the design was stress-tested against the installed framework sources (real TCP MSS 1436; the host's immediate `shutdown(SHUT_WR)` makes `recv()==0` the expected half-close, not a disconnect; the INADDR_ANY listener survives STA reconnects; `ArduinoOTA.handle()` flashes synchronously, so telemetry idles during an update). Production builds succeed with the secrets header (RAM 98,236 bytes, flash 1,063,349 bytes; the 33 KB RAM delta is the ring plus socket state) and without it (pure CDC pass-through). Native tests remain 13/13; Wokwi is unaffected (portable logic untouched). Hardware gates remain open until the user authorizes the fourth OTA: post-reboot verification over TCP, then the no-cable equivalence acceptance recorded in `docs/V2_TRACE_CAPTURE_PROTOCOL.md` section 5b.

## 2026-08-17T23:22:38Z — HEAD 4e4d1f1

Recorded the branch-only v2 pure-core scaffold `agent/v2-core-scaffold` (`00c1285`, stacked on master `4e4d1f1`): a self-contained C11 CMake project under `v2/` with vendored Unity 2.6.1, fixed-size event contracts (16-byte static-asserted header, bounded TTL horizon, wraparound-safe comparisons, explicit little-endian codec), the fail-closed `access-intent` arbiter (every fault yields no action plus release-all; negative, positive, property, and rollover suites), `profile` validation (CRC32 as corruption detection only), `blink-dsp` (impulse events only) and `blink-code` (the double-pause-double click candidate) composed by a pipeline that reproduces `sticks3` `ImuBlinkDetector` sample-for-sample on synthetic patterns, a boundary sweep, and PRNG fuzz across the timestamp wrap, plus golden tests that read committed `NAME.csv` + derived `NAME.labels.tsv` and skip (developer preset) or fail (oracle preset) while `v2/traces/` holds no promoted fixture. `imu-motion` is contract-only. Host preset: 16 ctest tests pass with the two golden tests skipped; host-oracle preset: exactly those two fail; ASan preset clean; a `labels_schema_consistency` test pins the enums and TSV header shared with `sticks3/tools/make_trace_fixture.py`. The scaffold reaches master only through a green integration branch after real fixtures are promoted; master's tip is never RED.

## 2026-08-17T22:01:55Z — HEAD dee4bc6

Re-synchronized both repositories at Colibrino `dee4bc6` and Oracle Loop `3cf1c30` (engine stack top `3c67984`, Luos qualification `84b5bea`), then validated the round-one plan against the actual firmware. Fourteen documentation contradictions were corrected: the StickS3 CDC is write-only (no host command channel exists; Button A is the sole input); the retained captures run at a nominal 200 Hz poll cadence with bursty dropouts (about 162-170 Hz effective, per-row `usec` is the timing ground truth); the IMU classifier consumes the raw gyro vector and derives its own EMA baseline; the ratified oracle map and v2 SPEC are commits on the engine branch itself, with `84b5bea` adding only `docs/10` and the contributor guide; the round-one unit set is `imu-motion` (contract-only this round), `blink-dsp` (ends at impulse events), `blink-code` (PROPOSED, the coded double-pause-double click candidate), `access-intent`, and `profile`; the negative `AccessIntent` set is the full ten-item list; the capture segment set gains coded double-pause-double groups and an evenly spaced four-blink negative; and the existing 6/15/12 s guided capture can only produce the round-one fixtures through a multi-run matrix with host-side labels (no upload). The replay tool now reports every probe session separately with `controls_clean` and `validation_passes` summaries; replaying the three retained logs under the current coded detector gives `controls_clean=yes` in every session (still and head stages zero everywhere; one deliberate-stage sequence in session 4 of the five-session log). Oracle Loop received the mirrored corrections on the new branch-only `agent/colibrino-v2-round-one-corrections` (commit `c7a82ac`, restoring the owner-ratified three-rule safety list verbatim, extending the template regime enum, and untracking `.coverage`); its engine measured 44 tests and 100% statement coverage (199/199) at `84b5bea`. Monitor-only capture tooling, the physical trace-capture protocol, and the branch-only `v2/` pure-core scaffold are recorded in the following entries and in `PORT_PLAN.json` T12-T14. No firmware source changed and nothing was uploaded.

## 2026-08-17T19:22:03Z — HEAD 8bfd7f7

Fetched and fast-forward checked both Colibrino and the sibling Oracle Loop repository, analyzed the unmerged Colibrino v2 oracle and engine stack at `3c67984`, and independently qualified Luos Engine 3.1.0. Luos passed 121 upstream native cases and compiled for ESP32-S3 with the pinned Colibrino framework, but upstream ESP32 CI, watchdog, HAL concurrency, persistence, and Robus warnings prevent unconditional production adoption. Recorded the resulting cross-repository authority model, from-scratch pure service boundaries, localhost-only Luos spike, internal bounded-bus fallback, and rule that HID authorization and synchronous release-all remain outside Luos. Corrected and pushed the Oracle documents on the new unmerged branch `agent/colibrino-v2-luos-qualification` commit `84b5bea` without changing `main` or the prior stack.

## 2026-08-17T01:58:32Z — HEAD 8abfa03

Expanded the product investigation from a StickS3 blink head mouse to a multimodal accessibility wearable. Verified the StickS3 wake-word hardware and ESP-SR path, recorded the current Arduino and flash-partition obstacles, mapped Apple Voice Control, Vocal Shortcuts, Switch Control, Dwell, and Live Speech integration, compared compact near-eye sensors and the Nicla Voice low-power alternative, and defined a fail-closed intent architecture plus staged physical acceptance plan. This is research evidence only; no voice, BLE HID, new optical sensor, or Nicla capability has been implemented or physically validated.

## 2026-08-15T18:45:17Z — HEAD 7b3b55b

Five additional worn guided sessions invalidated the evenly spaced four-blink IMU cadence: it recognized one intentional sequence in two runs but also produced a still-control sequence in three runs, while all head-motion controls remained clear. A fixed-threshold search could eliminate those controls only by losing intended runs, so the detector now requires double blink, a 0.8-1.4 second pause, and double blink at a 1.1 dps entry threshold. Thirteen native tests, eleven Wokwi checks, the production build, and replay of all retained controls pass; the coded build was committed, pushed, and installed in a third authenticated OTA upload, but its fresh worn validation and post-reboot CDC confirmation remain pending.

## 2026-08-15T18:07:48Z — HEAD 298c7c0

Bootstrapped the OTA-enabled Colibrino image over the explicitly matched USB ROM port for MAC `AC:27:6E:D2:68:B8`, then completed two authenticated OTA round trips through `sticks3-ptt.local`. After each reboot the same MAC returned and CDC confirmed HID locked, external IR off, BMI270 calibrated, blink clicking disabled, and OTA ready; the separate `bedside-countdown-s3` remained untouched. Updated the operating guides and structured plan from pending bootstrap to hardware-validated OTA.

## 2026-08-15T16:20:19Z — HEAD 59ffaab

Distinguished the tested Colibrino StickS3 from the separate live `bedside-countdown-s3`, recovered the tested unit's original `sticks3-ptt` authenticated ArduinoOTA workflow, and carried that capability into Colibrino behind ignored credentials. Added fail-closed OTA callbacks, a hostname plus saved-MAC upload guard, documentation and structured acceptance criteria, then passed all twelve native tests and the OTA-enabled production build while confirming the local simulator artifact contains no Wi-Fi or OTA credential strings. The installed older Colibrino image still needs one cable bootstrap before later updates can use OTA.

## 2026-08-15T03:12:46Z — HEAD 4c640d2

Physically validated StickS3 native USB CDC plus HID, BMI270 calibration, safe pointer arming, stationary suppression, and worn-head cursor motion. Recorded that the onboard demodulating IR pair is unsuitable for near-eye reflectance, added a conservative current-boot four-blink IMU classifier and guided validation workflow, replayed two nominal-200 Hz physical captures without still or head-motion false sequences, expanded native and Wokwi coverage, and deferred any TCRT5000 purchase until one repeat mounted test proves or rejects the IMU click path.

## 2026-08-14T23:30:44Z — HEAD 8d91076

Fast-forwarded the fully validated `agent/sticks3-port` history into local and remote `master`. Both branches now contain the guarded StickS3 firmware, Wokwi gate, complete README set, source annotations, agent instructions, and hardware acceptance plan; the working tree is clean apart from ignored local credentials and PlatformIO output.

## 2026-08-14T23:26:51Z — HEAD 9817312

Reworked the root and StickS3 READMEs into complete project and operating guides, annotated every StickS3 interface and implementation boundary, expanded `AGENTS.md` with the full safety, architecture, simulator, test, and hardware-decision context, and recorded the current emulator inventory. Public M5Stack, UiFlow2, Espressif QEMU, and Wokwi evidence confirms that no complete StickS3 emulator exists; the fresh device remains mandatory for onboard-IR feasibility and the TCRT5000 decision.

## 2026-08-14T23:14:25Z — HEAD 4790d38

Added and executed a reproducible Wokwi CLI gate for the committed StickS3 branch. A generic ESP32-S3 image now runs the production portable motion and blink-analysis sources, eight deterministic checks pass in Wokwi, the local simulator token is kept in an ignored repository `.env`, and documentation now distinguishes simulated logic from the BMI270, M5PM1, IR optics, display, button, and TinyUSB behavior that still requires the fresh StickS3.

## 2026-08-14T22:21:27Z — HEAD 870cf23

Added the uncommitted `sticks3/` prototype implemented in this session: its pinned build, guarded native USB HID, BMI270 motion path, sensor-neutral blink boundary, RMT onboard-IR experiment, guided feasibility gates, host tests, safety defaults, successful no-upload build, and remaining fresh-device validation work. Corrected the earlier USB-mode and onboard-IR assumptions using the exact bundled framework APIs and M5Stack controller documentation.

## 2026-08-14T21:39:57Z — HEAD 870cf23

Initial capture at the current repository tip. It records the AVR firmware architecture and execution flow, hardware and persistent-state behavior, build dependencies, repository and tracker history, static-analysis risks, and the feasibility boundary for a future StickS3 or ESP32 port raised during this session.

# Analyzed revision

### Snapshot

The last fully analyzed source commit is `dee4bc64d3da126462df0ae5d54f876ca7e0c4f3`. It contains the physically exercised StickS3 firmware, coded IMU blink classifier, hardware-validated authenticated OTA, MAC-guarded uploader, per-session capture replay tool, native and Wokwi validation, hardware findings, multimodal wearable research, and operating documentation. This update also analyzes the unmerged Oracle Loop branch set: `main` `3cf1c3012548912adebc22a90b2d03dce3e396cd`; the `dasbrow/build-the-transform-prompt-parse-core-fo-20260817-005512` branch (tip `3c6798413e5514419579e7af23a31f06f603f20f`, which carries the engine, the ratified v2 oracle map `b932e38`, and the v2 SPEC commit `3c67984` itself); `agent/colibrino-v2-luos-qualification` `84b5bea` (adds `docs/10` and `AGENTS.md`, revises the map, SPEC, README, and STATE); and `agent/colibrino-v2-round-one-corrections` `c7a82ac06ebfd72016082321588daa97d30c2c1b` (round-one corrections, stacked on `84b5bea`); plus Luos Engine upstream commit `f1af47bdd760ce7038fbb396d1d203c8c2723464`. It intentionally excludes ignored device backups, physical capture logs, credentials, temporary build output, and PlatformIO state.

### Repository lineage

The configured origin is the public fork `fcavalcantirj/Colibrino`. GitHub identifies `tix-life/Colibrino` as its parent and source repository. The upstream description is "Dispositivo para controlar o computador apenas com movimentos da cabeça. Voltado para pessoas com deficiências motoras", and its listed homepage is `https://colibrino.com.br`.

### Activity and maturity

Before the successor work in this session, the latest upstream firmware-affecting commit was from January 2023 and the latest upstream repository commit was a README update from 2025-01-29. The upstream repository was not archived as of this capture, but it had no current CI, release tags, open pull requests, or recent firmware work. Treat the AVR implementation as an old hardware prototype. The new StickS3 path resumed local development in August 2026; USB, IMU, motion, controls, and safety interlocks now have physical evidence, while wearable blink-click validation remains incomplete.

# Runtime

### Product

Colibrino is standalone embedded firmware for a do-it-yourself head mouse. It converts head orientation into cursor movement and scroll input, and uses a strong blink detected by an infrared reflectance circuit for mouse-button input.

### Intended users

The README explicitly targets people with physical and motor disabilities including tetraplegia, arthrogryposis, amputations, and cerebral palsy. Accessibility and prevention of unintended pointer or button events are therefore primary safety and usability constraints.

### Deployment model

The active target is an Arduino Leonardo or ATmega32U4 Pro Micro connected to a computer over USB. The microcontroller runs continuously as a USB HID mouse; there is no server, desktop application, worker, cron job, or cloud component.

A committed successor prototype exists under `sticks3/` for the M5Stack StickS3. It is a standalone wired USB HID mouse with a write-only composite USB CDC diagnostics channel (STATUS, EVENT, IR, IMU, and RESULT records; there is no host-to-device command path), plus optional authenticated Wi-Fi OTA when an ignored device header is present. Physical testing confirmed USB enumeration, BMI270 calibration, fail-closed arming, stationary suppression, head-controlled cursor motion, and authenticated OTA. It starts with movement locked and requires the large blue Button A to advance guided tests or arm the pointer. Blink clicks remain disabled unless the current boot passes its guided IMU validation. The tested device now holds the double-pause-double and OTA build after a third authenticated upload, advertises `sticks3-ptt.local` while awake, and may operate from battery; USB remains useful for raw CDC capture but is not required for OTA or battery operation.

The user firmware present before Colibrino testing was captured as an ignored 8 MB flash image at `sticks3/.device-backups/sticks3-ac276ed268b8-pre-colibrino-20260815T014627Z.bin`. Its SHA-256 is `712cd6797ff0b77bfda8674b0aaaee93bc187cc64000fc8c0135374f8e031f65`. Preserve this backup and restore it after the remaining hardware experiment when the user requests their working project back.

### Physical assembly

The documented assembly uses an MPU-6050 accelerometer and gyroscope attached to eyeglasses, a TCRT5000 infrared reflectance sensor placed near the eye, an Arduino-compatible board, resistors, indicator components, a buzzer, wiring, and a USB cable. Three STL files provide enclosure parts, and `doc/` contains assembly images.

### Language and documentation

Firmware is Arduino C and C++. The root landing page is bilingual, the detailed StickS3 guide and new source annotations are English, and the legacy user documentation and most legacy comments are Portuguese. Some comments in `blink.cpp` contain replacement characters from prior encoding damage.

### License

The repository includes the GNU GPL version 3 license text. The README and primary sketch headers permit GPL version 3 or, at the recipient's option, a later version.

# Core structure

### Main sketch

`arduino project/Colibrino/Colibrino.ino` owns `setup()` and `loop()`. It initializes I2C and the blink subsystem, gates motion processing behind warm-up and gyroscope calibration, invokes the orientation filter, derives mouse reports, and sends USB HID events.

### Raw MPU-6050 access

`MPU6050.cpp` and `MPU6050.h` configure MPU-6050 registers and synchronously read the 14-byte accelerometer, temperature, and gyroscope register block over I2C. The code uses a fixed device address of `0x68`, a plus-or-minus 2 g accelerometer range, a plus-or-minus 250 degree-per-second gyro range, and DLPF setting `0x05`.

### Orientation estimation

`MahonyAHRS.cpp` contains a vendored Mahony attitude filter and quaternion-to-yaw, pitch, and roll conversion. Its integration frequency is compile-time fixed at 1000 Hz, proportional gain is 0.5, and integral gain is disabled.

### Motion mapping

`mouseIMU.cpp` converts raw MPU data to g and radians per second, calibrates and persists gyro offsets, unwraps Euler angles, maps yaw and pitch deltas to signed mouse motion, derives roll-based scrolling, and contains dormant gesture recognition.

### Blink subsystem

`blink.cpp` pulses the TCRT5000 emitter, samples reflected infrared with and without illumination, maintains moving windows, detects changes with a state machine, and directly presses or releases the left mouse button. It also contains disabled dwell-click and older blink-calibration paths.

### Calibration reset sketch

`LimparCalibracao/limparCalibracao/limparCalibracao.ino` is a separate utility sketch. Its `setup()` writes zero to EEPROM addresses 0 through 12 so that the main sketch performs gyro calibration again; its `loop()` is empty.

### Bundled support code

`I2Cdev.cpp`, `I2Cdev.h`, `helper_3dmath.h`, the three `MPU6050_*MotionApps*.h` files, `keywords.txt`, and `library.json` are vendored I2Cdevlib artifacts. The active firmware uses its own small MPU register driver and does not invoke the bundled DMP headers.

### Repository assets

The non-code assets are two documentation images, three enclosure STL files, the README, and the license. There are no schematics in an editable electronics-CAD format, board definition files, test fixtures, or generated firmware binaries.

### StickS3 successor tree

`sticks3/src/main.cpp` is the physical-device application. It initializes M5Unified, composite TinyUSB CDC plus HID, optional Wi-Fi plus ArduinoOTA, power, BMI270 motion, display state, buttons, optical diagnostics, guided IMU capture, and fail-closed HID output. `motion_controller.cpp` contains portable calibration and pointer mapping. `signal_analysis.cpp` and `ir_blink_input.cpp` implement the retained optical experiment. `imu_blink_detector.cpp` implements the conservative double-pause-double classifier. Headers under `sticks3/include/colibrino/` define the portable boundaries.

`sticks3/src/sim_main.cpp` is a generic ESP32-S3 Wokwi gate rather than a second product entry point. Native Unity tests live under `sticks3/test/`. `sticks3/tools/replay_imu_capture.cpp` replays physical CDC logs through the exact production detector. `sticks3/scripts/run_wokwi.sh` builds and launches the simulator. `sticks3/scripts/upload_ota.sh` builds locally, resolves only the configured Stick hostname, verifies the resolved ARP MAC, and invokes Espressif's authenticated updater. `PORT_PLAN.json` is the structured evidence and decision record.

# Execution flow

### Startup order

`setup()` starts `Wire`, sets I2C to 100 kHz, waits 100 ms, wakes and configures the MPU-6050, waits another 100 ms inside `eyeBlinkSetup()`, starts USB serial at 115200, configures blink-related GPIO, and starts Timer1 callbacks. The source comment claiming that `Wire.setClock(100000)` is 400 kHz is incorrect; the configured value is 100 kHz.

### Blink sampling interrupt

Timer1 fires every 200 microseconds and cycles through five phases. Phase 1 raises the infrared emitter and advances a millisecond counter, phase 2 reads the illuminated analog value then turns the emitter off, phase 3 reads the ambient value, and phases 4 and 5 are idle. A main-loop flag causes the latest completed pair to be processed approximately once per available loop iteration.

### Compensated optical signal

The active optical sample is `abs(sensorValueOff - sensorValueOn)`. This removes most ambient-light contribution and corresponds to the 2021 incident fix for false triggering when an uncorrected signal became negative near zero.

### Blink filtering

The code keeps 50 raw optical readings, computes their average for every processed sample, and keeps 50 such averages. `CalculaDerivada()` compares the newest average with the circular-buffer slot that will be overwritten next, effectively measuring change across roughly 50 processed samples after the buffer is full.

### Blink state machine

After 100 processed initialization cycles, `MaquinaBordas()` waits for at least 20 ms of rest, stores a baseline, watches derivative thresholds of greater than 4 or less than minus 3.4, and requires the current mean to exceed the baseline threshold before calling `Mouse.press()`. It calls `Mouse.release()` when the mean falls below baseline plus 70 percent of that threshold.

### Warm-up gate

Every main-loop iteration runs blink refresh, reads the MPU, and converts raw units. Cursor orientation processing is skipped for the first 3000 loop iterations. This gate counts iterations, not elapsed time, and blink-driven button events are already active during it.

### Gyroscope calibration

After the warm-up gate, `IMU_calibration()` reads EEPROM address 0. A marker byte equal to 1 means that 12 subsequent bytes hold three raw `int32_t` offsets. Otherwise it averages 100 gyro samples while stationary, stores the three offsets and marker, and permits orientation processing.

### Orientation update

With calibration available, the main loop calls the IMU-only Mahony filter with axes deliberately permuted as gyro Y, Z, X and accelerometer Y, Z, X. It then calculates Euler yaw, pitch, and roll. This axis mapping reflects the documented vertical mounting orientation of the sensor board.

### Cursor reports

Horizontal and vertical functions sample their corrected angle every fifth call, multiply the angle delta by sensitivity 30, and return `int8_t` values. The most recently computed displacement is returned on the four intervening calls as well. `Mouse.move(xchg, ychg, scroll)` is called on every calibrated loop.

### Scroll reports

`scrollDetector()` tracks a very slow roll baseline beginning at 90 degrees. A roll deviation between 4 and 10 degrees produces one wheel step at most every 200 ms; larger deviations are ignored and can reset the baseline after 5 seconds.

### Active click path

The active blink state machine calls `Mouse.press()` at the detected start and `Mouse.release()` at the detected end, allowing a sustained blink to become a held left button. `g_novaPiscada` is cleared inside every active refresh and is only set by the unused `SalvarPiscada()` path, so the main sketch's later `Mouse.click()` condition is effectively dead.

### Dormant modes

`DWELL_CICK` is compile-time false and the main-loop call to `dwellClick()` is commented out. Gesture collection, sleep/wake interpretation, and blink exemplar calibration are also commented out or disconnected from active flow.

### Lifetime

There is no shutdown or cleanup path. The device continues sampling sensors and emitting HID reports until reset, power loss, or USB disconnection. Gyro offsets persist across resets in EEPROM.

### StickS3 execution flow

The successor starts composite USB and M5Unified, disables unsafe or unproven output, initializes the BMI270, and performs per-boot stationary bias calibration. When ignored credentials are compiled in, it also begins a non-blocking Wi-Fi join and advertises authenticated ArduinoOTA as `sticks3-ptt.local` after connection. The display and CDC report the current state. Button A advances the optical or IMU guided workflows and physically arms or locks mouse mode; pointer reports cannot escape the calibration and arming gates.

The IMU workflow first records a still and normal-blinking control, then deliberate coded groups, then normal head movement. The coded gesture is double blink, a 0.8-1.4 second pause, and double blink; it is performed twice with a pause between patterns. Each stage feeds `ImuBlinkDetector`, counts completed patterns, and logs high-rate CSV. The result is valid only when the current boot has zero still patterns, at least two deliberate patterns, and zero head-motion patterns. Runtime clicks are allowed only after that result and while the pointer is armed. Power loss clears calibration and blink validation, returning the next boot to a locked state.

An incoming OTA update first locks HID, releases all mouse buttons, resets motion and both IMU detectors, invalidates current-boot blink validation, cancels guided capture, and forces external 5 V off. The display then owns progress feedback while `ArduinoOTA.handle()` processes the transfer. A failed transfer leaves mouse output locked; a successful transfer reboots into a fresh locked state.

# Dependencies and build

### Arduino core APIs

The firmware depends on Arduino core facilities, `Wire`, `EEPROM`, and the AVR-native `Mouse` library. The HID implementation requires a board with USB-device capability, matching the documented ATmega32U4 Leonardo and Pro Micro targets.

### External TimerOne dependency

`blink.h` includes `TimerOne.h`, but TimerOne is neither vendored nor declared in installation instructions or a dependency manifest. A developer must install a compatible TimerOne library separately for the AVR sketch.

### Vendored I2Cdevlib

The sketch directory includes its I2Cdevlib sources, so Arduino builds will compile `I2Cdev.cpp` even though the active MPU path does not call it. `library.json` describes I2Cdevlib-Core rather than the Colibrino firmware and should not be treated as a complete project manifest.

### Missing reproducible build definition

There is no `platformio.ini`, Arduino CLI configuration, lockfile, Makefile, Dockerfile, or explicit core and library version. The README instructs users to select Arduino Leonardo in Arduino IDE, which is the only recorded build target.

### Local verification status

An Arduino CLI build was attempted on 2026-08-14 with FQBN `arduino:avr:leonardo`. It stopped before compilation because this machine has only the ESP32 core installed and lacks `arduino:avr`; TimerOne is also absent from the installed user libraries. No source-level build result is therefore available from this capture.

### Automated validation

The legacy target has no unit tests, hardware-in-the-loop tests, static-analysis configuration, GitHub Actions workflows, or other CI. The StickS3 prototype has thirteen native Unity test cases; the authoritative list is the `RUN_TEST` names in `sticks3/test/*/test_main.cpp`, quoted in `AGENTS.md` (four motion, four optical, five coded-IMU cases; refractory rejection is asserted only by the Wokwi gate, not by a native case). All thirteen passed locally on 2026-08-17. No automated hardware-in-the-loop or hosted CI job exists yet.

The StickS3 tree also has a Wokwi CLI gate using the generic `board-esp32-s3-devkitc-1` model. It cross-compiles the production motion, optical analysis, and IMU blink-classifier sources and runs eleven deterministic firmware-side checks. The motion and optical checks remain, and three checks cover a valid double-pause-double IMU pattern, rejection of uniform blinking, and rejection after head motion. The 2026-08-15 Wokwi CLI 0.26.1 run passed and printed `COLIBRINO_SIM_PASS`. Assumption: Wokwi's generic ESP32-S3 CPU and Arduino execution are representative for portable logic only; they are not evidence for StickS3 peripherals or near-eye sensing.

The StickS3 firmware compiled successfully after the coded-pattern change using PlatformIO `espressif32@6.12.0`, Arduino-ESP32 2.0.17, M5Unified 0.2.19, M5GFX 0.2.26, and the core-bundled WiFi, ESPmDNS, Update, and ArduinoOTA libraries. With the ignored local OTA configuration present, the composite CDC, HID, and OTA image used 65,180 bytes of reported RAM and 1,048,465 bytes of the application flash partition. All thirteen native tests passed. The credential-free generic Wokwi image built and ran remotely with eleven passing checks; its separate simulator environment excludes `main.cpp`, the secrets header, Wi-Fi, OTA, board peripherals, and USB HID.

Physical deployment on 2026-08-15 used `/dev/cu.usbmodem1101`, whose ROM descriptor and esptool both reported MAC `AC:27:6E:D2:68:B8`. The cable bootstrap wrote and hash-verified the image. Two subsequent invocations of the authenticated uploader resolved `sticks3-ptt.local` to `192.168.0.194`, verified the same ARP MAC, authenticated, uploaded, rebooted, and returned the OTA service. CDC after both reboots reported `armed=0`, `ir=0`, `calibrated=1`, `imu_blink=0`, and `ota=READY`.

A third invocation resolved the same hostname and saved MAC, authenticated, and completed upload of the coded-pattern image before the user disconnected USB. A new post-reboot CDC state capture was not retained, so do not treat that third transfer as another full round-trip state verification. The updater's exit status was zero and the prior two complete round trips remain the recovery evidence.

# Configuration

### Runtime configuration model

The legacy firmware has no environment variables, flags, secrets, configuration files, databases, or runtime settings. Behavior is controlled by source constants and requires recompilation to change.

The optional Wokwi gate requires `WOKWI_CLI_TOKEN`. `sticks3/scripts/run_wokwi.sh` loads it from the process environment or the repository-root `.env`, which is ignored by Git. `PLATFORMIO_CLI_BIN` and `WOKWI_CLI_BIN` can point the wrapper at CLIs that are not on `PATH`. These values affect development tooling only and are not compiled into device firmware.

StickS3 OTA is optional at compile time. An ignored `sticks3/include/colibrino_secrets.h` must define `WIFI_SSID`, `WIFI_PASS`, and `OTA_PASS`; the committed `.example` contains placeholders only. On this workstation the ignored header reuses the same physical unit's original PTT secrets without copying them into this repository. The ignored root `.env` sets `COLIBRINO_OTA_SECRETS`, `COLIBRINO_OTA_HOST`, and `COLIBRINO_OTA_EXPECTED_MAC` for the uploader. Never print, stage, send to simulation, or commit these values.

The tested unit's OTA hostname is `sticks3-ptt.local` and its required saved MAC is `AC:27:6E:D2:68:B8`. The separate live bedside device advertises `bedside-countdown-s3.local`, identifies as a LilyGO T-Display-S3, and was observed at a different MAC. The uploader must refuse that device even if a caller supplies its hostname.

### Motion constants

Movement sensitivity is 30. MPU conversion constants are 16384 counts per g and 131.072 counts per degree per second. Cursor deltas are refreshed every five calls, and scroll uses `RMEDIA=0.001`, margin 4 degrees, a 200 ms report interval, and a 5 second large-tilt recenter interval.

### Blink constants

Timer sampling nominally forms one optical pair per millisecond. Raw and averaged windows both contain 50 values. Initialization lasts 100 processed cycles. Rest before detection is 20 ms, baseline reset timeout is 100 ms, and the nominal stuck-blink timeout is 1000 ms.

The StickS3 IMU classifier uses a 1.1 degree-per-second residual enter threshold, 0.6 exit threshold, 2.5 degree-per-second maximum raw head rate, 2000 millisecond initial and post-motion suppression, 20 through 300 millisecond impulse duration, and 300 millisecond impulse refractory time. Its four impulses are temporally coded: 300-700 milliseconds within each double blink and 800-1400 milliseconds across the deliberate middle pause. The click refractory time is 1500 milliseconds. Change these only with exact replay of every retained control and intended capture followed by physical validation.

### Dwell constants

Dwell click is disabled by `DWELL_CICK false`. If re-enabled and its main-loop call restored, it waits 1000 ms after motion stops, holds the left button for about 50 ms, and uses pin 16 as its visual indicator.

### Active pin map

The optical analog input is Arduino analog channel 0. The TCRT emitter control is pin 15. Pin 16 is configured as an indicator but is also used by startup and gyro calibration. Pin 9 is initialized as a relay output and left low, but the active relay-named function sends HID button events instead. A buzzer variable points to pin 14, but its `pinMode` is commented and the buzzer function is empty.

### MPU bus

The MPU-6050 is addressed at I2C `0x68` on the board's hardware SDA and SCL pins. The README connection table lists MPU power and ground but omits explicit SDA and SCL rows; the wiring image is therefore important to assembly.

### EEPROM layout

EEPROM address 0 is a one-byte validity marker. Addresses 1 through 12 contain the raw in-memory bytes of three signed 32-bit gyro offsets. There is no version, checksum, sensor identity, board identity, or sanity range.

# Side effects

### Host input

The primary external side effect is USB HID mouse movement, wheel movement, and left-button press, release, or click reports. Bugs can therefore move the user's pointer, activate controls, drag content, or leave a button held.

The StickS3 prototype enumerates native USB CDC plus HID at startup but suppresses reports until stationary calibration succeeds and the user physically arms it through the large blue Button A workflow. M5Unified's hold threshold is set to two seconds so the control has no ambiguous interval at the library's former 500 millisecond default. Stationary armed testing produced zero HID events for more than 30 seconds, while worn-head movement controlled the host pointer. Blink-generated clicks remain disabled unless the current boot's IMU feasibility session passes. There is no separate Button B test-click path.

### Persistent writes

The main sketch writes 13 EEPROM bytes after an uninitialized calibration. The reset sketch writes zero to all 13 bytes every time it boots. These are the only persistent data mutations.

### Hardware output

The firmware toggles the infrared emitter at roughly 1 kHz, changes pin 16 for indication and calibration, holds relay pin 9 low, and performs continuous I2C traffic to the MPU. The intended buzzer output is not active.

The StickS3 prototype can enable the M5PM1-controlled external 5 V rail, transmit 38.46 kHz carrier bursts on internal GPIO 46, and capture the demodulated receiver on internal GPIO 42 during the retained optical diagnostic. It explicitly disables the speaker amplifier latch because M5Stack states that the amplifier must be off during IR reception. The rail must never be driven simultaneously by an external supply. Physical near-eye trials did not produce separable eye state and this optical path is no longer considered a viable click source.

### Serial output

The legacy USB serial path starts at 115200. Once a valid EEPROM marker exists, its calibration function prints the marker value on every calibrated loop. Most other legacy diagnostic printing is commented out.

The StickS3 composite CDC interface is output-only: `USBCDC diagnostics` in `sticks3/src/main.cpp` is only ever written (`begin`, `printf`, `println`), M5Unified's UART is disabled (`serial_baudrate = 0`), and the large blue Button A is the sole application input. It emits one-hertz STATUS, EVENT, RESULT, IR CSV, and IMU CSV during the three guided capture stages (6 s still, 15 s deliberate, 12 s head; nothing is logged during the 3 s preparation screens). Three ignored physical logs (nominal 200 Hz poll cadence, about 162-170 Hz effective because of bursty dropouts up to about 40 ms) are retained under `sticks3/.device-backups/logs/` for exact detector replay. Diagnostic collection writes only to the connected host through CDC; firmware does not persist recordings on the device.

### External services

The legacy firmware makes no network requests, uses no Wi-Fi or Bluetooth, writes no filesystem, calls no external API, and has no database, messaging, telemetry, payment, or cloud side effect.

An OTA-configured StickS3 joins the configured local Wi-Fi network, advertises `_arduino._tcp` through mDNS as `sticks3-ptt.local`, and listens for authenticated update requests on UDP port 3232. Firmware transfer writes the inactive application partition and reboots only after a verified complete image. It performs no cloud call, telemetry, database, payment, or external messaging. OTA is a material persistent write and can replace the whole running application, so hostname plus hardware-MAC verification is mandatory before upload.

Development-only Wokwi validation sends the compiled generic ESP32-S3 image to Wokwi's simulation API and receives its serial output. It requires the local `WOKWI_CLI_TOKEN`; the token and generated firmware stay untracked.

# Risks and constraints

### Case-sensitive build portability

Static-analysis finding: `Colibrino.ino` and `MPU6050.cpp` include `mpu6050.h`, while the tracked filename is `MPU6050.h`. This works on a default case-insensitive macOS filesystem but is expected to fail on case-sensitive Linux builders.

### Fixed-frequency filter

Static-analysis finding: the Mahony integrator assumes exactly 1000 updates per second, but the main loop is not scheduled and performs a 14-byte I2C read at only 100 kHz plus filtering, blink processing, EEPROM reads, and serial output. Orientation scale and drift can therefore depend on actual loop rate.

### Optical processing cadence

Static-analysis finding: Timer1 produces a sample pair every millisecond, but the main loop can take longer and uses a single Boolean ready flag. Multiple interrupt samples can collapse into one processed update, so the moving-window time span varies even though state-machine timeouts use the independently incremented millisecond counter.

### ISR data races

Static-analysis finding: `tickMs` and `horaDoPrint` are shared between the Timer1 interrupt and main code without `volatile` or atomic access. `tickMs` is 32-bit on an 8-bit AVR, so a main-context read may tear. The 16-bit sensor samples are volatile but a coherent pair is not copied under an atomic section.

### Interrupt load

Static-analysis finding: the timer callback performs `analogRead()` inside two 200 microsecond phases. The source itself estimates each conversion at about 110 microseconds, leaving limited interrupt budget and making timing sensitive to core and library changes.

### Calibration reload cost

Static-analysis finding: after calibration is valid, every main-loop call rereads the marker and all 12 offset bytes and prints to serial. The offsets should only need loading once; the repeated I/O adds jitter and worsens the fixed-frequency mismatch.

### Calibration ordering

Static-analysis finding: raw-unit conversion runs before `IMU_calibration()` in every loop. The first loop that loads existing offsets, and the loop that first writes new offsets, use gyro values converted with the previous zero-initialized offsets before updating the orientation filter.

### Calibration integrity

Static-analysis finding: a marker byte of 1 is the only validity check. Partial writes, corrupted offsets, calibration on a different physical mounting, or EEPROM copied across firmware variants are accepted without range or checksum validation.

### Startup feedback drift

Static-analysis finding: the README says a beep announces completed calibration, but the buzzer function is empty and its pin is not configured. The warm-up creates only extremely short pin-16 pulses, and after calibration sets pin 16 high the low branch in `IMU_calibration()` is effectively unreachable because the EEPROM marker is already 1.

### Blink threshold initialization

Static-analysis finding: `limiarDeltaBaseline` is assigned only inside `#ifdef SEM_POT_AJUSTE_SENSIBILIDADE`, but neither that macro nor `DELTA_BASELINE_PADRAO` is defined anywhere in the repository. In the default build the static float remains zero, leaving the baseline-amplitude check far more permissive than the unused constants 12 and 5 suggest.

### Held-button timeout

Static-analysis finding: after 1000 ms in the blink-active state, release occurs only if the derivative is still beyond an edge threshold. A signal that rises, then stays quietly above the release baseline can keep the left mouse button pressed indefinitely until the signal falls.

### Angle unwrap state collision

Static-analysis finding: `mouseHoriz()` calls `corrigePitch(yaw_mahony)` instead of `corrigeYaw()`, and `mouseVert()` immediately calls the same stateful `corrigePitch()` with pitch. Alternating two different axes through one unwrap state can create false plus-or-minus 360 degree offsets and cursor jumps.

### Movement report range

Static-analysis finding: scaled floating-point deltas are converted directly to `int8_t` without clamping. Large orientation discontinuities or filter spikes can exceed the HID signed-byte range and yield implementation-dependent or wrapped motion.

### Repeated displacement

Static-analysis finding: each newly calculated motion delta is returned for five consecutive loops, not only on its sample loop. Sensitivity therefore depends on loop cadence and this repetition, and a future fixed-rate scheduler must preserve or intentionally retune the behavior.

### I2C error handling

Static-analysis finding: MPU writes ignore `Wire.endTransmission()` status, reads do not verify that all 14 bytes arrived, and the local read buffer is not initialized. A disconnected or failing sensor can feed stale or uninitialized data into HID motion instead of entering a safe no-movement state.

### Dead and incomplete paths

Static-analysis finding: `g_novaPiscada` cannot become true through the active path; `SalvarPiscada()` and `MaquinaPiscadas()` are disconnected; `CalibrarPiscada()` is declared and called by dormant code but has no definition; `maquinaGestos_v2()` declares an `int8_t` return but reaches the end without returning. Re-enabling dormant features requires repair, not just uncommenting calls.

### Documentation and pin-role drift

Static-analysis finding: the README calls pin 16 a buzzer connection while code treats it as a visual and calibration output; code's buzzer variable is pin 14 and inactive. The README also contains unfinished placeholders for an image panel and positioning instructions, while its roadmap still lists scroll even though scroll is active in code.

### Accessibility safety

Assumption: because this device controls a computer for users with limited alternative input, firmware changes should fail closed by releasing buttons and suppressing movement on sensor, calibration, or connection errors. Bench testing should use a non-critical host session and include explicit stuck-button and runaway-cursor tests.

### StickS3 classifier generalization

The IMU path is tuned from only three retained logs, one user, one device, and improvised mounting. It detects head-coupled motion associated with deliberate blinking, not eyelid closure itself. Five new runs proved that evenly spaced timing can admit tiny resting motion: three still phases produced a complete false sequence while only two intentional phases produced one. A parameter search could eliminate controls only by losing intended runs. Never enable the replacement coded pattern from offline replay alone; retain current-boot still, deliberate, and head-motion controls and fail closed on any control event.

### StickS3 optical safety and geometry

The integrated IR components are intended for remote-control distances and M5Stack warns that too-close placement produces abnormal reception. No reviewed device documentation provides radiant-intensity or near-eye exposure limits. Do not continue eye-facing experiments with the onboard emitter. An external TCRT5000 design must independently limit emitter current, verify voltage compatibility, and establish a safe mechanical distance before any worn test.

### OTA security and recovery

ArduinoOTA authentication prevents an unauthenticated LAN caller from installing an image, but the protocol is not a TLS deployment channel and the compiled device image necessarily contains network configuration. Keep OTA on a trusted local network, use the existing non-default password, never upload the production artifact to Wokwi or another third party, and preserve USB plus the full flash backup as recovery paths. The uploader's ARP MAC check prevents the known bedside S3 from being overwritten but does not replace physical identity checks when provisioning a new board.

# Git history and tracker context

### Development arc

The project began in July 2020 as HeadMouse. Major firmware work occurred through 2021, including Mahony filtering, TCRT5000 blink detection, ambient-light compensation, scrolling, and dwell click. The repository was reorganized for Arduino IDE 2.0 in October 2022, then gained one-time gyro calibration and EEPROM persistence in December 2022 and the calibration-reset sketch in January 2023.

### Recorded incidents

Commit messages document cursor jumps from derivative handling, blink false positives when no reflective object was present, mouse buttons that were too easy to leave held, dwell click failing after vertical-only movement, and the need to rotate the sensor reference for vertical mounting. These areas deserve regression tests before behavior changes.

### Churn hotspots

Across history, `README.md` is the most frequently changed current file with 23 commits. The active sketch has six commits under its current path, while its legacy predecessor `src/HeadMouse_ino/HeadMouse_ino.ino` has 15. Blink behavior was historically concentrated in that monolithic predecessor before the 2022 split, so path-only churn understates risk in `blink.cpp`.

### Branch layout

Upstream retains `master`, `brunoo`, `dwellClick`, `henrique`, `polly`, and `tcrt5000`. Every upstream non-master branch tip is already an ancestor of the historical upstream `master`; they are historical topic branches, not pending work. The local checkout is on `master`, tracking `origin/master`. The completed `agent/sticks3-port` branch is retained at the source merge point; `master` may contain later bookkeeping-only knowledge commits.

### GitHub issue state

As of 2026-08-14 the fork has no issues, while upstream has one open issue, `tix-life/Colibrino#6`, opened 2023-11-29. It asks whether the device can operate over Bluetooth and what materials would be required. It has no labels, assignee, milestone, or comments.

### Open pull requests

Upstream has no open pull requests. Its five historical pull requests were merged and covered documentation, function separation, one-time calibration, a main-sketch update, and the calibration-reset utility.

### Linear and other trackers

No Linear connector, CLI configuration, API key, or repository reference was available in this environment. No other issue tracker or wiki was discovered. The README links a Google Group for community support and a Google Docs FAQ, but their content was not required to understand the checked-in firmware.

### Stated roadmap

The README roadmap mentions scroll on lateral head tilt and head gestures. Scroll is implemented, while gesture recognition exists only as disconnected code. A user in this session also raised StickS3 and generic ESP32 porting as a likely next direction.

# StickS3 and ESP32 port context

### Prior ESP32 attempt

Git history contains a deleted `src/HeadMouse_ESP` implementation from 2020. It used `BleMouse`, the external MPU-6050, Mahony filtering, fixed hard-coded gyro offsets, and a digital click input on GPIO 34. Commit `d053e5a` removed all 997 lines in January 2022 as an "old / incomplete ESP based alternative". It is useful as historical intent but should not be restored as-is.

### StickS3 hardware

The official StickS3 page identifies an ESP32-S3-PICO-1-N8R8 at 240 MHz, 8 MB flash, 8 MB PSRAM, a built-in BMI270 six-axis IMU at I2C `0x68`, native USB OTG, a 250 mAh battery, buttons, display, audio, IR transmit and receive, a HY2.0-4P port, and a 16-pin Hat2 bus. The internal BMI270 and power chip use GPIO 48 for SCL and GPIO 47 for SDA.

### StickS3 toolchain

The new `sticks3/platformio.ini` follows the official `espressif32@6.12.0`, `esp32-s3-devkitc-1`, Arduino framework, 8 MB partition, and octal PSRAM settings. It pins M5Unified 0.2.19 and explicitly replaces the board's `ARDUINO_USB_MODE=1` with native TinyUSB mode 0. Automatic CDC startup is off so the application can register its CDC and HID interfaces and USB descriptors before starting the composite device. PlatformIO 6.12.0 currently resolves Arduino-ESP32 2.0.17, so the IR driver supports both that object-oriented RMT API and the installed Arduino-ESP32 3.x pin-oriented API.

### StickS3 USB HID feasibility

A wired StickS3 build is source- and hardware-validated for the current prototype. ESP32-S3 native USB and `USBHIDMouse` enumerate as a CDC plus relative-mouse composite when `ARDUINO_USB_MODE=0`; mode 1 is hardware USB-JTAG CDC and excludes the HID example path. Physical testing verified composite enumeration, CDC diagnostics, recovery flashing through the download port, bounded HID reports, stationary suppression, and visible pointer movement from worn-head motion.

### StickS3 IMU migration

The prototype uses M5Unified timestamped BMI270 gyro values in degrees per second. It estimates bias from at least 50 stationary samples over 2.5 seconds, rejects calibration when any axis exceeds 2 degrees per second standard deviation, applies a low-pass filter and smooth deadzone, accumulates fractional relative movement, and bounds each HID report to 60 pixels. It uses measured IMU timestamps rather than a fixed filter rate. Physical table and glasses-mounted testing verified stable calibration, zero stationary host events for more than 30 seconds, and useful head-driven pointer movement. Axis selection and sign are still source constants, calibration is intentionally per boot, and remount reproducibility needs another session.

### StickS3 blink input

The built-in receiver is a digital demodulating remote-control receiver rather than an analog reflectance channel and does not replace the TCRT5000. The retained experiment uses RMT carrier bursts on internal GPIO 46 and captures the active-low response on internal GPIO 42, but multiple physical near-eye stages produced malformed or inseparable signal. M5Stack documents transmitter and receiver alignment at no less than 30 centimeters and warns that too-close placement causes abnormal reception. The vendor does not publish enough radiant-intensity or eye-exposure data to justify pointing this emitter at an eye. Treat onboard near-eye IR as rejected, not merely uncalibrated.

The current alternative detects a deliberately exaggerated coded blink rhythm from bias-corrected BMI270 gyro magnitude rather than claiming ordinary eyelid motion is directly sensed. The production classifier enters an impulse above a 1.1 degree-per-second residual threshold, exits below 0.6, rejects raw head rate above 2.5, suppresses detection during the first two seconds and after head motion, and accepts impulses lasting 20 through 300 milliseconds. It requires double blink with 300-700 millisecond spacing, an 800-1400 millisecond pause, and another double blink, then applies a 1500 millisecond click refractory interval. These constants are conservative safety gates, not a completed usability calibration.

Three ignored physical logs (fixed 5000 us poll cadence, nominal 200 Hz; 162-170 Hz effective per stage because of bursty dropouts; the per-row `usec` column is the timing ground truth and v2 fixtures keep it) contain still, deliberate blinking, and head motion, including five guided sessions from the latest worn test. Firmware results for those five were `1/0/0`, `0/0/0`, `0/1/0`, `1/1/0`, and `1/0/0` for still/blink/head, invalidating the former evenly spaced cadence. Exact replay of the replacement coded detector finds zero patterns across every retained still/head control; old intentional phases were not performed with the new code and therefore cannot establish recall. The current-boot firmware gate still requires still equals zero, deliberate at least two, and head motion equals zero before it can enable runtime IMU clicks. TCRT5000 purchase remains deferred for one focused coded-pattern run; inconsistency, fatigue, or any control event should end this approach and trigger a separately designed analog reflectance adapter.

### StickS3 power and expansion caution

The official page warns that external 5 V can be input or output and defaults to input under M5Unified. Enabling external output while another source drives the bus can damage the device. Any TCRT or accessory wiring must document whether it uses 3.3 V, StickS3-controlled 5 V, or external 5 V before firmware enables `M5.Power.setExtOutput(true)`.

### StickS3 Bluetooth option

Assumption: a wireless StickS3 variant is also feasible as Bluetooth Low Energy HID. ESP32-S3 supports Bluetooth LE but not Bluetooth Classic. A BLE port needs pairing, reconnect, disconnected-state motion suppression, battery and sleep policy, host compatibility tests, and an explicit choice between wired USB HID, BLE HID, or a build that offers both without ambiguous state.

### StickS3 OTA lineage

The tested StickS3's original working project is the separate `devices/m5sticks3/ptt` firmware under the local `home-automations` repository. That project already uses authenticated ArduinoOTA, `sticks3-ptt.local`, and an ignored `secrets.h`; its build wrapper supports `./build.sh ota` while the device is awake. This is distinct from the live `bedside-countdown-s3` project on a LilyGO T-Display-S3. Colibrino intentionally preserves the PTT device's existing OTA identity so future agents can use the established workflow after the one-time bootstrap.

### Generic ESP32 distinction

Assumption: "ESP32" must be resolved to a specific chip and board. The original ESP32 supports Bluetooth Classic and BLE but lacks the ESP32-S3 native USB device path, making BLE HID the normal mouse transport. ESP32-S2 and ESP32-S3 support native USB HID; C-series capabilities differ. Pinout, sensor, power, and HID libraries must therefore be target-specific.

### Emulator inventory

No public tool reviewed on 2026-08-14 emulates the complete StickS3. M5Stack's official `lv_m5_emulator` compiles M5GFX and LVGL UI code as a native SDL application; its current PlatformIO targets cover Core, Core2, CoreS3, StickC Plus, StickC Plus2, Dial, and Tab5, but not StickS3. It is a display-development tool rather than an ESP32-S3 or peripheral emulator.

UiFlow2 supports StickS3 as a deployment target, but its documentation requires a physical device connected by USB or access code and states that `Run Once` runs the program on that device. Its visual canvas is not a hardware emulator.

Espressif QEMU emulates the ESP32-S3 CPU, memory, and selected SoC peripherals. Its graphical support uses a virtual framebuffer that does not exist in the actual SoC. It does not supply StickS3 models for the BMI270, M5PM1, ST7789 wiring, integrated demodulating IR receiver, optical geometry, or full board-level USB behavior.

Wokwi remains the most useful automation layer for this repository because it runs the generic ESP32-S3 image and portable production sources. Assumption: adding M5Stack's UI emulator could help future display-layout work, and adding QEMU could help lower-level ESP-IDF code, but neither would reduce the physical evidence required for the current blink-sensor decision.

### Port architecture boundary

The StickS3 prototype separates portable motion control, optical signal analysis, and `ImuBlinkDetector` from the RMT source and application. `BlinkInput` still supplies normalized sensor-neutral optical samples, allowing a future analog TCRT5000 producer without changing the optical classifier. The IMU classifier is an independent, stateful consumer of the raw gyro vector: `ImuBlinkDetector::update(uint32_t now_ms, const Vec3& gyro_dps)` derives the adaptive EMA baseline (350 ms time constant, dt clamped to 1-50 ms), the residual magnitude, and the raw head-rate magnitude internally; callers cannot inject a pre-computed residual, and a v2 port exposing a residual input must externalize that baseline state or replay parity with v1 will not hold. HID remains in the application layer behind physical arming and current-boot validity gates; when IMU validation passes, the optical path is suppressed to prevent duplicate clicks. A generic ESP32 BLE transport is not implemented.

### Port timing model

The StickS3 motion path uses the BMI270 sample timestamp for actual `dt`; gaps above 200 milliseconds produce no motion. The guided IMU validation has three preparation stages followed by six seconds of normal blinking and stillness, fifteen seconds for two double-pause-double patterns, and twelve seconds of normal head movement. Detector input continues during the three-second preparation screens so the two-second initial quiet gate is genuine. A capture stage cannot be abandoned by holding the button mid-run. Mouse reports are bounded and emitted only in physically armed mode, and clicks are additionally blocked until the current boot proves its pattern gate.

`sticks3/tools/replay_imu_capture.cpp` compiles the production detector for host replay against CDC CSV logs. It splits a log into probe sessions on `EVENT,IMU_PROBE_STARTED`, prints one block per session with the firmware's own PASS predicate, then `SUMMARY sessions=N controls_clean=yes|no validation_passes=K`; exit 0 means parsed and controls clean, never physically validated. This is the preferred tuning loop because it checks the exact C++ implementation rather than a separate analytical approximation. Physical acceptance still requires a remounted device because replay cannot prove sensor placement, comfort, or voluntary usability.

### Port source references

The authoritative StickS3 product and pin documentation is `https://docs.m5stack.com/en/core/StickS3`. Arduino-ESP32 documents ESP32-S2 and ESP32-S3 USB HID classes at `https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html`, and Espressif's Bluetooth capability matrix is at `https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/bt-architecture/overview.html`.

Simulator scope was checked against `https://github.com/m5stack/lv_m5_emulator`, its current `platformio.ini`, the StickS3 UiFlow2 workflow at `https://docs.m5stack.com/en/uiflow2/sticks3/program`, Espressif's ESP32-S3 QEMU guide at `https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/tools/qemu.html`, and Wokwi's ESP32 guide at `https://docs.wokwi.com/guides/esp32`.

# Accessibility wearable direction

### Product model

The investigated product should be a multimodal accessibility intent router rather than a blink-only mouse or general voice assistant. Head motion, deliberate blink, offline voice or consistent vocal sound, dwell, and external adaptive switches should be independent producers. A central fail-closed policy layer should validate their events before a USB HID, BLE HID, Apple Switch Control, keyboard, Shortcut, or optional caregiver action can occur. Voice must supplement rather than replace a non-speech access path.

The current rule that `sticks3/src/main.cpp` is the only owner of HID side effects is the correct architectural foundation. A future `AccessIntent` type should carry bounded semantic requests such as click, right click, double click, drag, drop, scroll, recenter, pause, cancel, or switch activation. Sensor and recognition modules must not call a transport directly.

### StickS3 audio facts

M5Stack documents an ES8311 mono codec, high-sensitivity MEMS microphone, AW8737 amplifier, one-watt speaker, 8 MB PSRAM, and 250 mAh battery in addition to the ESP32-S3 and BMI270. M5's Xiaozhi image runs a wake phrase on the exact board, proving the hardware audio path. The Xiaozhi conversational service is Wi-Fi and account dependent and is not suitable as the safety-critical action path.

The open Xiaozhi StickS3 definition uses MCLK GPIO18, BCLK GPIO17, word-select GPIO15, audio output GPIO14, audio input GPIO16, and codec I2C on GPIO47 and GPIO48. It limits speaker output to 60 percent. M5 warns that speaker volume above 75 percent can reboot the board. M5's published microphone example alternates microphone and speaker operation; simultaneous full-duplex behavior is not proven for Colibrino.

Espressif WakeNet9 and WakeNet9l support ESP32-S3 local wake-word detection. MultiNet accepts a runtime command vocabulary from 16 kHz, 16-bit mono audio and supports hundreds of English or Chinese offline commands depending on model version. Portuguese is not supported by the current MultiNet command models. Espressif's AFE supplies VAD, noise suppression, and optional echo cancellation. Published reference-board memory, latency, and accuracy figures establish feasibility only and cannot be transferred to the StickS3 microphone, glasses mount, acoustic noise, or intended user's speech.

A production custom wake word is a data, licensing, training, and validation project. Start with a built-in phrase such as "Hi M Five" or "Hi ESP". Assumption: a personalized non-word sound or Portuguese vocabulary may later be practical on a trainable NDP120-class processor, but no model or dataset exists yet.

### Voice toolchain and storage constraints

The verified production environment resolves Arduino-ESP32 2.0.17. Espressif's convenient Arduino `ESP_SR` wrapper belongs to the 3.x generation. Voice experiments need a separate Arduino-ESP32 3.x or ESP-IDF environment so the proven USB, OTA, motion, and recovery build remains reproducible.

The current 8 MB layout has NVS, OTA metadata, two approximately 3.3 MB application slots, about 1.5 MB SPIFFS, and a coredump partition. It has no model partition. Espressif's general model-loading example reserves 6 MB. Assumption: a selected minimal WakeNet plus MultiNet artifact may fit a smaller partition, but only an actual build can establish this. Changing the partition table requires one explicitly authorized cable flash; an ordinary OTA image cannot safely replace the installed partition table.

ESP32-S3 supports BLE HID but not Bluetooth LE Audio. Using StickS3 as a normal Mac Bluetooth microphone is not a viable design. Native USB Audio Class is technically possible, but combining audio with the existing CDC and HID stack is higher risk and not required for the first local wake-word experiment.

The current OTA configuration keeps Wi-Fi awake. A battery wearable should disable Wi-Fi during ordinary operation, use BLE HID for primary cable-free control, and expose an explicit maintenance mode for authenticated OTA. Reconnect, host sleep, pairing loss, low battery, and transport switching must release buttons and discard stale actions.

### Safe voice state machine

A wake detection should freeze the pointer, open a short command window, recognize one bounded action, run arming, connection, confidence, and cooldown checks, provide brief feedback, and then resume or recenter. A bare command word must never click. Timeout, uncertainty, cancellation, audio-task overrun, transport loss, low battery, or failed calibration must produce no action and release every held button.

Freezing motion prevents jaw and head movement during speech from displacing the target. Short earcons are preferable to simultaneous spoken feedback because full-duplex audio is unproven. Haptic feedback on a head-mounted IMU can create false motion and should suppress pointer output while active. High-consequence host actions should use host-level confirmation.

### Apple accessibility integration

macOS Voice Control already provides navigation, numbered overlays, grids, dictation, custom vocabulary, custom commands, and Shortcut execution. After its initial language download, normal operation can run without an Internet connection. Vocal Shortcuts on Apple-silicon Macs learns a selected word or another consistent sound locally and is specifically intended to help people with moderate-to-severe atypical speech who can reliably vocalize an utterance. It should be tested against the intended user's actual sounds before building personalized embedded recognition.

Apple Switch Control supports one or multiple switches for scanning, selecting, tapping, dragging, typing, and custom panels. Head Pointer and Dwell provide native fallbacks, and Live Speech speaks typed or saved phrases. Colibrino should expose standard BLE or USB HID and one- or two-switch semantics. Assumption: a future companion app is useful for calibration, profiles, logs, and Shortcuts but should not be required for basic access.

### Compact blink-sensor alternatives

Rejecting StickS3 onboard IR does not require a bulky TCRT5000 breakout. Vishay VCNL36828P integrates a 940 nm VCSEL and proximity receiver in a 2.0 by 1.0 by 0.5 mm I2C package and lists smart glasses as an application. Its published material does not provide the same explicit completed-system Class 1 statement as the ST alternative, so near-eye use requires formal optical and regulatory review.

ST VL53L4CD is a miniature short-range Time-of-Flight sensor with an 18 degree field of view, measurement rates up to 100 Hz, and a documented Class 1 940 nm emitter when used as specified. It is a stronger safety candidate for an initial compact breakout evaluation, but it has not detected this user's eyelid and its field of view, cover, mounting, ambient behavior, and practical sub-centimeter accuracy still require physical validation.

Assumption: the preferred mechanical arrangement is a tiny sensor beside the lens on a flexible PCB with compute and battery mass along the glasses arm or behind the ear. Never aim an undocumented emitter at an eye or infer optical safety from an electrical success.

### Nicla Voice alternative

Arduino Nicla Voice is the strongest second prototype found. Its 22.86 mm square bare board is approximately 2 g and combines an NDP120 always-on neural processor, nRF52832 BLE, BMI270, BMM150, high-quality digital microphone, 16 MB external flash, battery charging, and an external microphone connector. Arduino measures its factory Alexa demo at 0.8 mA with BLE off and 2.4 mA with BLE advertising plus one-hertz sensor polling. Those values do not predict high-rate head-mouse operation but establish a promising always-listening power baseline.

Edge Impulse supports training and deploying custom Nicla Voice audio models, including small vocabularies or non-word sounds. This requires an external account, dataset, training, posterior tuning, and a model flash workflow. BLE HID behavior, high-rate BMI270 access, simultaneous motion plus audio inference, pairing recovery, model licensing, and realistic battery runtime remain unproven. The onboard SAMD11 is a programming bridge, so native USB HID should not be assumed.

Assumption: a final product will choose between an ESP32-S3 or S31 with at least 16 MB flash and 8 MB PSRAM for simpler low-cost development, and an NDP115 or NDP120 plus Nordic BLE controller for better always-on power and personalized audio. The choice requires measured StickS3-versus-Nicla motion, voice, weight, battery, and recovery evidence.

### Competitive targets

Quha Zono X demonstrates a 12 g gyroscopic Bluetooth head mouse with a quoted 208 Hz update rate, up to 18 hours, dwell, external switches, multiple mounts, gestures, a magnetic dock, and four-device pairing. GlassOuse demonstrates head-worn mouse, switch, joystick, multi-device, and adaptive-switch modes. Cephable demonstrates local software combination of voice, facial expression, head movement, gestures, and switches.

Assumption: useful final targets are less than 15 g on the head, 200 Hz-class motion, all-day battery, cable-free primary operation, simple charging, multiple mounting choices, multi-device pairing, a universal adaptive-switch input, and basic operation without an application, account, Wi-Fi, or cloud service.

### Staged validation

Stage zero compares the current pointer with macOS Dwell, Head Pointer, Voice Control, Vocal Shortcuts, Switch Control, and Live Speech, recording which modalities the intended user can use comfortably. Stage one builds an isolated StickS3 16 kHz microphone and built-in wake-word proof that emits diagnostics only. It tests quiet and soft speech, television, conversation, fan noise, glasses rubbing, microphone orientation, and lying versus seated use.

Stage two introduces `AccessIntent`, pointer freeze during speech, a central safety reducer, BLE HID alongside USB, maintenance-only Wi-Fi, a generic dry-contact switch input, and watchdog behavior. Stage three completes the installed coded-IMU worn test, evaluates a compact optical sensor if that test is inconsistent or tiring, and compares a Nicla Voice proof. Stage four selects a custom board and mechanical system only after measured evidence.

Voice acceptance requires zero HID actions during a long negative-audio soak, repeatable intended commands in real positions and noise, stable pointer targeting during speech, and fail-closed behavior for every audio, power, and transport fault. Wokwi can test intent and policy logic but cannot validate microphone acoustics, a glasses-mounted BMI270, BLE hosts, battery, optics, or user usability.

The full source list and staged design record are in `docs/ACCESSIBILITY_WEARABLE_STUDY.md`.

# Cross-repository v2 and Luos decision

### Repository authority

The local firmware repository is `/Users/fcavalcanti/dev/Colibrino` with remote `https://github.com/fcavalcantirj/Colibrino`. It owns device firmware, portable domain code, hardware captures, immutable labeled fixtures, tests, feel constants, board glue, HID and BLE side effects, physical acceptance, and releases.

The sibling `/Users/fcavalcanti/dev/oracle-loop` repository with remote `https://github.com/fcavalcantirj/oracle-loop` owns the executable oracle map, specification, and assisted implementation engine. Its synchronized `main` was `3cf1c3012548912adebc22a90b2d03dce3e396cd`. Nothing below is on main. The branch `dasbrow/build-the-transform-prompt-parse-core-fo-20260817-005512` (tip `3c6798413e5514419579e7af23a31f06f603f20f`) carries the engine (last engine code commit `6377bd9`) plus the ratified v2 oracle map (`b932e38`) and the v2 SPEC (`3c67984` itself); `agent/colibrino-v2-luos-qualification` (`84b5bea`) adds `docs/10` and `AGENTS.md` and revises the map, SPEC, README, and STATE; `agent/colibrino-v2-round-one-corrections` (`c7a82ac06ebfd72016082321588daa97d30c2c1b`, stacked on `84b5bea`) restores the owner-ratified three-rule safety list, records the canonical unit set as PROPOSED, the capture-path decision, the `v2/` paths and presets, and untracks `.coverage`. `PORT_PLAN.json` records these as `engine_stack_commit`, `luos_qualification_commit`, and `round_one_corrections_commit`; a later Oracle state-handoff commit may advance that last branch and Colibrino does not chase it.

The analyzed Oracle files are `AGENTS.md`, `STATE.md`, `docs/09-colibrino-v2-multimodal-accessibility.md`, `docs/10-colibrino-v2-luos-qualification.md`, `docs/colibrino-v2-ORACLE-MAP.md`, `docs/colibrino-v2-SPEC.md`, and `engine/README.md`. The stack must remain branch-only until the user gives Oracle Loop's exact merge grant. Fetching, pulling, pushing, or documenting the stack never grants merge, rebase, squash, or history-rewrite authority.

### Oracle round one

Round one is head motion plus blink. Voice is intentionally deferred to round two. The round-one units are `imu-motion` (contract-only this round; rate-based, subsuming the map's `imu-fusion` + `gesture` split; the proven `sticks3` `MotionController` is the differential reference and Mahony fusion is deferred), `blink-dsp` (pure impulse-candidate detector that ends at `IMPULSE`/`CANCEL` events), `blink-code` (PROPOSED in the Oracle map: the temporal double-pause-double matcher that alone produces a click candidate), `access-intent` (the only action authority), and `profile` (versioned configuration validation). A thin blink pipeline composes `blink-dsp` and `blink-code` and is the differential reference against `ImuBlinkDetector`.

Fresh evidence requires labeled still, head sweep, single hard blink, natural blink, confounder, coded double-pause-double, and evenly spaced four-blink traces. The existing guided capture already emits the timestamped BMI270 accel+gyro stream but only inside its fixed 6/15/12 s stages with three stage labels and no host command channel, so the round-one fixture set is produced by a multi-run matrix with host-side labels (`docs/V2_TRACE_CAPTURE_PROTOCOL.md`): every deliberate gesture happens only inside `BLINK_FIRMLY`, and `KEEP_HEAD_STILL` and `MOVE_HEAD` stay genuine controls in every run. No upload is required; a free-run capture stage is an optional later extension. Tests, trace labels, feel tolerances, hardware glue, HID and BLE output, and physical validation remain human-owned. Oracle Loop may propose implementation only within the file authority named by its map.

The crown-jewel oracle is that each service converts a recorded input trace into expected output events within tolerance and the `AccessIntent` arbiter never emits an action that a bounded, authorized input did not explicitly request. Negative cases must include invalid or malformed, stale, expired, duplicated, low-confidence, unarmed, unhealthy-producer, queue-fault, disconnected-transport, and low-battery events, each producing no action and releasing held output, while exactly one authorized, armed, fresh, healthy candidate produces exactly one action.

### Luos upstream facts

Luos Engine 3.1.0 is an MIT-licensed ANSI C embedded service and message runtime. Its concepts are nodes, services, messages, and optional physical transports. A single MCU can use local services without an external network. The default message contains a seven-byte header and at most 128 bytes of data. Storage is statically bounded by configuration such as `MAX_LOCAL_SERVICE_NUMBER`, `MAX_MSG_NB`, and `MSG_BUFFER_SIZE`, and the application must call `Luos_Loop()` continuously.

The upstream commit `f1af47bdd760ce7038fbb396d1d203c8c2723464` passed 121 of 121 native Unity cases on this Mac. A temporary no-upload build compiled Luos 3.1.0 for `esp32-s3-devkitc-1` with `espressif32@6.12.0` and Arduino-ESP32 2.0.17. The reference example reported 22,104 bytes of RAM and 282,689 bytes of flash. Those sizes include Arduino, the example, and Robus and are not the incremental cost of Colibrino integration.

The ESP32-S3 reference build emitted an incompatible Robus timer callback warning, an integer-to-pointer warning, and a linker warning for a global `ctx` symbol that collides with an ESP32 Wi-Fi library symbol. The build proves compilation only, not runtime operation.

### Luos upstream risks

The official source contains ESP32 and ESP32-C3 examples but no explicit ESP32-S3 or StickS3 example. The GitHub workflow comments the ESP32 example directory out of its build matrix. Open issue 423 records the ESP build CI gap. Open issue 464 records watchdog resets for ESP32 LED and button examples; its discussion reports that a single-node build without the network avoided the reset.

The ESP32 HAL supplies the ESP timer but leaves general IRQ state control, flash initialization and read or write, boot mode, node-ID persistence, and reboot operations empty. Its default message allocator and global Luos mutex hooks are empty. These no-op hooks are unsafe if multiple FreeRTOS tasks, cores, callbacks, or interrupts access Luos concurrently.

Luos services are stateful callbacks, not pure functions. Wrapping DSP inside a service callback would weaken fixture oracles. The current Colibrino portable C++ modules already demonstrate the desired pure boundary and must not be thrown away merely to claim a from-scratch service architecture.

### Conditional architecture

Luos service concepts are accepted, while the runtime is conditional. New motion, blink, intent, and profile components use allocation-free typed APIs and fixed-size event contracts without Luos, Arduino, FreeRTOS, USB, or BLE types. Events include monotonic time, sequence, producer identity, validity or confidence, and expiry. Safety commands use fixed identifiers rather than aliases or dynamic discovery.

The `AccessIntent` reducer remains the only action authority and fail-closes on invalid input, stale state, producer failure, transport loss, low battery, and queue fault. Its release-all path is a direct synchronous board operation and cannot depend on message delivery. Sensor, speech, switch, USB, BLE, OTA, Apple, and optional Luos code are adapters.

The first Luos spike is diagnostic-only, localhost-only, and excludes Robus, topology discovery, network gates, and Luos remote update. One documented FreeRTOS task must own every Luos call unless real ESP32-S3 mutex and critical-section hooks are implemented and tested. The same contracts must also operate over a simple internal fixed-capacity event bus so a failed qualification discards no domain, test, fixture, or oracle work.

Production qualification requires exact-StickS3 measurements of incremental RAM and flash, 100 to 200 Hz latency and jitter, queue saturation and overflow, sequencing, duplicates, timeouts, restarts, watchdog stability, USB CDC and locked HID behavior, authenticated OTA maintenance mode, and later audio coexistence. Silent drops, stale delivery, assertion loops, watchdog resets, or a stuck button reject the runtime. Assumption: Luos may remain useful for diagnostics or non-HID orchestration even if it never becomes part of the authorization path.

### Immediate work order

The next evidence-producing work is the fresh worn BMI270 capture, immutable fixture selection, pure `blink-dsp`, and negative `AccessIntent` oracles. The diagnostic Luos spike follows those units. Apple accessibility and isolated wake-word work remain valuable but do not block round one. The authoritative detailed decision is `docs/LUOS_ARCHITECTURE_DECISION.md`, the physical protocol is `docs/V2_TRACE_CAPTURE_PROTOCOL.md`, and machine-readable task gates are T12, T13, and T14 in `sticks3/PORT_PLAN.json`. The v2 host core lives under `v2/` and is branch-only until real fixtures exist; the repository invariant is that master's tip is never RED.

# Constraints for future changes

### Preserve user-visible semantics deliberately

Current motion sensitivity combines angle delta, five-call sampling, and fivefold repeated reports. Current blink behavior is press-on-start and release-on-end, not a discrete click. A refactor or port must define whether compatibility means preserving these exact mechanics or preserving only perceived cursor speed and click behavior.

### Persistent-state compatibility

Changing the calibration representation requires a new versioned layout or an intentional reset. Do not reinterpret the existing 12 raw bytes as another sensor or platform's offsets. Prefer validated records and `EEPROM.update()` or the target platform's wear-aware preferences storage.

### Hardware-safe failure behavior

Sensor initialization failure, short I2C reads, invalid calibration, BLE disconnection, USB suspension, and task stalls should stop movement and release all mouse buttons. Any code that powers an external sensor or emitter must establish pin role and voltage before enabling output.

# Open questions

### Current hardware behavior

The source has not been compiled or exercised on an Arduino Leonardo in this capture. Actual loop frequency, cursor direction, scroll direction, blink sensitivity, and whether a button can remain held require hardware measurement.

### Intended blink threshold

It is unclear whether `SEM_POT_AJUSTE_SENSIBILIDADE` and `DELTA_BASELINE_PADRAO` were supplied by an omitted build configuration, or whether the default zero baseline threshold is accidental. History should be consulted further or behavior measured before choosing a replacement threshold.

### Intended click semantics

The active state machine implements click-and-hold, while the main loop contains a dead discrete `Mouse.click()` path and the repository also contains disabled dwell click. Product requirements must establish which modes are required and how the user selects them.

### Board choice

StickS3 over wired native USB is the implemented prototype direction for the next hardware test. Another ESP32-S3 board and legacy ESP32 over BLE remain possible later targets but are not part of the current source.

### StickS3 eye-sensor wiring

No external eye sensor is assigned yet. The internal GPIO 46 transmitter and GPIO 42 receiver failed the intended near-eye geometry and should not be aimed at the eye. If the remaining IMU rhythm test fails, the exact expansion pins, analog input, voltage rail, TCRT5000 current-limiting resistor, receiver circuit, and mechanical position require a schematic and bench validation before connection. Do not infer that a bare TCRT5000 can be wired directly to arbitrary StickS3 pins.

### StickS3 mounting and repeatability

Improvised glasses-mounted runs produced useful pointer motion, but repeat placement, axis feel, sensitivity, comfort, and accidental movement after remounting are not yet characterized. The coded-pattern firmware is already installed by OTA. The next session should attach the device consistently, capture one post-reboot locked-state status, and repeat the approximately 45-second guided test before tuning movement or deciding on an external sensor.

### Coded-blink usability

The replay evidence proves conservative separation for the recorded controls, not that the double-pause-double head-coupled gesture is reliable or accessible for intended users. The unresolved decision is whether two firm blinks, a roughly one-second pause, and two firm blinks can produce two intended patterns without still or normal-head false events. If that is inconsistent, tiring, or ever unsafe, stop tuning it and move to an analog reflectance sensor.

### Device restoration

The original 8 MB device image is backed up and ignored correctly, but has not been restored because one final hardware validation remains. After that experiment, restore the captured image and verify normal boot before returning the user's working StickS3 project. The exact application-level acceptance signal after restoration is user-specific and remains to be confirmed at that time.

### OTA and motion coexistence

The cable bootstrap and two authenticated OTA round trips are complete, including post-reboot fail-closed state checks. A third upload installed the coded pattern but lacks a retained post-reboot CDC check because USB was disconnected. A longer worn regression is still needed to confirm that continuous Wi-Fi background service does not perceptibly degrade BMI270 pointer timing, stationary suppression, or coded-blink validation. OTA transfer itself must continue to own a locked, off-face maintenance state.

### Transport priority

The open upstream issue requests Bluetooth, while the current user also named the USB-capable StickS3. It remains unresolved whether low-latency wired USB, wireless BLE, or dual transport is the primary requirement and how pairing or cable transitions should behave.

### Voice suitability and language

It is unknown which wake phrase, word, or consistent non-word sound the intended user can produce reliably while seated and lying down, how quickly voice becomes tiring, and whether Portuguese is mandatory for device-side commands. ESP-SR MultiNet does not currently supply Portuguese commands. Compare Apple Vocal Shortcuts with the isolated embedded proof before selecting or training a speech stack.

### Speech partition and runtime

The minimum viable WakeNet and command-model flash artifacts have not been built against the current application. It is unknown whether dual OTA can remain practical in 8 MB, whether upgrading the Arduino core changes USB or OTA behavior, and how continuous audio plus BMI270 sampling affects pointer timing, battery, temperature, or microphone interference.

### Compact optical sensor

VCNL36828P and VL53L4CD are research candidates, not validated click sensors. The exact eyelid distance signal, sample rate, ambient rejection, cover geometry, eye-safety documentation, supply rail, I2C pins, interrupt behavior, mount, and flexible interconnect remain unresolved. Prefer a Class 1 documented evaluation path and stop immediately on any safety ambiguity.

### Nicla Voice feasibility

Nicla Voice has not been acquired or tested. Standard BLE HID mouse and switch behavior, 100-200 Hz motion delivery through the NDP120-connected BMI270, simultaneous custom audio inference, local model update and licensing, host reconnect, battery size, and accessible feedback must be proven before it can replace StickS3.

### Luos runtime qualification

Compilation and upstream native tests do not answer whether localhost-only Luos on the exact StickS3 can coexist with 100 to 200 Hz BMI270 sampling, composite USB, OTA, Wi-Fi background work, and later audio without drops, jitter, races, or watchdog resets. It is also unresolved whether a one-owner-task design offers enough value over the simpler internal bounded bus to justify the dependency. No production decision should be made before the T13 measurements and fault injection exist.

### Oracle fixture privacy

Fresh trace fixtures need enough raw timing and sensor data to reproduce failures, but body-motion traces may be identifying in some contexts. Decide which captures can be de-identified and committed immutably to Colibrino and which must remain private with only derived synthetic fixtures or hashes recorded. Oracle Loop must consume only evidence explicitly cleared for its repository workflow.

### Build baseline

The supported Arduino AVR core version and TimerOne version are not documented. A future maintenance change should first establish a reproducible legacy build, or explicitly declare the new ESP32 target as a clean successor rather than claiming bit-for-bit compatibility.
