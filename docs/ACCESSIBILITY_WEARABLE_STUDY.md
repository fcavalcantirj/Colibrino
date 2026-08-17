# Accessibility Wearable Architecture Study

This document records the investigated direction beyond the current StickS3
head-mouse prototype. It separates hardware facts from proposed design choices
so a later session can continue without repeating the research or mistaking a
promising component for a validated capability.

## Product conclusion

Colibrino should evolve into a multimodal accessibility intent router rather
than a blink-only mouse or a general voice assistant. A user profile should be
able to combine head motion, deliberate blinking, offline voice or consistent
vocal sounds, dwell, and an external adaptive switch. One unreliable modality
must be removable without eliminating the user's entire access path.

The current StickS3 is a strong proof platform. A final wearable will probably
need a lighter, lower-power board, Bluetooth Low Energy HID, and a compact
near-eye sensor on a flexible connection. Core pointer and click actions must
remain local and fail closed; cloud services can add convenience but must not
be required for basic computer access.

## StickS3 voice capability

M5Stack documents the StickS3 as an ESP32-S3-PICO-1-N8R8 with 8 MB flash,
8 MB PSRAM, an ES8311 mono audio codec, a high-sensitivity MEMS microphone,
an AW8737 speaker amplifier, a one-watt speaker, a BMI270, and a 250 mAh
battery. M5's published Xiaozhi firmware uses a wake phrase on this exact board,
which verifies the complete microphone, codec, processing, and feedback path.
The Xiaozhi conversation service itself depends on Wi-Fi and an account and is
not appropriate as the safety-critical click path.

The open-source Xiaozhi StickS3 board definition confirms the audio wiring:
MCLK GPIO18, BCLK GPIO17, word-select GPIO15, codec output GPIO14, codec input
GPIO16, and codec I2C on GPIO47 and GPIO48. It reduces speaker output to 60
percent. M5 warns that high speaker volume can reboot the board. M5's microphone
example alternates recording and playback, so Colibrino should treat the board
as half-duplex until simultaneous microphone and speaker operation is proven.

Espressif ESP-SR provides the relevant local pipeline. WakeNet9 and WakeNet9l
support ESP32-S3 wake-word detection. MultiNet recognizes a runtime-configured
offline command vocabulary from 16 kHz, 16-bit mono audio. The Audio Front End
provides voice activity detection, noise suppression, and optional echo
cancellation. Published ESP32-S3 reference-board benchmarks show that a
single-microphone speech-recognition pipeline and WakeNet are practical in
PSRAM; MultiNet models use materially more PSRAM. Those figures are feasibility
evidence, not StickS3 accuracy or battery measurements.

Built-in wake phrases include options such as "Hi ESP" and "Hi M Five". A
production custom wake word is a separate data, licensing, and validation
project. Espressif's current offline command recognizer supports English and
Chinese, not Portuguese. Do not promise Portuguese device-side commands using
MultiNet. A custom small-vocabulary model on a neural audio processor is a
possible later path.

## Current firmware integration constraints

The production environment pins PlatformIO `espressif32@6.12.0`, which resolves
Arduino-ESP32 2.0.17. Espressif's current Arduino `ESP_SR` wrapper and basic
wake-word/command example belong to the Arduino-ESP32 3.x generation. Voice
work should therefore use a separate experimental environment or an ESP-IDF
component build. It must not silently replace the physically validated USB,
OTA, and motion environment.

The current 8 MB flash layout contains two approximately 3.3 MB application
slots, about 1.5 MB SPIFFS, NVS, OTA metadata, and a coredump partition. It has
no speech-model partition. Espressif's general model-loading example reserves
6 MB, which does not fit beside the present dual-slot layout. The actual minimal
WakeNet plus command model may be smaller, but only a compiled image and model
artifact can settle the layout. Installing a changed partition table requires
one authorized cable flash; normal OTA can resume afterward if two viable
application slots remain.

ESP32-S3 can expose BLE HID, but it does not provide Bluetooth LE Audio. The
StickS3 should recognize a small command vocabulary locally rather than try to
become a normal Bluetooth microphone for the Mac. USB Audio Class is technically
possible through native USB, but composite CDC, HID, and microphone support
adds substantial risk and is not required for the first voice experiment.

Continuous Wi-Fi is also undesirable for a wearable. The present OTA build
keeps Wi-Fi awake so maintenance is convenient. A battery product should keep
Wi-Fi off during normal use, use BLE HID for control, and enter a deliberate
maintenance mode for authenticated OTA.

