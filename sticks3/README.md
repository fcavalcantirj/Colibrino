# Colibrino StickS3 prototype

This is a separate ESP32-S3 port. It does not modify the legacy AVR firmware.
It provides a guarded USB head-mouse prototype and an evidence-gathering test
for the StickS3 onboard infrared pair. The onboard IR receiver is digital and
demodulating, so blink detection is intentionally considered unproven until a
fresh device passes the guided test.

## Build without flashing

From this directory, run:

```sh
platformio test -e native
platformio run -e m5stack-sticks3
```

Neither command uploads firmware. Do not connect or flash the StickS3 that is
currently in use. The upload step is deliberately deferred until the fresh
device arrives.

## ESP32-S3 simulator gate

Wokwi does not model the complete StickS3 board. The simulator target therefore
uses its generic ESP32-S3 DevKitC model to execute the same portable gyro
calibration, pointer mapping, signal separation, blink timing, and guided
feasibility-protocol code used by the StickS3 firmware. It is a real
cross-compiled ESP32-S3 run, not a replacement for the fresh-device test.

Install the Wokwi CLI, put the token in the repository's ignored `.env`, and run:

```sh
WOKWI_CLI_TOKEN=your_token_here
./scripts/run_wokwi.sh
```

The script also accepts `PLATFORMIO_CLI_BIN` and `WOKWI_CLI_BIN` when either CLI
is not on `PATH`. A passing run must print `COLIBRINO_SIM_PASS`; any failed check
prints `COLIBRINO_SIM_FAIL` and fails the CLI run.

This gate does not validate the StickS3 BMI270/I2C wiring, M5PM1-controlled 5 V
rail, display, onboard IR optics/receiver, native USB HID enumeration, or button
electrical behavior. Those remain hardware-only acceptance gates.

After the fresh-device upload, capture diagnostics with:

```sh
platformio device monitor --baud 115200
```

## Safety defaults

The firmware starts in IR diagnostic mode with the controlled external 5 V rail
off. Before holding button B for two seconds to enable onboard IR, disconnect
anything that can drive the EXT_5V or external connector power rail. Driving
that rail from the StickS3 and an external source at the same time can damage
hardware. Hold B for two seconds again to turn IR power back off before adding
anything to that rail.

The internal speaker and microphone stay disabled because the speaker amplifier
interferes with the onboard IR receiver. USB enumerates as CDC plus HID, but no
mouse reports are emitted until motion calibration succeeds, mouse mode is
selected, and button A is held for two seconds.

## Fresh-device procedure

Build and upload only to the fresh StickS3. Keep it still until the motion page
says READY. In IR mode, make sure no external 5 V source is present, then hold B
for two seconds. Position the board at the intended glasses-to-eye distance and
press B once to run the guided sequence: eye open, eye closed, then repeated
deliberate blinks. The display shows each stage and USB CDC emits CSV evidence.

Run the sequence twice under different ambient lighting. The firmware reports a
pass only when open and closed signals are separated and at least two plausible
blink-duration transitions are observed. The stricter purchase decision in
`PORT_PLAN.json` requires at least four detections from five deliberate blinks
in each repeat and a false-click control before relying on onboard IR.

Button A cycles IR, motion monitor, and mouse mode while mouse output is locked.
The motion monitor exposes raw BMI270 gyro axes for confirming axis orientation
on the final mount. In mouse mode, hold A for two seconds to arm or lock output;
button B sends a deliberate test click. Blink clicks remain disabled unless the
guided onboard-IR test has passed in the current boot.

## Why an external TCRT5000 remains optional

The software consumes blink measurements through a sensor-neutral interface.
If onboard IR passes, no external reflectance sensor is needed. If it cannot
produce stable open-versus-closed separation or reliable deliberate blinks, an
analog TCRT5000 adapter can be added later without rewriting the motion or USB
layers.
