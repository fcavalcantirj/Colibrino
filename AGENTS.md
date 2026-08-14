# AGENTS.md

## Project purpose

Colibrino is an accessibility-focused head mouse. Head motion controls the
pointer and an intentional blink provides the primary click input. Accidental
pointer reports, false clicks, stuck buttons, and unsafe sensor power are
product-safety issues, not cosmetic bugs.

Read `PROJECT_KNOWLEDGE.md` before making broad changes. Update it when a change
materially alters architecture, hardware assumptions, build commands, safety
behavior, or verified hardware status.

## Source layout

The legacy implementation is under `arduino project/Colibrino/` and targets an
Arduino Leonardo or ATmega32U4 Pro Micro with an external MPU6050 and TCRT5000.
Preserve it unless a request explicitly includes legacy maintenance.

The new implementation is under `sticks3/` and targets the M5Stack StickS3. It
uses the internal BMI270, native USB CDC plus HID, and an experimental onboard
IR blink probe. It is a clean successor target rather than a drop-in compilation
of the AVR sketch.

`sticks3/PORT_PLAN.json` records validated hardware facts, acceptance gates, and
the decision boundary for adding an external TCRT5000. Keep it valid JSON.

## StickS3 build and tests

Run commands from `sticks3/`.

```sh
platformio test -e native
platformio run -e m5stack-sticks3
```

The first command must pass all portable motion and blink-analysis tests. The
second command compiles firmware but does not upload it. Do not treat a clean
build as hardware validation.

If PlatformIO is installed in its managed environment on this workstation, the
equivalent executable is:

```sh
/Users/fcavalcanti/.platformio/penv/bin/platformio
```

Generated `.pio/` contents are ignored and must not be committed.

## Hardware safety

Never upload firmware unless the user explicitly identifies the target device
or serial port. The StickS3 currently in active use is protected and must not be
flashed. Fresh-device testing is a separate, explicit step.

The StickS3 internal IR path uses the M5PM1-controlled external 5 V rail. Never
enable that output while another supply drives EXT_5V or an attached accessory
power rail. Keep output power off at boot and require deliberate physical
arming.

Keep the StickS3 speaker amplifier disabled during IR reception. M5Stack states
that it interferes with the receiver.

USB mouse output must default to locked. Sensor initialization failure,
calibration failure, invalid timing, or invalid blink data must suppress HID
reports. Any future held-button implementation must release the button when its
input or transport becomes invalid.

## Onboard IR evidence standard

The StickS3 receiver is a digital demodulating remote-control receiver, not an
analog reflectance sensor. Do not claim that it detects blinks merely because
the RMT code compiles or receives pulses.

Use the guided open-eye, closed-eye, and deliberate-blink protocol on the fresh
device. Retain the CDC CSV output. Test at the intended glasses-to-eye geometry
in at least two ambient-light conditions.

Rely on onboard IR only after both repeat runs meet the gates in
`sticks3/PORT_PLAN.json`, including deliberate-blink recall and the open-eye
false-click control. If those gates fail after positioning trials, add an
external analog reflectance sensor through the existing sensor-neutral
`BlinkInput` boundary.

## Coding guidance

Keep board-specific GPIO, RMT, M5Unified, power, and USB code out of the portable
motion and signal-analysis modules. Portable behavior belongs under
`sticks3/include/colibrino/` and corresponding source files, with native tests.

Use measured IMU timestamps. Preserve bounded HID reports, stationary bias
calibration, deadzone behavior, and fractional motion accumulation unless a
change includes updated tests and an on-device rationale.

Treat gyro axis selection and signs as mount-specific. Confirm them using the
motion monitor on the final glasses mount instead of copying the AVR axis
permutation.

Support the pinned PlatformIO toolchain first. The IR adapter currently handles
the Arduino-ESP32 2.x RMT API bundled by `espressif32@6.12.0` and the 3.x API;
preserve both paths when changing it.

## Change discipline

Do not commit generated firmware, local PlatformIO state, credentials, serial
logs containing unrelated user data, or editor files.

Before handing off a StickS3 code change, run the native tests, compile the
firmware without upload, validate `PORT_PLAN.json`, and report clearly which
claims remain hardware-dependent.