## Safe voice interaction

Voice code must produce an intent, never call the mouse API directly. The
current `main.cpp` rule that only the guarded application owns HID side effects
is the correct foundation. A future `AccessIntent` boundary should accept
events from IMU motion, voice, blink, dwell, and external switches and pass
them through one policy reducer before any transport emits output.

The initial state machine should be:

```text
pointer active
  -> wake phrase detected
  -> freeze pointer and open a short command window
  -> recognize click, right, double, drag, drop, scroll, center, pause, or cancel
  -> validate current arming, connection, confidence, and cooldown
  -> emit at most one bounded action
  -> provide a short confirmation and resume or recenter
```

A bare occurrence of "click" must never click. Uncertain recognition, timeout,
task overrun, transport loss, low battery, failed calibration, or cancelled
speech must produce no action and release every held button. Freezing the
pointer during speech prevents jaw and head movement from shifting the target.
Audible feedback should use short earcons between listening windows. Head-worn
vibration can disturb the IMU and must pause pointer output while active.

Voice cannot be the sole access channel. Fatigue, noise, privacy, atypical
speech, loss of speech, and microphone occlusion all require blink, dwell, or
external-switch fallback. High-consequence host actions should use host-level
confirmation rather than trusting a single embedded recognition result.

## Apple accessibility integration

Colibrino should complement Apple accessibility instead of rebuilding it.
macOS Voice Control already supports navigation, item names, numbered overlays,
grids, dictation, custom vocabulary, custom commands, and Shortcuts. After its
initial language download, normal Voice Control operation can run without an
Internet connection.

Vocal Shortcuts on Apple-silicon Macs learns a user-selected word or another
consistent sound and processes it on device. Apple specifically positions it
for moderate-to-severe atypical speech when the person can reliably vocalize
some utterance. This may outperform a generic embedded English command model
for the intended user and should be tested before investing in custom speech
training.

Switch Control accepts one or more switches and can scan, select, tap, drag,
type, and run custom panels. Head Pointer and Dwell provide additional native
fallbacks. Live Speech can speak typed or saved phrases aloud. Colibrino should
therefore expose standard BLE/USB HID mouse controls and one- or two-switch
semantics while a companion application, if built, focuses on calibration,
profiles, diagnostics, and Apple Shortcut integration.

## Blink sensing beyond TCRT5000

The StickS3 onboard IR pair is rejected for near-eye reflectance. The receiver
is a demodulating remote-control component and physical tests did not separate
the eyelid states. This does not mean a full TCRT5000 breakout is the only
alternative.

The Vishay VCNL36828P integrates a 940 nm VCSEL and proximity receiver in a
2.0 by 1.0 by 0.5 mm package, supports I2C and low idle current, and lists smart
glasses and VR/AR headsets as applications. Its datasheet does not provide the
same explicit completed-system eye-safety statement as the ST ToF alternative,
so near-eye use would require a proper optical and regulatory safety review.

The ST VL53L4CD is a miniature short-range Time-of-Flight sensor with an 18
degree field of view, measurements up to 100 Hz, and a documented Class 1
940 nm emitter when used as specified. It is larger than the Vishay part but
offers a clearer prototype safety case and should be evaluated before buying a
bulky TCRT5000 module. Neither component is validated for detecting this user's
blink at the intended glasses geometry; a physical breakout test and custom
mount remain mandatory.

The preferred mechanical concept is a tiny optical sensor beside the lens on a
flexible PCB, with processing and battery mass along the glasses arm or behind
the ear. Do not point an undocumented emitter at an eye or infer safety from a
successful electrical test.

## Alternative compute platform

Arduino Nicla Voice is the strongest investigated second prototype. Its bare
board is 22.86 mm square and about 2 g. It combines an NDP120 always-on neural
processor, nRF52832 BLE controller, BMI270, BMM150 magnetometer, a high-quality
digital microphone, 16 MB external flash, battery charging, and an external
microphone input. Arduino's datasheet measures 0.8 mA for its factory Alexa
demo with BLE off and 2.4 mA with BLE advertising plus one-hertz sensor polling.
Those numbers do not predict a 100-200 Hz BLE head mouse, but they show a much
better always-listening power architecture than a continuously awake ESP32-S3.

The NDP120 can run custom audio models deployed through Edge Impulse, including
small vocabularies or non-word sounds trained for an individual. That workflow
requires data collection and an external model-training service. BLE HID mouse
and switch behavior, high-rate BMI270 access, pairing recovery, model licensing,
and simultaneous audio-plus-motion performance remain unproven on Nicla Voice.
Its SAMD11 is a programming bridge, so native USB HID should not be assumed.

