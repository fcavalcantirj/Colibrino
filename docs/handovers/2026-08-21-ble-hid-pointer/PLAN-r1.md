---
slug: ble-hid-pointer
round: 1
builder_session: Codex Builder session, 2026-08-21 — baseline and BLE stack researched before planning
---

# PLAN-r1 — BLE HID pointer: the cable-free mouse build

## Blocking constraints, restated in the Builder's own words

1. **Hardware deployment stays human-gated.** A build, test, Bluetooth scan,
   monitor, or prepared payload never authorizes an upload. Every OTA needs
   Felipe's explicit go for that exact attempt, given in the moment after the
   target is identified. The only OTA target is the saved
   `sticks3-ptt.local` identity behind its ARP-MAC guard;
   `bedside-countdown-s3` is never a target. Cable flashing is out unless OTA
   is demonstrated impossible and Felipe separately authorizes the exact port
   and device.
2. **One fail-closed safety identity covers USB and BLE.** Boot is locked.
   Pairing, connection, and reconnection never arm output. Pointer or button
   reports require MOUSE mode, a safe current-boot IMU calibration, a ready
   authorized transport, and the existing deliberate two-second Button A
   arming hold. A disconnect, failed authentication, report failure, IMU fault
   or invalid timing, transport change, mode exit, OTA start, or explicit lock
   clears pending movement, releases every mouse button on both transports as
   far as the live links allow, resets motion state, and disarms. No automatic
   failover may preserve the armed state.
3. **BLE is bonded, encrypted, and deliberately discoverable.** Just Works is
   acceptable for this no-keyboard/no-display mouse, but a connection is not a
   report-capable connection until encryption and a persisted bond are both
   confirmed. Advertising is permitted only inside a deliberate bounded
   physical window, never continuously. An existing bond admits only its
   stored peer; clearing it and opening public re-pairing needs a separate,
   visibly deliberate physical flow approved by Felipe.
4. **The telemetry mux is frozen without a new review.** Its TCP socket remains
   output-only; inbound bytes continue to be discarded. The nonblocking raw
   `send(..., MSG_DONTWAIT)` drain, explicit attachment flag, drop accounting,
   and `incoming.fd() >= 0` accept gate stay intact. Neither
   `WiFiClient::write` nor `WiFiClient::connected()` may enter that path.
5. **Master's tip is always green.** Development happens on a topic branch.
   Master can move only by fast-forwarding a state that retains all thirteen
   existing native cases, passes every new native case, passes all 29 host-tool
   cases, builds production both with and without the ignored secrets header,
   and passes Wokwi when new portable safety logic is included there.
6. **Oracle Loop and v2 are outside this feature.** No Oracle merge, rebase,
   squash, rewrite, generated change, or documentation edit is scheduled.
   `agent/v2-core-scaffold` and everything under `v2/` remain untouched.
7. **USB HID remains a first-class path.** BLE is additive. When USB is the
   selected output, the same `USBHIDMouse` calls, signed-byte bounds,
   calibration, physical arming, and motion feel remain. The implementation
   must never duplicate one delta onto USB and BLE unless Felipe explicitly
   chooses that policy after reviewing the risk; this plan recommends against
   simultaneous delivery.
8. **Evidence labels remain literal.** Compile/unit/simulator results are
   `[TEST]`; behavior observed on the identified StickS3 and Mac is `[REAL]`;
   everything else stays `[UNVERIFIED]`. No hardware, battery, coexistence,
   pairing, reconnect, USB-regression, or full Stage-2 claim is promoted from a
   software-only result.

## Step 0 verification and handover discrepancies

Performed before writing this plan. No device network request, Bluetooth scan,
monitor, upload, or flash occurred.

| Check | Verified result on 2026-08-21 |
| --- | --- |
| Colibrino local / refreshed remote head | `d6594f4e8c6ea29d8821d94ff384492855d67b4e` / same |
| Working tree before this plan | clean, `master...origin/master` |
| Known USB device port | `/dev/cu.usbmodem1101` absent; device otherwise untouched |
| Installed device image from retained evidence | `[REAL]` `254fb7e`; not contacted or re-verified in this planning turn |
| Native portable suite | `[TEST]` 13/13 pass |
| Host capture-tool suite | `[TEST]` 29/29 pass |
| Production build with secrets | `[TEST]` build id `d6594f4`; RAM 98,244 B / 327,680 B (30.0%); flash 1,063,597 B / 3,342,336 B (31.8%) |
| Clean production build without secrets | `[TEST]` build id `d6594f4`; RAM 35,536 B (10.8%); flash 589,137 B (17.6%) |
| Installed framework / coexistence config | Arduino-ESP32 2.0.17 on ESP-IDF 4.4.x; installed ESP32-S3 sdkconfig has software Wi-Fi/Bluetooth coexistence enabled |

