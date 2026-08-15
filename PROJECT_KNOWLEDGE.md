# Project Knowledge

# Changelog

## 2026-08-15T16:20:19Z — HEAD 59ffaab

Distinguished the tested Colibrino StickS3 from the separate live `bedside-countdown-s3`, recovered the tested unit's original `sticks3-ptt` authenticated ArduinoOTA workflow, and carried that capability into Colibrino behind ignored credentials. Added fail-closed OTA callbacks, a hostname plus saved-MAC upload guard, documentation and structured acceptance criteria, then passed all twelve native tests and the OTA-enabled production build while confirming the local simulator artifact contains no Wi-Fi or OTA credential strings. The installed older Colibrino image still needs one cable bootstrap before later updates can use OTA.

## 2026-08-15T03:12:46Z — HEAD 4c640d2

Physically validated StickS3 native USB CDC plus HID, BMI270 calibration, safe pointer arming, stationary suppression, and worn-head cursor motion. Recorded that the onboard demodulating IR pair is unsuitable for near-eye reflectance, added a conservative current-boot four-blink IMU classifier and guided validation workflow, replayed two 200 Hz physical captures without still or head-motion false sequences, expanded native and Wokwi coverage, and deferred any TCRT5000 purchase until one repeat mounted test proves or rejects the IMU click path.

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

The last fully analyzed source commit is `59ffaab1b19d283f3859c4695dcc2b389f9d8763`. It contains the physically exercised StickS3 firmware, conservative IMU blink classifier, authenticated OTA preservation, MAC-guarded uploader, capture replay tool, native and Wokwi validation, hardware findings, and operating documentation. The current knowledge update describes that source revision and intentionally does not include ignored device backups, physical capture logs, credentials, or PlatformIO output.

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

A committed successor prototype exists under `sticks3/` for the M5Stack StickS3. It is a standalone wired USB HID mouse with composite USB CDC for commands and diagnostic CSV, plus optional authenticated Wi-Fi OTA when an ignored device header is present. Physical testing confirmed USB enumeration, BMI270 calibration, fail-closed arming, stationary suppression, and head-controlled cursor motion. It starts with movement locked and requires the large blue Button A to advance guided tests or arm the pointer. Blink clicks remain disabled unless the current boot passes its guided IMU validation. The tested device currently holds the earlier safe test image whose validation failed closed rather than the latest four-blink and OTA build; because that installed image lacks an OTA listener, one cable bootstrap remains unavoidable.

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

`sticks3/src/main.cpp` is the physical-device application. It initializes M5Unified, composite TinyUSB CDC plus HID, optional Wi-Fi plus ArduinoOTA, power, BMI270 motion, display state, buttons, optical diagnostics, guided IMU capture, and fail-closed HID output. `motion_controller.cpp` contains portable calibration and pointer mapping. `signal_analysis.cpp` and `ir_blink_input.cpp` implement the retained optical experiment. `imu_blink_detector.cpp` implements the conservative four-impulse classifier. Headers under `sticks3/include/colibrino/` define the portable boundaries.

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

The IMU workflow first records a still and normal-blinking control, then deliberate four-blink groups, then normal head movement. Each stage feeds `ImuBlinkDetector`, counts completed sequences, and logs high-rate CSV. The result is valid only when the current boot has zero still sequences, at least two deliberate sequences, and zero head-motion sequences. Runtime clicks are allowed only after that result and while the pointer is armed. Power loss clears calibration and blink validation, returning the next boot to a locked state.

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

The legacy target has no unit tests, hardware-in-the-loop tests, static-analysis configuration, GitHub Actions workflows, or other CI. The StickS3 prototype has twelve native Unity test cases covering stationary gyro calibration, motion rejection, pointer deadzone and accumulation, optical-signal separation, optical blink timing, the guided optical protocol, IMU impulse rejection, head-motion rejection, valid four-impulse rhythm detection, and refractory behavior. All twelve passed locally. No automated hardware-in-the-loop or hosted CI job exists yet.

