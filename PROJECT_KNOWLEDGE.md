# Project Knowledge

# Changelog

## 2026-08-14T22:21:27Z — HEAD 870cf23

Added the uncommitted `sticks3/` prototype implemented in this session: its pinned build, guarded native USB HID, BMI270 motion path, sensor-neutral blink boundary, RMT onboard-IR experiment, guided feasibility gates, host tests, safety defaults, successful no-upload build, and remaining fresh-device validation work. Corrected the earlier USB-mode and onboard-IR assumptions using the exact bundled framework APIs and M5Stack controller documentation.

## 2026-08-14T21:39:57Z — HEAD 870cf23

Initial capture at the current repository tip. It records the AVR firmware architecture and execution flow, hardware and persistent-state behavior, build dependencies, repository and tracker history, static-analysis risks, and the feasibility boundary for a future StickS3 or ESP32 port raised during this session.

# Analyzed revision

### Snapshot

The analyzed commit is `870cf2333915872fd3dc88d5c980a3c63dc375c8` on local branch `master`. Local `master`, `origin/master`, and upstream `tix-life/Colibrino` all resolve to this commit. The worktree was clean before this knowledge file was created.

### Repository lineage

The configured origin is the public fork `fcavalcantirj/Colibrino`. GitHub identifies `tix-life/Colibrino` as its parent and source repository. The upstream description is "Dispositivo para controlar o computador apenas com movimentos da cabeça. Voltado para pessoas com deficiências motoras", and its listed homepage is `https://colibrino.com.br`.

### Activity and maturity

The latest firmware-affecting commit is from January 2023, and the latest repository commit is a README update from 2025-01-29. The upstream repository was not archived as of this capture, but it has no current CI, release tags, open pull requests, or recent firmware work. Treat the implementation as an old hardware prototype rather than a maintained production firmware.

# Runtime

### Product

Colibrino is standalone embedded firmware for a do-it-yourself head mouse. It converts head orientation into cursor movement and scroll input, and uses a strong blink detected by an infrared reflectance circuit for mouse-button input.

### Intended users

The README explicitly targets people with physical and motor disabilities including tetraplegia, arthrogryposis, amputations, and cerebral palsy. Accessibility and prevention of unintended pointer or button events are therefore primary safety and usability constraints.

### Deployment model

The active target is an Arduino Leonardo or ATmega32U4 Pro Micro connected to a computer over USB. The microcontroller runs continuously as a USB HID mouse; there is no server, desktop application, worker, cron job, or cloud component.

An uncommitted successor prototype now exists under `sticks3/` for the M5Stack StickS3. It is also a standalone wired USB HID mouse, with composite USB CDC for diagnostic CSV output. It deliberately starts with IR power off and mouse movement locked, and it has not been flashed to hardware.

### Physical assembly

The documented assembly uses an MPU-6050 accelerometer and gyroscope attached to eyeglasses, a TCRT5000 infrared reflectance sensor placed near the eye, an Arduino-compatible board, resistors, indicator components, a buzzer, wiring, and a USB cable. Three STL files provide enclosure parts, and `doc/` contains assembly images.

### Language and documentation

Firmware is Arduino C and C++. The user documentation and most project-specific comments are Portuguese. Some comments in `blink.cpp` contain replacement characters from prior encoding damage.

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

The legacy target has no unit tests, hardware-in-the-loop tests, static-analysis configuration, GitHub Actions workflows, or other CI. The StickS3 prototype adds eight native Unity test cases for stationary gyro calibration, motion rejection, pointer deadzone and accumulation, optical-signal separation, blink timing, and the guided protocol. All eight passed locally. No hardware-in-the-loop or CI job exists yet.

The StickS3 firmware compiled successfully without upload using PlatformIO `espressif32@6.12.0`, Arduino-ESP32 2.0.17, M5Unified 0.2.19, and M5GFX 0.2.26. The final composite CDC and HID image used 35,064 bytes of reported RAM and 575,733 bytes of the application flash partition. Hardware behavior remains unverified until the fresh StickS3 arrives.

# Configuration

### Runtime configuration model

There are no environment variables, flags, secrets, configuration files, databases, or runtime settings. Behavior is controlled by source constants and requires recompilation to change.

