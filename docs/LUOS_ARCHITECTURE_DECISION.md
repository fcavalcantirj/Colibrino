# Luos Architecture Decision

## Decision

Colibrino v2 will use Luos-compatible service boundaries from the first new
module, but Luos Engine is not yet approved as the production safety kernel.
Pure, allocation-free signal and policy code remains independent of Luos,
Arduino, FreeRTOS, USB, and BLE. A thin adapter may carry bounded events through
a single-node Luos runtime after the qualification gate below passes on the
exact StickS3.

This is a conditional adoption, not a rejection. Luos is a good fit for
modality isolation, replaceable hardware producers, host replay, and future
multi-board prototypes. Its current ESP32 support is not strong enough to own
click authorization, stuck-button release, or transport-failure recovery
without additional evidence.

## Cross-repository authority

The firmware, portable domain code, hardware captures, and physical validation
live in `/Users/fcavalcanti/dev/Colibrino`, mirrored at
<https://github.com/fcavalcantirj/Colibrino>.

The oracle specification and assisted implementation engine live in
`/Users/fcavalcanti/dev/oracle-loop`, mirrored at
<https://github.com/fcavalcantirj/oracle-loop>. Its branch
`dasbrow/build-the-transform-prompt-parse-core-fo-20260817-005512` (tip
`3c67984`) carries the engine plus the ratified v2 oracle map (`b932e38`) and
the v2 SPEC (`3c67984` itself); `agent/colibrino-v2-luos-qualification`
(`84b5bea`) adds `docs/10` and the contributor guide and revises the map, SPEC,
README, and STATE; `agent/colibrino-v2-round-one-corrections` (`c7a82ac`, stacked
on `84b5bea`) holds the round-one corrections. The relevant files
are `AGENTS.md`, `docs/09-colibrino-v2-multimodal-accessibility.md`,
`docs/10-colibrino-v2-luos-qualification.md`,
`docs/colibrino-v2-ORACLE-MAP.md`, `docs/colibrino-v2-SPEC.md`, and
`engine/README.md`.

Oracle Loop may propose code only inside the authority granted by its map. It
may not alter immutable traces, tests, feel constants, board glue, HID/BLE
output, or physical acceptance evidence. Its three engine branches and
Colibrino documents remain branch-only until the user gives the repository's
exact merge grant. A sync or push never implies a merge grant.

## Evidence reviewed

Luos Engine 3.1.0 is an MIT-licensed ANSI C runtime organized around nodes,
services, messages, and optional physical transports. A single MCU can use
local service delivery without Robus or another external network. Messages use
a seven-byte header and a configurable payload whose default maximum is 128
bytes. Core storage is statically bounded through values such as
`MAX_LOCAL_SERVICE_NUMBER`, `MAX_MSG_NB`, and `MSG_BUFFER_SIZE`, and
`Luos_Loop()` cooperatively dispatches work.

The upstream commit `f1af47bdd760ce7038fbb396d1d203c8c2723464` passed all 121
upstream native Unity cases on this Mac. A temporary, no-upload build also
compiled Luos 3.1.0 for `esp32-s3-devkitc-1` with Colibrino's
`espressif32@6.12.0` and Arduino-ESP32 2.0.17 toolchain. That reference example
reported 22,104 bytes of RAM and 282,689 bytes of flash, but those figures
include its example and Robus and are not an integration budget.

The ESP32-S3 compile proves source compatibility only. The Robus build emitted
an incompatible timer-callback warning, an integer-to-pointer warning, and a
linker warning for a global `ctx` symbol colliding with an ESP32 Wi-Fi library
symbol. Colibrino does not need Robus on its first single-board prototype.

## Reasons for the qualification gate

The official tree has ESP32 and ESP32-C3 examples but no explicit ESP32-S3 or
StickS3 target. Its GitHub workflow comments the whole ESP32 example directory
out of the build matrix. Open issue 423 records that ESP builds were removed
from CI, and open issue 464 records watchdog resets in the LED and button
examples. In that issue, a maintainer and reporter found that a single-node
build without the network avoided the reset.

The ESP32 HAL currently implements timing but leaves general IRQ control,
persistent Luos metadata, node-ID storage, reboot, and flash operations empty.
Its default `MSGALLOC_MUTEX_*` and `LUOS_MUTEX_*` hooks are also empty. That can
be acceptable only if one task owns every Luos call and no callback crosses
cores or interrupt context. It is not an acceptable undocumented assumption on
a dual-core ESP32-S3 running USB, OTA, IMU, and later audio tasks.