The likely final hardware choice is either an ESP32-S3/S31 with at least 16 MB
flash and 8 MB PSRAM for lower cost and faster development, or an NDP115/NDP120
plus Nordic BLE controller for lower always-on power and personalized audio.
The StickS3 and Nicla comparison should measure actual worn weight, motion
latency, wake accuracy, false actions, battery runtime, and recovery behavior
before a custom board is selected.

## Product targets from existing devices

Quha Zono X demonstrates a 12 g gyroscopic Bluetooth head mouse with a quoted
208 Hz update rate, up to 18-hour battery life, multiple mounting accessories,
dwell, external switches, gestures, a magnetic dock, and four-device pairing.
GlassOuse demonstrates approximately 24 g head-worn control with mouse, switch,
joystick, multiple-device, and adaptive-switch modes. Cephable demonstrates a
software-side multimodal model combining voice, facial expressions, head
movement, gestures, and switches locally.

These products suggest concrete goals: less than 15 g on the head, 200 Hz-class
motion, all-day battery, cable-free primary operation, simple charging,
multiple mounts, multi-device pairing, a universal adaptive-switch input, and
configuration that is not required for basic operation. Colibrino's opportunity
is an open and affordable combination of these capabilities with personalized
offline voice and blink input.

## Luos and Oracle Loop architecture

The from-scratch v2 work is coordinated with
`/Users/fcavalcanti/dev/oracle-loop`. Colibrino remains the authority for
firmware, physical captures, immutable fixtures, tests, feel tuning, hardware
glue, HID/BLE output, and acceptance. Oracle Loop's engine stack ends at
`dasbrow/build-the-transform-prompt-parse-core-fo-20260817-005512` commit
`3c67984`; the audited v2 map, specification, and Luos qualification are on the
new branch `agent/colibrino-v2-luos-qualification` commit `84b5bea`. Neither is
on `main`, and the stack must not be merged without the repository's exact user
grant.

Luos is a promising service boundary, but the independent audit supports only
conditional adoption. Luos Engine 3.1.0 passed all 121 upstream native tests and
compiled in a temporary ESP32-S3 Arduino build using Colibrino's pinned
toolchain. Its official ESP32 examples are nevertheless absent from current CI,
an open issue records watchdog resets when the network is enabled, and the
ESP32 HAL leaves mutex, IRQ, persistence, and reboot hooks empty. The compile
also exposed Robus warnings that reinforce avoiding the physical network on a
single-board wearable.

New `imu-motion`, `blink-dsp`, `profile`, and `AccessIntent` code will therefore
use pure, allocation-free APIs and fixed-size typed contracts that can be
wrapped by Luos without depending on it. The synchronous fail-closed reducer
and release-all path remain outside Luos. A later diagnostic-only spike may use
one owner task and localhost delivery, with no Robus, only after fresh traces
and the pure host oracle exist. Production adoption requires hardware evidence
for message ordering and overflow, 100-200 Hz timing, watchdog stability,
USB/OTA coexistence, and fault recovery. The full evidence, qualification gate,
fallback bus, and cross-repository rules are in
[`LUOS_ARCHITECTURE_DECISION.md`](./LUOS_ARCHITECTURE_DECISION.md).

## Staged implementation

### Stage 0: round-one evidence and pure core

Capture fresh labeled StickS3 BMI270 traces, preserve them as immutable
fixtures, and implement pure `blink-dsp` and `AccessIntent` units against the
Oracle map. Keep the existing pointer and fail-closed HID behavior intact.
After those oracles pass, run the diagnostic-only localhost Luos qualification
spike; do not make Luos a release dependency yet.

### Stage 1: Apple baseline

Compare the current StickS3 pointer with macOS Dwell, Head Pointer, Voice
Control, Vocal Shortcuts, Switch Control, and Live Speech. Record which signals
the intended user can produce comfortably and repeatedly. This requires no new
hardware.

### Stage 2: isolated StickS3 audio proof

Create a separate toolchain environment, capture continuous 16 kHz mono audio,
and detect a built-in free wake phrase plus six to ten offline commands. Emit
only diagnostics and display state. Test quiet speech, soft speech, television,
conversation, fan noise, glasses rubbing, different microphone orientations,
and lying versus seated use. Do not emit HID.

### Stage 3: multimodal safety integration