### Motion constants

Movement sensitivity is 30. MPU conversion constants are 16384 counts per g and 131.072 counts per degree per second. Cursor deltas are refreshed every five calls, and scroll uses `RMEDIA=0.001`, margin 4 degrees, a 200 ms report interval, and a 5 second large-tilt recenter interval.

### Blink constants

Timer sampling nominally forms one optical pair per millisecond. Raw and averaged windows both contain 50 values. Initialization lasts 100 processed cycles. Rest before detection is 20 ms, baseline reset timeout is 100 ms, and the nominal stuck-blink timeout is 1000 ms.

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

The StickS3 prototype enumerates native USB CDC plus HID at startup but suppresses reports until stationary calibration succeeds, mouse mode is selected, and button A is held for two seconds. Blink-generated clicks remain disabled unless the current boot's IR feasibility session passes; button B can issue an intentional test click while mouse output is armed.

### Persistent writes

The main sketch writes 13 EEPROM bytes after an uninitialized calibration. The reset sketch writes zero to all 13 bytes every time it boots. These are the only persistent data mutations.

### Hardware output

The firmware toggles the infrared emitter at roughly 1 kHz, changes pin 16 for indication and calibration, holds relay pin 9 low, and performs continuous I2C traffic to the MPU. The intended buzzer output is not active.

The StickS3 prototype can enable the M5PM1-controlled external 5 V rail after a two-second button hold, transmit 38.46 kHz carrier bursts on internal GPIO 46, and capture the demodulated receiver on internal GPIO 42. It explicitly disables the speaker amplifier latch because M5Stack states that the amplifier must be off during IR reception. Holding the button again disables the rail. The rail must never be driven simultaneously by an external supply.

### Serial output

USB serial starts at 115200. Once a valid EEPROM marker exists, the calibration function prints the marker value on every calibrated loop. Most other diagnostic printing is commented out.

### External services

The active firmware makes no network requests, uses no Wi-Fi or Bluetooth, writes no filesystem, calls no external API, and has no database, messaging, telemetry, payment, or cloud side effect.

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

# Git history and tracker context

### Development arc

The project began in July 2020 as HeadMouse. Major firmware work occurred through 2021, including Mahony filtering, TCRT5000 blink detection, ambient-light compensation, scrolling, and dwell click. The repository was reorganized for Arduino IDE 2.0 in October 2022, then gained one-time gyro calibration and EEPROM persistence in December 2022 and the calibration-reset sketch in January 2023.

### Recorded incidents

Commit messages document cursor jumps from derivative handling, blink false positives when no reflective object was present, mouse buttons that were too easy to leave held, dwell click failing after vertical-only movement, and the need to rotate the sensor reference for vertical mounting. These areas deserve regression tests before behavior changes.

### Churn hotspots

Across history, `README.md` is the most frequently changed current file with 23 commits. The active sketch has six commits under its current path, while its legacy predecessor `src/HeadMouse_ino/HeadMouse_ino.ino` has 15. Blink behavior was historically concentrated in that monolithic predecessor before the 2022 split, so path-only churn understates risk in `blink.cpp`.

### Branch layout

Upstream retains `master`, `brunoo`, `dwellClick`, `henrique`, `polly`, and `tcrt5000`. Every non-master branch tip is already an ancestor of `master`; they are historical topic branches, not pending work. The local checkout tracks only `origin/master`.

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

A wired StickS3 build is source-validated. ESP32-S3 native USB and `USBHIDMouse` compile as a CDC plus relative-mouse composite when `ARDUINO_USB_MODE=0`; mode 1 is hardware USB-JTAG CDC and excludes the HID example path. The prototype builds and links the TinyUSB mouse implementation but enumeration, CDC coexistence, recovery flashing, and report behavior still require the fresh board.

### StickS3 IMU migration

The prototype uses M5Unified timestamped BMI270 gyro values in degrees per second. It estimates bias from at least 50 stationary samples over 2.5 seconds, rejects calibration when any axis exceeds 2 degrees per second standard deviation, applies a low-pass filter and smooth deadzone, accumulates fractional relative movement, and bounds each HID report to 60 pixels. It uses measured IMU timestamps rather than a fixed filter rate. Axis selection and sign remain source constants pending the final glasses-mount test; calibration is intentionally per boot and not persisted yet.