A Luos service callback is stateful orchestration, not automatically a pure
function. Putting blink DSP inside a callback would make host oracles weaker,
not stronger. The detector and policy reducer therefore keep ordinary typed
function APIs; the Luos-facing service only translates messages to and from
those APIs.

## From-scratch component boundary

The first implementation units are pure domain components:

`imu-motion` converts timestamped BMI270 samples into bounded pointer deltas.
It is the single round-one motion unit and subsumes the Oracle map's original
`imu-fusion` + `gesture` split; round one keeps the rate-based mapping proven in
`sticks3` (`MotionController` is the differential reference) and defers Mahony
or orientation fusion. In the first v2 scaffold it is contract-only (types and
prototypes); its implementation and differential test are a later commit.

`blink-dsp` converts immutable labeled traces or live samples into candidate
impulse events (`IMPULSE` with duration, or `CANCEL` on head motion, an
overlong hold, or the first-sample quiet gate) with confidence and timing
fields. It ends at impulses and never emits a click.

`blink-code` consumes the `blink-dsp` event stream and produces a click
candidate only for the temporal code double (300-700 ms), pause (800-1400 ms),
double, with a 1500 ms click refractory; single, incomplete, and evenly spaced
four-blink inputs never produce a candidate. A thin blink pipeline composes the
two and reproduces the v1 `ImuBlinkDetector` sample-for-sample. `blink-code` is
recorded in the Oracle map as a PROPOSED row pending owner ratification.

`access-intent` is the only authority that can accept a candidate action. It
enforces arming, freshness, cooldown, producer health, transport health, and
mutual exclusion, and defaults to no action plus release-all.

`profile` validates versioned configuration before atomically publishing it.

`usb-hid`, future `ble-hid`, sensors, speech, switches, and Apple-facing
integration are adapters. They never bypass `access-intent`.

Every event contract must have a fixed maximum size and include a monotonic
timestamp, sequence number, producer identity, validity or confidence, and
expiry. Safety decisions use fixed IDs and typed commands rather than dynamic
aliases or discovery. The release-all path remains a direct, synchronous board
operation even if ordinary diagnostics use Luos.

If Luos is enabled, one dedicated task owns `Luos_Init()`, `Luos_Loop()`, and
all send/read calls. The first deployment is localhost-only: no Robus, topology
detection, remote update, WebSocket gate, or external physical network. A
small internal fixed-capacity event bus remains a build-time fallback behind
the same contracts.

## Luos qualification task

The spike is diagnostic-only and cannot emit HID. It must use the exact
StickS3 target and pinned production framework, create the planned local
services without Robus, and retain a no-Luos build for comparison.

It passes only after recording incremental flash and RAM cost; proving one-task
ownership or implementing real mutex hooks; preserving 100–200 Hz BMI270 sample
deadlines; and completing sustained queue, timeout, overflow, ordering, and
watchdog tests with no reset, stale delivery, or silent drop. Queue saturation
must become an explicit health fault that suppresses action. USB CDC, composite
HID lock/release, authenticated OTA maintenance mode, and later audio load must
be exercised independently and together on hardware.

Only after the spike passes may Luos orchestrate non-HID services. Moving the
authorization reducer or release-all path behind Luos requires a separate
safety decision and fault-injection evidence. If the spike fails, preserve the
service contracts and use the internal bounded bus; no DSP, oracle, or fixture
work is discarded.

## Immediate work order

First, capture and label fresh StickS3 BMI270 traces with the existing guarded
firmware and the monitor-only tooling in `sticks3/tools/`, following
`docs/V2_TRACE_CAPTURE_PROTOCOL.md` (multi-run matrix inside the fixed
6/15/12 s guided stages; every deliberate gesture only inside `BLINK_FIRMLY`;
no upload required). Second, keep the pure `blink-dsp`, `blink-code`,
`access-intent`, and `profile` units with their synthetic, differential, and
negative-authorization tests on the branch-only `v2/` scaffold; promote cleared
fixtures so the golden suites become the oracle. Third, run the diagnostic-only
local Luos spike only after those suites are green on real fixtures. Voice
remains a separate round-two producer and must not delay the head-motion and
blink evidence loop.

## Primary sources

Luos Engine source and release: <https://github.com/Luos-io/luos_engine>

Luos basics: <https://www.luos.io/docs/luos-technology/basics>

Luos services: <https://www.luos.io/docs/luos-technology/services>

Luos messages: <https://www.luos.io/docs/luos-technology/messages>

Luos physical transports: <https://www.luos.io/docs/luos-technology/phy>

ESP32 CI gap: <https://github.com/Luos-io/luos_engine/issues/423>

ESP32 watchdog-reset report: <https://github.com/Luos-io/luos_engine/issues/464>