Introduce `AccessIntent` and a central fail-closed policy reducer. Freeze motion
during voice command windows. Add BLE HID beside the proven USB transport, an
explicit maintenance-only Wi-Fi mode, a generic dry-contact adaptive-switch
input, and watchdog/release behavior for every producer and transport.

### Stage 4: click and hardware comparison

Complete the installed double-pause-double IMU test. If it is inconsistent,
tiring, or produces any control event, stop tuning it and evaluate a compact
VL53L4CD or similarly documented near-eye sensor. Compare the StickS3 with a
Nicla Voice proof under the same motion, voice, battery, and recovery protocol.

### Stage 5: custom wearable

Select the processor only after measured evidence. Design a light glasses or
behind-ear assembly with physical mute/arm control, charging dock or protected
connector, safe optical geometry, accessible feedback, battery monitoring, and
an optional universal switch connection. Core input must remain operational
without a phone application, account, Wi-Fi, or cloud service.

## Acceptance gates

The voice proof passes only when a long negative-audio soak produces zero HID
actions, intended commands are repeatable in the user's real positions and
acoustic environments, pointer position remains stable during speech, and all
task, transport, and power failures suppress output. A false wake may open and
close a harmless listening window; a false command must never escape as an
action.

BLE passes only when reconnect, host sleep/wake, pairing loss, low battery,
transport switching, and device reboot release every button and never replay a
stale action. Apple integration must be physically checked on each intended
Mac, iPhone, and iPad mode; generic BLE capability alone is not sufficient.

Wokwi can continue testing portable intent and policy logic, but it does not
emulate the StickS3 codec, microphone acoustics, BMI270 mount, BLE host, power
system, near-eye optics, or composite USB behavior. Audio recordings and IMU
captures can be replayed on the host, while final acceptance always requires a
worn physical test.

## Open decisions

The intended user's reliable vocal capability, preferred language, fatigue
limits, and tolerance for a wake phrase remain unknown. Portuguese embedded
recognition, the minimal 8 MB speech partition, StickS3 battery runtime, BLE
HID latency, Nicla BLE HID feasibility, compact optical mounting, and whether a
single-module or flex-connected behind-ear layout is most comfortable all
require measurement.

No TCRT5000, Nicla Voice, or custom PCB purchase is justified solely by this
study. The next evidence-producing work is the prepared worn IMU capture,
immutable trace fixtures, pure blink and intent oracles, and then the
diagnostic-only Luos spike. Apple and isolated wake-word comparisons follow
without blocking that round-one loop.

## Primary sources

M5Stack StickS3 hardware: <https://docs.m5stack.com/en/core/StickS3>

M5Stack StickS3 Xiaozhi assistant: <https://docs.m5stack.com/en/guide/realtime/xiaozhi/sticks3>

Xiaozhi ESP32 source: <https://github.com/78/xiaozhi-esp32>

Espressif ESP-SR: <https://github.com/espressif/esp-sr>

ESP-SR WakeNet: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/README.html>

ESP-SR MultiNet: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_command_recognition/README.html>

ESP-SR benchmarks: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/benchmark/README.html>

Arduino-ESP32 ESP-SR example: <https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP_SR/examples/Basic/Basic.ino>

Luos Engine: <https://github.com/Luos-io/luos_engine>

Luos service architecture: <https://www.luos.io/docs/luos-technology/services>

Luos ESP32 CI issue: <https://github.com/Luos-io/luos_engine/issues/423>

Luos ESP32 watchdog issue: <https://github.com/Luos-io/luos_engine/issues/464>

Apple Vocal Shortcuts: <https://support.apple.com/guide/mac-help/use-vocal-shortcuts-mchlf4548bb6/mac>

Apple Voice Control: <https://support.apple.com/pt-br/guide/mac-help/mchl9899c8a5/mac>

Apple Switch Control: <https://developer.apple.com/documentation/accessibility/switch-control>

Arduino Nicla Voice: <https://docs.arduino.cc/hardware/nicla-voice/>

Nicla Voice datasheet: <https://docs.arduino.cc/resources/datasheets/ABX00061-datasheet.pdf>

Edge Impulse Nicla Voice deployment: <https://docs.edgeimpulse.com/hardware/boards/arduino-nicla-voice>

Vishay VCNL36828P: <https://www.vishay.com/en/product/80306/>

ST VL53L4CD: <https://www.st.com/en/imaging-and-photonics-solutions/vl53l4cd.html>

Quha Zono X: <https://www.quha.com/products/quha-zono-x/>

GlassOuse Pro: <https://glassouse.com/product/glassouse-pro>

Cephable: <https://cephable.com/>