### StickS3 blink input

The built-in receiver is a digital demodulating remote-control receiver rather than an analog reflectance channel, so it must not be assumed to replace the TCRT5000. The prototype nevertheless performs the requested empirical test: RMT generates eight 500 microsecond carrier bursts separated by 500 microsecond gaps, RMT captures active-low pulse timing, and the normalized active-time ratio feeds a sensor-neutral classifier.

The guided session captures open-eye, closed-eye, and repeated-blink stages without assuming polarity. Its firmware gate requires at least 20 valid open and closed frames, at least 4 pooled standard deviations of separation, at least 15 percent relative change, and at least two transitions lasting 40 through 650 milliseconds. The stricter documented purchase gate requires two ambient-light runs, at least four detected blinks from five attempts in each, and no false click during a 30-second open-eye control. Until those tests pass, onboard blink sensing remains unproven and TCRT5000 purchase remains deferred.

### StickS3 power and expansion caution

The official page warns that external 5 V can be input or output and defaults to input under M5Unified. Enabling external output while another source drives the bus can damage the device. Any TCRT or accessory wiring must document whether it uses 3.3 V, StickS3-controlled 5 V, or external 5 V before firmware enables `M5.Power.setExtOutput(true)`.

### StickS3 Bluetooth option

Assumption: a wireless StickS3 variant is also feasible as Bluetooth Low Energy HID. ESP32-S3 supports Bluetooth LE but not Bluetooth Classic. A BLE port needs pairing, reconnect, disconnected-state motion suppression, battery and sleep policy, host compatibility tests, and an explicit choice between wired USB HID, BLE HID, or a build that offers both without ambiguous state.

### Generic ESP32 distinction

Assumption: "ESP32" must be resolved to a specific chip and board. The original ESP32 supports Bluetooth Classic and BLE but lacks the ESP32-S3 native USB device path, making BLE HID the normal mouse transport. ESP32-S2 and ESP32-S3 support native USB HID; C-series capabilities differ. Pinout, sensor, power, and HID libraries must therefore be target-specific.

### Port architecture boundary

The StickS3 prototype separates portable motion control and signal analysis from the RMT source and application. `BlinkInput` supplies normalized sensor-neutral samples, allowing a future analog TCRT5000 producer without changing classification or USB click behavior. HID remains in the application layer behind physical arming and validity gates. A generic ESP32 BLE transport is not implemented.

### Port timing model

The StickS3 motion path uses the BMI270 sample timestamp for actual `dt`; gaps above 200 milliseconds produce no motion. The onboard-IR experiment samples at a nominal 35 millisecond interval and uses asynchronous receive plus blocking transmission of an 8 millisecond envelope. It emits no blink click without valid frames and passed calibration. Mouse reports are bounded and only emitted in physically armed mouse mode.

### Port source references

The authoritative StickS3 product and pin documentation is `https://docs.m5stack.com/en/core/StickS3`. Arduino-ESP32 documents ESP32-S2 and ESP32-S3 USB HID classes at `https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html`, and Espressif's Bluetooth capability matrix is at `https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/bt-architecture/overview.html`.

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

No external eye sensor is assigned yet. First test the internal GPIO 46 transmitter and GPIO 42 receiver at the intended eye geometry using the guided protocol. If that fails the decision gates, the exact expansion pins, voltage rail, TCRT resistor values, and mechanical position still require a schematic and bench validation before connection.

### StickS3 mounting axes

The relationship between the StickS3 BMI270 axes and the intended glasses or head mounting is unknown. The current Y-Z-X permutation and 90-degree roll baseline must not be copied without an orientation test matrix.

### Transport priority

The open upstream issue requests Bluetooth, while the current user also named the USB-capable StickS3. It remains unresolved whether low-latency wired USB, wireless BLE, or dual transport is the primary requirement and how pairing or cable transitions should behave.

### Build baseline

The supported Arduino AVR core version and TimerOne version are not documented. A future maintenance change should first establish a reproducible legacy build, or explicitly declare the new ESP32 target as a clean successor rather than claiming bit-for-bit compatibility.