The StickS3 tree also has a Wokwi CLI gate using the generic `board-esp32-s3-devkitc-1` model. It cross-compiles the production motion, optical analysis, and IMU blink-classifier sources and runs ten deterministic firmware-side checks. The original eight motion and optical checks remain, and two checks cover a valid four-impulse IMU rhythm plus rejection of short and motion-contaminated sequences. The 2026-08-15 run passed and printed `COLIBRINO_SIM_PASS`. Assumption: Wokwi's generic ESP32-S3 CPU and Arduino execution are representative for portable logic only; they are not evidence for StickS3 peripherals or near-eye sensing.

The StickS3 firmware compiled successfully after the OTA safety changes using PlatformIO `espressif32@6.12.0`, Arduino-ESP32 2.0.17, M5Unified 0.2.19, M5GFX 0.2.26, and the core-bundled WiFi, ESPmDNS, Update, and ArduinoOTA libraries. With the ignored local OTA configuration present, the composite CDC, HID, and OTA image used 65,172 bytes of reported RAM and 1,048,377 bytes of the application flash partition. All twelve native tests passed. The credential-free generic Wokwi image built locally and contained none of the configured Wi-Fi SSID, Wi-Fi password, or OTA password strings. The external Wokwi run was not repeated after this app-layer-only change to avoid sending a credential-bearing production artifact; the ten-check portable logic result from earlier the same day remains applicable because no portable source changed.

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

The StickS3 IMU classifier uses a 1.0 degree-per-second residual enter threshold, 0.6 exit threshold, 2.5 degree-per-second maximum raw head rate, 2000 millisecond initial and post-motion suppression, 20 through 300 millisecond impulse duration, 300 millisecond impulse refractory time, 350 through 1100 millisecond sequence gaps, four impulses per sequence, and a 1500 millisecond click refractory time. Change these only with exact replay of every retained control and intended capture followed by physical validation.

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

The StickS3 composite CDC interface accepts text commands and emits status plus high-rate IMU CSV during guided capture. Two ignored approximately 200 Hz physical logs are retained under `sticks3/.device-backups/logs/` for exact detector replay. Diagnostic collection writes only to the connected host through CDC; firmware does not persist recordings on the device.

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

The four-blink IMU path is tuned from only two sessions with one user, one device, and improvised mounting. It detects head-coupled motion associated with deliberate blinking, not eyelid closure itself. A threshold change that increases intended detections can also admit normal posture corrections, speech, walking, tremor, or cable movement. Never enable it from stored historical success alone; retain current-boot still, deliberate, and head-motion controls and fail closed on any control event.

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

The current alternative detects a deliberately exaggerated four-blink rhythm from bias-corrected BMI270 gyro magnitude rather than claiming ordinary eyelid motion is directly sensed. The production classifier enters an impulse above a 1.0 degree-per-second residual threshold, exits below 0.6, rejects raw head rate above 2.5, suppresses detection during the first two seconds and after head motion, accepts impulses lasting 20 through 300 milliseconds, requires four impulses separated by 350 through 1100 milliseconds, and applies a 1500 millisecond click refractory interval. These constants are conservative safety gates, not a completed usability calibration.

Two ignored approximately 200 Hz physical capture sets contain still, deliberate blinking, and head motion. Exact replay of the production C++ detector found zero sequences in both still controls and both head-motion controls. The first deliberate-blink recording contained three valid four-impulse sequences; the second contained one, below the required two intended sequences for a full validation pass. The current-boot firmware gate therefore requires still equals zero, deliberate blink at least two, and head motion equals zero before it can enable runtime IMU clicks. TCRT5000 purchase remains deferred for one repeat mounted run; inconsistent, tiring, or false-positive four-blink behavior should end this approach and trigger a separately designed analog reflectance adapter.

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