The handover's only material baseline discrepancy is expected: its stated
master `a3c98de` predates the handover commit itself. Current master and
`origin/master` are `d6594f4` (`Add BLE HID pointer build handover`). The
firmware sources and the reported image sizes are unchanged; a new build embeds
the newer tree id. The ignored secrets header was temporarily set aside for the
clean no-secrets build and was automatically restored without printing it.

The first sandboxed invocations of PlatformIO and the three localhost socket
tests were denied access to the existing home cache / loopback socket. They
were rerun with the required local permissions and passed in full; these were
environment denials, not product test failures.

## BLE stack research and decision

Context7 was used for the NimBLE-Arduino and Arduino-ESP32 API pass, then the
facts were checked against the projects' primary source, releases, and the
installed 2.0.17 framework.

| Candidate | Evidence | Decision |
| --- | --- | --- |
| **NimBLE-Arduino 2.5.1, direct `NimBLEHIDDevice` use** | Latest stable release on 2026-07-30; active repository; official CI includes generic ESP32-S3 PlatformIO builds; its 2.3.2 release explicitly fixed Arduino cores using IDF 4.x. It exposes bounded advertising, stop, authentication callbacks, encryption/bond inspection, persistent bond enumeration/deletion, whitelist filters, and encrypted HID input-report characteristics. Upstream describes roughly 50% lower flash and about 100 KB lower RAM than equivalent Bluedroid use. | **Primary, exactly pinned at 2.5.1.** The actual Colibrino build on Arduino-ESP32 2.0.17 is still the first implementation gate; upstream CI does not replace it. Disable unused central/observer roles and cap bonds/connections at one through project build flags, without editing the dependency. |
| **Arduino-ESP32 2.0.17 built-in `BLEHIDDevice` / Bluedroid** | Already pinned with the core, no extra dependency. The installed HID implementation marks input report and CCCD access encrypted and `BLESecurity` supports bonding. Espressif recommends NimBLE for BLE-only work because it consumes less runtime memory and code. | **Fallback only.** Use it directly behind the identical adapter if pinned NimBLE fails to compile, pair, reconnect, or coexist on this exact board. Re-run every budget and hardware gate; do not silently switch stacks. |
| **T-vK ESP32-BLE-Mouse 0.3.1** | Last release was 2020-10-20; source starts an always-advertising Bluedroid server in a separate 20 KB task, has an empty `end()`, and offers no project-owned bounded pairing / peer-filter state machine. Its README warns macOS behavior can be unstable. | **Rejected.** Its convenient Mouse-like API does not satisfy this product's advertising, ownership, lifecycle, or safety requirements. |

Primary sources:

- [NimBLE-Arduino 2.5.1 release](https://github.com/h2zero/NimBLE-Arduino/releases/tag/2.5.1), [resource statement](https://raw.githubusercontent.com/h2zero/NimBLE-Arduino/2.5.1/library.properties), [HID implementation](https://raw.githubusercontent.com/h2zero/NimBLE-Arduino/2.5.1/src/NimBLEHIDDevice.cpp), and [API documentation](https://h2zero.github.io/NimBLE-Arduino/).
- [Espressif ESP32-S3 BLE stack overview](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/bluetooth/index.html), [Wi-Fi/BLE RF coexistence](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/coexist.html), and [RAM measurement guidance](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/performance/ram-usage.html).
- [Arduino-ESP32 2.0.17 built-in HID source](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/BLE/src/BLEHIDDevice.cpp) and [T-vK implementation](https://github.com/T-vK/ESP32-BLE-Mouse/blob/master/BleMouse.cpp).

If NimBLE fails on hardware, implementation stops and reports the exact failure.
The Architect and Felipe decide whether to authorize a second implementation
round using built-in Bluedroid. Updating Arduino-ESP32, changing the partition
table, or importing a different mouse wrapper is not an implicit fallback.

## Proposed architecture

### One output choke point

Replace direct application calls to the global `USBHIDMouse` with a small,
board-owned output router. Portable motion math still returns only bounded
`PointerDelta`; it never gains Arduino, USB, BLE, Wi-Fi, or FreeRTOS knowledge.

The intended flow is:

```text
BMI270 -> existing MotionController -> MouseOutputRouter::move()
                                      -> exactly one selected transport
camera click -------------------------------------------------> macOS (off-device)
IMU/optical click candidates -> existing validation gates -> MouseOutputRouter::click()
```

The router owns no producer authority. It accepts a report only when all of
these are true at that instant: `mode == MOUSE`, `mouse_armed`, current-boot
motion calibration valid, IMU timing fresh, no OTA/fault latch, and one
selected output transport ready. Pairing or connecting cannot satisfy the
arming requirement by itself.

Implementation boundaries:

- A self-contained C++17 `MouseOutputPolicy` holds the fail-closed state,
  selected transport enum, bounded BLE cadence/accumulator, and transition
  reasons. It uses no Arduino APIs and gets native tests plus deterministic
  Wokwi checks.
- A board adapter keeps the existing `USBHIDMouse` instance registered before
  `USB.begin()`. When USB is selected it calls the same `move`, `click`, and
  `release` APIs with the same signed-byte bounds.
- A `BleMouseTransport` owns NimBLE server/HID objects, the relative-mouse HID
  descriptor, one input report, one connection, one persisted bond, bounded
  advertising, and local button state. It never authorizes output.
- BLE and USB event callbacks only publish atomic connection/fault facts. The
  cooperative loop consumes them before button and IMU processing, then makes
  all safety transitions and HID decisions in the application owner task.
  Authentication failure is allowed to send a zero-button report before
  disconnect; a completed disconnect zeroes local state even though no report
  can then reach the peer.
- All board-level report calls move behind `MouseOutputRouter::move`,
  `click`, and `releaseAll`. `updateImu`, `updateIr`, arming, mode transitions,
  and `lockForOta` no longer address a transport directly.

### Safety transitions

Introduce one `forceLock(reason)` operation. It is idempotent and always:

1. clears `mouse_armed` before doing anything else;
2. clears any BLE rate-limit accumulator, report queue, and all local button
   bits so no stale movement or click can survive;
3. sends best-effort all-buttons-up to both currently live transports;
4. resets `MotionController` and both runtime detector temporal states; and
5. emits one rate-limited diagnostic reason without blocking recovery.

Call it for physical lock, MOUSE-mode exit, OTA start, BLE disconnect,
authentication/bond/encryption failure, advertising or notify failure, USB
unmount/suspend where the installed API reports loss, selected-transport
change, missing gyro samples while armed, or a measured IMU `dt` outside the
existing valid window (over 200 ms included). Reconnection or USB return only
changes readiness; it never restores `mouse_armed` or replays a delta.

The BLE adapter has no unbounded report queue. It targets at most one report
per 8-10 ms, combines only deltas observed in that short current window,
clamps the resulting signed report to the existing 60-pixel safety bound, and
drops rather than carries overflow into a later window. A stopped head must
not produce delayed cursor motion. Notification failure is a lock-worthy
transport fault, never a retry backlog.

### Transport selection — Felipe decision required

**Recommended default: wired-preferred, never simultaneous.** If TinyUSB is
mounted on a data host, USB is the sole HID output. Otherwise one bonded,
encrypted BLE connection may be selected. A USB mount/unmount or BLE
connect/disconnect that changes the selected transport first locks and releases
both sides; Felipe must perform the two-second arming hold again.

This preserves a deterministic recovery/debug path, avoids double cursor
movement when the Mac sees the same physical device over two HID transports,
and treats a cable change as a safety transition. A power-only USB cable does
not mount TinyUSB and therefore does not steal selection from BLE.

Alternatives for Felipe through the Architect:

- BLE-preferred keeps cable-free control even when USB data is present, but
  makes the proven USB mouse appear inert while BLE is connected.
- Simultaneous USB+BLE delivery can double every delta on one Mac or control
  two hosts from one head motion. It is not recommended and will not be
  implemented without an explicit decision plus revised tests.

### Pairing and reconnect UX — Felipe decision required

**Recommended default:** initialize BLE at boot but do not advertise. In
locked MOUSE mode, when no HID transport is ready, the existing two-second
Button A hold opens a BLE window instead of arming (there is nothing safe to
arm). Pairing never produces output, and after a secure connection the screen
still says `OUTPUT: LOCKED`; a second two-second hold is required to arm.
Entering MOUSE latches any Button A press already in progress, so the hold used
to change mode cannot carry into pairing or arming; the button must be fully
released before either action becomes eligible.

- **No stored bond:** open public connectable advertising for 60 seconds.
  Accept one peer only; require Just Works Secure Connections with bonding,
  then require `isEncrypted()` and `isBonded()` in the authentication-complete
  callback before marking BLE ready. Failure disconnects, locks, and remains
  non-reporting.
- **One stored bond:** load its identity into the NimBLE whitelist and open a
  30-second connect-only-whitelisted window. Unknown peers cannot connect.
- **Timeout:** stop advertising, remain locked, show `BLE: IDLE`, and require a
  fresh physical hold. Disconnect does not auto-advertise.
- **Deliberate re-pair:** during the bonded reconnect window, the display offers
  `hold BLUE 5s: forget + pair`. That second continuous hold deletes the sole
  bond, clears the whitelist, releases/disarms again, and opens one new
  60-second public pairing window. It cannot occur while output is armed or a
  transport is ready.
- **Screen:** MOUSE mode always shows output lock, selected HID (`NONE`, `USB`,
  `BLE`), BLE state (`IDLE`, `PAIR 42s`, `RECONNECT 18s`, `AUTH`, `SECURE`, or
  `FAULT`), and the unchanged `blink click: DISABLED` truth unless the separate
  optical current-boot gate is genuinely ready.

The initial recommendation reuses the physically known two-second hold without
creating an ambiguous shorter tap or arming during pairing. The separate
five-second re-pair confirmation prevents accidental bond erasure. The
Architect must take this proposal to Felipe; implementation waits for his
choice.

### Diagnostics without changing the mux contract

Append stable tail fields to the existing status line:

```text
ble=DISABLED|IDLE|PAIRING|RECONNECT|AUTH|SECURE|FAULT,hid=NONE|USB|BLE,bonded=0|1
```

Existing parsers already accept trailing fields. Emit bounded transition
events for advertising start/timeout, connection, secure bond, disconnect,
report failure, bond clear, selected transport, and forced-lock reason. Never
include the peer address or other new personal identifier in committed logs.

For the required coexistence measurement, `main.cpp` (not `TelemetryMux` and
not capture tooling) will emit a distinct high-rate `COEX_IMU` record only
while all three conditions are true: TCP client attached, BLE is the selected
secure transport, and mouse output is armed. The window is capped at 30
seconds per physical arming and emits explicit start/end counters. This makes
the actual 170 Hz sensor/telemetry load overlap real BLE reports without adding
a host command or changing the mux. Outside this deliberately observed state
there is no extra stream. If the Architect rejects retaining this bounded
diagnostic, use a separately reviewed test-build flag and require a second
authorized OTA of the instrumentation-free image before acceptance.

## Resource budget

The measured secrets-enabled baseline is 98,244 B static RAM and 1,063,597 B
application flash. The no-secrets baseline is 35,536 B / 589,137 B. BLE is not
conditioned on OTA credentials, so both variants will include NimBLE; only
Wi-Fi, telemetry TCP, and OTA remain controlled by the ignored header.

Before the first feature build, record this prediction:

- NimBLE + one HID server/connection/bond: approximately **+20-60 KiB reported
  RAM**, **+180-350 KiB application flash**, and **+40-100 KiB runtime internal
  heap consumption** versus each corresponding baseline. These are planning
  bands, not claims; PlatformIO's report does not include every runtime
  allocation.
- Expected secrets-enabled result stays well below 50% of the 3,342,336 B app
  partition and 55% of reported 327,680 B RAM.

Measure and report after the first build:

1. exact PlatformIO RAM and flash for clean with-secrets and clean
   without-secrets builds, including absolute and baseline deltas;
2. internal free heap and largest free internal 8-bit block at boot, after BLE
   initialization, during advertising, after secure connection, during armed
   BLE + TCP coexistence, and after disconnect; and
3. BLE host-task stack high-water mark if the pinned API exposes it without
   modifying the library.

Stop before hardware deployment if linking exceeds 50% of the app partition,
reported RAM exceeds 55%, minimum free internal heap falls below 80 KiB,
largest free internal block falls below 40 KiB, a task has less than 1 KiB
stack headroom, allocation/init fails, or heap/stack values trend downward in
a ten-minute reconnect/coexistence soak. These are conservative engineering
stop thresholds chosen to retain fault and future-feature headroom; crossing
one triggers review rather than memory tuning or a core/partition change.

## Implementation and validation plan

No step below starts until `REVIEW-r1.md` says `APPROVED` and the two
Felipe-reserved UX/transport choices have returned through the Architect.

### Step 1 — branch and pure fail-closed policy

1. Refresh `origin`, confirm master is clean/green, and create
   `agent/ble-hid-pointer` from the exact refreshed tip. If master moved, record
   the discrepancy before editing.
2. Add the self-contained output-policy/router contract and fake transports.
   Keep Arduino, M5Unified, USB, BLE, Wi-Fi, and FreeRTOS out of the portable
   policy.
3. Native cases cover at least: boot locked; pairing/connection cannot arm;
   the mode-entry hold cannot carry into pairing or arming;
   no transport or insecure/unbonded BLE denies reports; only MOUSE + calibrated
   + fresh IMU + armed + selected-ready transport permits one report;
   simultaneous-ready topology selects exactly the Felipe-approved one;
   disconnect, auth/report failure, mode exit, OTA, USB topology change, and
   IMU timeout each force one release-all and lock; reconnect stays locked;
   accumulated BLE motion is bounded and cleared on stop/lock; timestamp
   rollover remains safe.
4. Include the policy in the Wokwi build and add deterministic locked,
   selected-transport, disconnect/reconnect, and no-stale-output checks.
5. First commit: `Add fail-closed HID output policy` only after native and
   Wokwi are green.

### Step 2 — board transports and UI

1. Pin `h2zero/NimBLE-Arduino@2.5.1`; disable unused central/observer roles,
   set maximum connections/bonds to one, and keep NVS bond persistence and
   Secure Connections enabled through project flags supported by the pinned
   library.
2. Add USB and BLE adapters plus the single router integration in `main.cpp`.
   Register USB HID before `USB.begin()` exactly as today. BLE initialization
   failure leaves BLE `FAULT`, releases/disarms, and leaves USB available for a
   fresh deliberate arming after the fault is stable.
3. Implement the Felipe-approved selection and pairing state machines,
   authentication checks, whitelist, bounded advertising, re-pair, callback
   handoff, report cadence, and forced-lock transitions. Pairing and BLE
   callbacks never send pointer deltas.
4. Route both presently dormant firmware click paths through the same choke
   point without changing their predicates or thresholds. Keep
   `imu_blink_validated` closed and do not tune the IMU detector.
5. Extend STATUS/display and add the bounded `COEX_IMU` observation described
   above. Do not edit `telemetry_mux.*`, `sticks3/tools/**`, or capture plans.
6. Second commit: `Add bounded bonded BLE pointer transport` after review and
   all software gates.

### Step 3 — software gates and measured budget

Run from `sticks3/` and retain concise results:

```sh
/Users/fcavalcanti/.platformio/penv/bin/platformio test -e native
/Users/fcavalcanti/miniconda3/bin/python3 -m unittest tools/tests/test_capture_tools.py
/Users/fcavalcanti/.platformio/penv/bin/platformio run -e m5stack-sticks3
./scripts/run_wokwi.sh
python3 -m json.tool PORT_PLAN.json >/dev/null
```

Also perform a genuinely clean no-secrets production build in an isolated
temporary build directory, restoring the ignored header without printing it.
Require all original thirteen native cases plus every new case, tooling 29/29,
Wokwi `COLIBRINO_SIM_PASS` plus the new safety checks, both production builds,
the resource thresholds above, shell syntax for untouched and affected
scripts, ignored `.env`/`.pio`/`.device-backups`, and a focused final diff.

No upload follows automatically from any green result.

### Step 4 — OTA rollout, exact order

1. Build the green topic-branch commit and note the printed build id and exact
   RAM/flash sizes.
2. Copy that exact `.pio/build/m5stack-sticks3/firmware.bin` to ignored
   `.device-backups/firmware-upload-<build-id>.bin`; record its SHA-256 without
   staging the payload.
3. Announce readiness with `/usr/bin/say`, naming Colibrino and that BLE pointer
   firmware is ready.
4. **WAIT** for Felipe to power on the identified StickS3 and explicitly
   authorize this upload attempt. Do not ping, pair, or run the uploader while
   waiting.
5. On that go only, run `./scripts/upload_ota.sh`. Its hostname and ARP-MAC
   guard must match the saved Colibrino unit; any mismatch is a refusal, not a
   reason to bypass the script.
6. Post-reboot over the sanctioned TCP path, require greeting build id equality
   and STATUS showing `armed=0`, `ir=0`, `calibrated=1`, `imu_blink=0`,
   `ota=READY`, `tele=1`, expected `build=`, `ble=IDLE`, `hid=NONE`, and a sane
   battery. Pairing must not have started by itself.
7. Announce and report the result. On upload failure, identity discrepancy,
   missing greeting, wrong build, unsafe STATUS, crash/reboot, or missing OTA:
   say failure, preserve evidence, **STOP**, and do not retry without a new
   explicit Felipe go. Rollback or a second-stack build is a separate decision.

### Step 5 — physical BLE, safety, USB, and coexistence acceptance

Felipe performs every Button A hold and host pairing action.

1. **Pairing:** enter the approved bounded window; confirm the countdown and
   discoverability stop at timeout. Pair the Mac; require firmware evidence of
   encryption + persisted bond, advertising stopped, `ble=SECURE`, and
   `OUTPUT: LOCKED`. An unknown peer is rejected in a bonded reconnect window.
   Exercise the approved two-step re-pair flow once if Felipe agrees.
2. **Locked negative:** with BLE secure, move the device/head for at least 30
   seconds before arming. The host receives no cursor or button events.
3. **BLE pointer:** after stationary calibration, enter MOUSE and hold Button A
   two seconds. Verify bounded left/right/up/down motion and stationary
   suppression. Stop moving abruptly; no delayed movement may follow.
4. **Physical lock:** hold two seconds to lock, then move the device. Require
   no host events and local/STATUS locked state.
5. **Disconnect/reconnect:** while armed, disable the Mac Bluetooth link. The
   device must release/disarm and stop reporting. Reconnection requires a new
   physical advertising window and remains locked until another two-second
   arm. Repeat for an authentication failure or forgotten host if practical;
   no automatic action is allowed.
6. **USB coexistence:** with BLE secure, attach USB data. Under the recommended
   wired-preferred policy the topology change locks both, selects USB, and
   requires re-arm; one head motion moves the cursor once, not twice. Verify
   CDC plus USB HID on the telemetry-era code. Unplugging data USB locks,
   selects BLE only after it is secure, and again requires re-arm. Adapt the
   expected selected transport if Felipe chooses BLE-preferred, but never
   relax the lock-on-switch gate.
7. **TCP + BLE RF coexistence:** connect the output-only TCP observer while BLE
   is selected and armed, then run the bounded 30-second `COEX_IMU` window with
   real pointer reports. Compare with the recorded ~170 Hz / zero-drop
   baseline. Pass requires: effective device-sample/log rate within 5%;
   `DROPPED` delta exactly 0; zero BLE report failures; no IMU gap over 200 ms;
   p50/p95/p99 device-time gaps comparable to baseline (investigate any p99
   regression over 5 ms or a worse maximum); no stale movement after the head
   stops; OTA remains `READY`. A ten-minute connected/armed bench soak must not
   reset, drift its heap thresholds, stick a button, or accumulate report
   failures.
8. Record battery at start/end and report the observation without turning one
   bench interval into a runtime claim or premature optimization.

Any failure locks output and stops progression. No threshold relaxation,
automatic retry, stack swap, or second OTA occurs inside the failed run.

### Step 6 — complete wireless Stage 2 with camera click

After the BLE pointer gates pass, Felipe runs the actual architecture:

1. Mac Head Pointer remains off; Alternative Pointer Actions keeps the already
   validated `Eye Blink -> Left Click`, calibrated at the lightest sensitivity
   that produces no natural-blink false fire (currently Exaggerated).
2. StickS3 connects by bonded BLE, stays locked through placement, then Felipe
   enters MOUSE and performs the two-second arm.
3. Head motion targets several harmless controls; a deliberate long firm eye
   closure performs the click. Include a stationary pointer control and a
   natural-blink control.
4. Felipe gives the usability verdict. Only an observed pass becomes `[REAL]`
   evidence for “wireless head pointer + camera click.” Mirrored-sunglasses and
   camera limitations remain known separate residuals.

### Step 7 — documentation, integration, and release

Only after Step 5 hardware acceptance passes:

1. Add `PORT_PLAN.json` task `T16` for bonded bounded BLE HID, with separate
   `[TEST]` software evidence and `[REAL]` exact-device/Mac observations;
   validate JSON.
2. Add a top `PROJECT_KNOWLEDGE.md` changelog entry recording the code commit,
   stack/version choice, exact build sizes and deltas, pairing/transport policy,
   coexistence metrics, failure behavior, battery observation, and what remains
   unverified.
3. Update the root README project-status row and architecture, plus
   `sticks3/README.md` operation/status/build guidance. State that camera blink
   clicking is off-device, the IMU click channel remains closed, BLE is
   hardware-validated only if it actually passed, and USB remains supported.
4. Re-run every Step 3 gate, inspect the final diff, confirm ignored artifacts
   and secrets are absent, fetch remote refs, and ensure the topic branch
   contains current `origin/master`. If master advanced, integrate it without
   rewriting published history and rerun all gates.
5. Commit the hardware evidence/docs separately from the already tested code.
   Fast-forward master and push only after the topic tip is green and Felipe's
   normal repository publishing authorization is in scope. Never force-push.

If software compiles but physical acceptance fails, keep the work branch-only,
record the failure truthfully in the handover/evidence record as authorized,
and do not update README or `PORT_PLAN` to a passing status.

## Acceptance checklist mapping

1. All eight blocking constraints are restated first.
2. Stack choice is NimBLE-Arduino 2.5.1 with direct built-in Bluedroid fallback;
   T-vK is explicitly rejected with maintenance/lifecycle/security reasons.
3. One `mouse_armed` choke point, both adapters, both-side release, callback
   handoff, disconnect/mode/IMU/OTA faults, STATUS `ble=`, and MOUSE display are
   specified.
4. Bounded deliberate pairing, bonded+encrypted acceptance, whitelisted
   reconnect, deliberate bond clear, timeout, and screen UX are proposed for
   Felipe.
5. The 30-second armed BLE + TCP overlap has explicit baseline comparisons and
   pass/fail criteria without changing `TelemetryMux` or capture tooling.
6. Predicted and measured RAM/flash/runtime-heap budgets plus stop thresholds
   are explicit for both secrets variants.
7. All original native/tooling gates, new portable safety tests, Wokwi, full
   pair/lock/disconnect/reconnect/USB bench sequence, and Felipe's wireless
   camera-click Stage 2 are scheduled.
8. Branch, commit cadence, exact OTA order, post-reboot `ble=` check, and
   stop/no-retry failure branch are explicit.
9. `T16`, `PROJECT_KNOWLEDGE`, root README, and StickS3 README change only after
   real hardware acceptance.
10. Oracle Loop, home automations, `v2/`, `sticks3/tools/**`, capture plans, the
    telemetry mux, legacy AVR, and IMU feel/classifier constants are outside
    the edit set.

## Concerns and questions for the Architect

1. **Felipe decision — pairing UX:** approve the recommended locked-MOUSE/no-
   transport two-second window and the second five-second bonded re-pair hold,
   or return a different physical gesture before coding.
2. **Felipe decision — transport priority:** approve wired-preferred with
   lock-on-every-switch (recommended), or choose BLE-preferred. Simultaneous
   reports are explicitly discouraged.
3. **Review decision — coexistence observability:** approve the bounded
   `COEX_IMU` record in ordinary firmware, or require a test-build flag and the
   resulting second authorized OTA before final acceptance. A low-rate STATUS
   stream cannot test the handover's required ~170 Hz TCP overlap while BLE HID
   is genuinely armed.
4. **MEDIUM — asynchronous loss:** after RF loss a BLE release report cannot
   reach the host. The design prevents held output, clears local state,
   disconnects, and requires the physical re-arm; physical acceptance must
   verify macOS releases any host-side button state on disconnect. A failure is
   a release blocker, not an accepted residual.
5. **MEDIUM — 100-170 Hz mismatch:** BLE connection cadence is slower than the
   BMI270 stream. The bounded current-window accumulator preserves normal
   movement while explicitly dropping overflow instead of creating delayed
   movement; feel and stop response remain hardware gates.
6. **LOW — upstream compatibility evidence:** NimBLE 2.5.1 is current and its
   CI covers ESP32-S3 PlatformIO, while release 2.3.2 specifically repaired
   IDF 4.x builds. The exact 2.5.1 + Arduino-ESP32 2.0.17 + StickS3 combination
   remains `[UNVERIFIED]` until Step 3 compiles and Step 5 runs; no source calls
   it proven early.