The StickS3 prototype separates portable motion control, optical signal analysis, and `ImuBlinkDetector` from the RMT source and application. `BlinkInput` still supplies normalized sensor-neutral optical samples, allowing a future analog TCRT5000 producer without changing the optical classifier. The IMU classifier is independent and consumes calibrated raw and residual gyro magnitudes. HID remains in the application layer behind physical arming and current-boot validity gates; when IMU validation passes, the optical path is suppressed to prevent duplicate clicks. A generic ESP32 BLE transport is not implemented.

### Port timing model

The StickS3 motion path uses the BMI270 sample timestamp for actual `dt`; gaps above 200 milliseconds produce no motion. The guided IMU validation has three preparation stages followed by six seconds of normal blinking and stillness, fifteen seconds of deliberate four-blink groups, and twelve seconds of normal head movement. Detector input continues during the three-second preparation screens so the two-second initial quiet gate is genuine. A capture stage cannot be abandoned by holding the button mid-run. Mouse reports are bounded and emitted only in physically armed mode, and clicks are additionally blocked until the current boot proves its sequence gate.

`sticks3/tools/replay_imu_capture.cpp` compiles the production detector for host replay against CDC CSV logs. This is the preferred tuning loop because it checks the exact C++ implementation rather than a separate analytical approximation. Physical acceptance still requires a remounted device because replay cannot prove sensor placement, comfort, or voluntary usability.

### Port source references

The authoritative StickS3 product and pin documentation is `https://docs.m5stack.com/en/core/StickS3`. Arduino-ESP32 documents ESP32-S2 and ESP32-S3 USB HID classes at `https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html`, and Espressif's Bluetooth capability matrix is at `https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/bt-architecture/overview.html`.

Simulator scope was checked against `https://github.com/m5stack/lv_m5_emulator`, its current `platformio.ini`, the StickS3 UiFlow2 workflow at `https://docs.m5stack.com/en/uiflow2/sticks3/program`, Espressif's ESP32-S3 QEMU guide at `https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/tools/qemu.html`, and Wokwi's ESP32 guide at `https://docs.wokwi.com/guides/esp32`.

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

One improvised glasses-mounted run produced useful pointer motion, but repeat placement, axis feel, sensitivity, comfort, and accidental movement after remounting are not yet characterized. The next session should flash the latest four-blink firmware, attach the device consistently, and repeat the approximately 45-second guided test before tuning movement or deciding on an external sensor.

### Four-blink usability

The replay evidence proves conservative separation for the recorded controls, not that four deliberate head-coupled blink impulses are reliable or accessible for the intended users. The unresolved decision is whether repeated groups of four firm blinks at roughly 0.6-second spacing can produce at least two intended sequences without still or normal-head false events. If that is inconsistent, tiring, or ever unsafe, stop tuning it and move to an analog reflectance sensor.

### Device restoration

The original 8 MB device image is backed up and ignored correctly, but has not been restored because one final hardware validation remains. After that experiment, restore the captured image and verify normal boot before returning the user's working StickS3 project. The exact application-level acceptance signal after restoration is user-specific and remains to be confirmed at that time.

### OTA bootstrap and round trip

The OTA-enabled Colibrino image has compiled but has not run on hardware. The installed older Colibrino test image does not join Wi-Fi or advertise ArduinoOTA, so being powered on is not equivalent to being network-reachable. One USB flash must bootstrap the new listener. Before relying on it, verify that the correct MAC advertises `sticks3-ptt.local`, complete two authenticated OTA round trips, confirm every update locks HID and external power, and check that Wi-Fi background activity does not degrade BMI270 motion timing or stationary suppression.

### Transport priority

The open upstream issue requests Bluetooth, while the current user also named the USB-capable StickS3. It remains unresolved whether low-latency wired USB, wireless BLE, or dual transport is the primary requirement and how pairing or cable transitions should behave.

### Build baseline

The supported Arduino AVR core version and TimerOne version are not documented. A future maintenance change should first establish a reproducible legacy build, or explicitly declare the new ESP32 target as a clean successor rather than claiming bit-for-bit compatibility.
