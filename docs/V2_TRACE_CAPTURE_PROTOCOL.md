# V2 trace capture protocol (StickS3 BMI270, round one)

This document is the physical and host-side protocol for collecting the fresh
worn StickS3 traces that become the immutable v2 fixtures for `blink-dsp`.
It is written for the person wearing the device and for the person running the
laptop; on this project both are usually the same person.

Nothing in this document is a hardware validation claim. The host tooling
described here has been exercised on retained logs and on synthetic logs only
(see `sticks3/tools/tests/test_capture_tools.py`). The first live capture
session is itself the validation of the live path.

## 1. Invariant

* Deliberate gestures happen **only inside `BLINK_FIRMLY`**.
* `KEEP_HEAD_STILL` and `MOVE_HEAD` are **genuine controls in every run**:
  "head still, blink normally" and "move your head as when pointing" (or the
  sweep instruction in the HS runs). Never perform a hard blink, a coded pattern
  or a cued gesture in a control stage.
* The firmware decides nothing about acceptance. Its `RESULT,IMU_BLINK` line is
  recorded and compared with the host replay (parity), but worn acceptance is a
  host-side decision on designated run ids (section 7).
* The device CDC channel is write-only from the firmware side. The host tools
  never send anything to the device; Button A is the only input.

## 2. Per-session caps

| Cap | Value |
| --- | --- |
| Runs per session | at most 8 probe runs |
| Firm blinks per session | at most 50 |
| Wall time per session | at most 20 minutes |
| Eye rest between blocks | at least 45 seconds |
| Break and remount | exactly one per session (Session A: after U; Session B: not required, but rest still applies) |
| Stop conditions | fatigue at 3/5 or worse, any discomfort, watering eyes, headache, or any alarm from the host tool |

The fatigue score (1-5) is typed into the host tool at `q`; it lands in
`session.json`. A session with fatigue 3 or worse is not used for acceptance.

The firmware probe itself is fixed: 3 s prepare, 6 s `KEEP_HEAD_STILL`,
3 s prepare, 15 s `BLINK_FIRMLY`, 3 s prepare, 12 s `MOVE_HEAD`, about
42 seconds per run (`sticks3/src/main.cpp`, `kImuPrepareMs`,
`kImuStillCaptureMs`, `kImuBlinkCaptureMs`, `kImuHeadCaptureMs`).

## 3. Session A (42 firm blinks)

The plan is machine-readable in `sticks3/tools/capture_plans/round1.json`
(`sessions[0]`). Order matters. Cue offsets are relative to the
`EVENT,IMU_PROBE_STAGE,<STAGE>` line of the named stage as received by the
host.

| Order | Run id | Kind | `BLINK_FIRMLY` instruction | Cues (`BLINK_FIRMLY` unless noted) | Firm blinks | Expected still / blink / head |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | BOOT | marker, no probe | Plug in, hands off, capture at least 60 s of `STATUS` before touching Button A | none | 0 | n/a |
| 1 | R0 | `R0_bench` (mount `bench`) | Device rests untouched on the bench; read the display fingerprint (section 6) | none | 0 | 0 / 0 / 0 |
| 2 | V1 | `V_standard` | "Blink twice, pause one second, blink twice. Then repeat once." | none | 8 | 0 / at least 2 / 0 |
| 3 | HB1 | `HB_hard_singles` | "One single hard blink on each beep. Nothing between beeps." | beeps at +0.75 s + 1.5 s * k, k = 0..5 (0.75, 2.25, 3.75, 5.25, 6.75, 8.25 s) | 6 | 0 / 0 / 0 |
| 4 | U | `U_uniform_four` | "Four evenly spaced hard blinks, one per beep. Two groups." | group U1 at 0.75, 1.25, 1.75, 2.25 s; group U2 at 6.25, 6.75, 7.25, 7.75 s | 8 | 0 / 0 / 0 |
| - | BREAK_REMOUNT | marker, no probe | Glasses off, at least 45 s eye rest, remount fresh | none | 0 | n/a |
| 5 | V2 | `V_standard` | as V1 | none | 8 | 0 / at least 2 / 0 |
| 6 | C | `C_cued_coded` | "Blink hard on each beep. Two quick, pause, two quick. Three patterns." | groups start at 0.75, 5.0, 9.25 s; each group beeps at +0, +0.5, +1.6, +2.1 s | 12 | 0 / 3 / 0 |
| 7 | HS | `HS_sweeps` | `BLINK_FIRMLY` = keep still, eyes relaxed. `MOVE_HEAD` = fast sweeps: left-right 0-4 s, up-down 4-8 s, diagonal 8-12 s (spoken cues at +0, +4, +8 s in `MOVE_HEAD`) | spoken cues in `MOVE_HEAD` | 0 | 0 / 0 / 0 |

Total: 8 + 6 + 8 + 8 + 12 = 42 firm blinks, 7 probe runs.

## 4. Session B (28 firm blinks)

`round1.json` `sessions[1]`. Same caps, same controls.

| Order | Run id | Kind | `BLINK_FIRMLY` instruction | Cues | Firm blinks | Expected still / blink / head |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | R1 | `R_rest` | "Rest. Eyes relaxed, blink naturally. No deliberate blinks." | none | 0 | 0 / 0 / 0 |
| 2 | SB | `SB_soft` | "Soft ordinary blinks, about one per second. Nothing forceful." | none | 0 | 0 / 0 / 0 |
| 3 | CF2 | `CF2_bumps` | "Tap the frame twice, then adjust the glasses, then swallow." | spoken cues at +0, +5, +10 s | 0 | 0 / 0 / 0 |
| 4 | V3 | `V_standard` | as V1 | none | 8 | 0 / at least 2 / 0 |
| 5 | HB2 | `HB_hard_singles` | as HB1 | beeps at +0.75 s + 1.5 s * k, k = 0..7 | 8 | 0 / 0 / 0 |
| 6 | C2 | `C_cued_coded` | as C | as C | 12 | 0 / 3 / 0 |
| 7 | HS2 | `HS_sweeps` | as HS but slow sweeps | spoken cues in `MOVE_HEAD` | 0 | 0 / 0 / 0 |
| 8 | CF1 | `CF1_confounders` (**private**) | "Chew until the next cue, then talk. No deliberate blinks." | spoken cues at +0 and +7.5 s | 0 | 0 / 0 / 0 |

Total: 8 + 8 + 12 = 28 firm blinks, 8 probe runs (the cap).

`BN_boundary` (near-threshold blinks, timing-boundary patterns) is
intentionally omitted from both sessions. If boundary evidence becomes
necessary it is an optional Session C with its own plan entry, never squeezed
into A or B.

## 5. Host tooling

All host tools live in `sticks3/tools/` and need only the Python standard
library plus `pyserial` for the live USB path (`--tcp`, `--simulate` and the
fixture tool work without it). Run them from `sticks3/`.

**Wireless is the default transport.** A USB cable is a physical anchor on the
~20 g head mount and distorts the blink impulses being measured, so worn
sessions run cable-free over the firmware's read-only TCP telemetry mirror
(port 35533, one client, inbound bytes discarded by the device). Every IMU row
is timestamped on the device before transport, and transport loss is explicit
(`EVENT,TELEMETRY,DROPPED,<total>`), so wireless capture is as trustworthy as
USB was - see the equivalence acceptance in section 5b. The USB path below
remains available for bench diagnostics only.

1. **Discover the device descriptor** (reads USB descriptors only; opens no port):

       python3 tools/capture_session.py --discover

   It prints every serial port with VID/PID/manufacturer/product/serial and,
   for the port that reports manufacturer `Colibrino` and product
   `Colibrino StickS3 Prototype`, the exact line to add to the ignored
   repo-root `.env`:

       COLIBRINO_USB_SERIAL=<serial>

2. **Add that line to `.env`** (repo root, git-ignored). The capture tool
   parses only that key and never prints other values.

3. **Capture with the plan** (wireless, the default for worn sessions):

       python3 tools/capture_session.py --tcp --plan tools/capture_plans/round1.json --plan-session A
       python3 tools/capture_session.py --tcp --plan tools/capture_plans/round1.json --plan-session B

   `--tcp` resolves `sticks3-ptt.local` (override host/port with
   `--tcp HOST[:PORT]` or `COLIBRINO_OTA_HOST` in the ignored `.env`),
   refuses unless the ARP MAC equals `COLIBRINO_OTA_EXPECTED_MAC` (the same
   guard the OTA uploader uses; `bedside-countdown-s3` is refused by
   construction), connects, immediately half-closes its send side
   (`shutdown(SHUT_WR)`) so the host physically cannot send a byte, and
   aborts unless the device greets with `EVENT,TELEMETRY,CONNECTED,<build>`
   within 3 seconds. Liveness: the 1 Hz STATUS stream means 5 seconds of
   silence triggers a guarded reconnect. USB capture (below) needs
   `COLIBRINO_USB_SERIAL`:

       python3 tools/capture_session.py --plan tools/capture_plans/round1.json --plan-session A
       python3 tools/capture_session.py --plan tools/capture_plans/round1.json --plan-session B --wait-for-port

   The tool refuses to open anything unless exactly one port matches
   manufacturer, product and `COLIBRINO_USB_SERIAL` (case-insensitive).
   `--port` only chooses between several verified candidates; it never bypasses
   verification. The port is opened at 115200 baud with DTR and RTS asserted
   (required: the firmware's native TinyUSB CDC drops every write while the
   host holds DTR low, which is why `pio device monitor` sees output), the
   modem lines are never toggled after open, never 1200 baud, and nothing is
   ever written to it.

   Output goes to `sticks3/.device-backups/logs/capture-YYYYMMDDTHHMMSS/`
   (git-ignored): `raw.log` (device bytes verbatim, so
   `replay_imu_capture` reads it directly), `hosttime.tsv`
   (`line_index<TAB>host_monotonic_ns`), `markers.jsonl` (keys, cues,
   instructions, stage transitions, alarms, warnings) and `session.json`
   (identity provenance with the serial redacted to its last four characters,
   boot `STATUS` block, plan used, run list with `valid|invalid|redo` status
   and per-run `cleared: false`, notes, fatigue, alignment estimates and the
   Colibrino `HEAD` commit).

   Keys: `n`/`p` select run, `r` redo (marks the previous attempt invalid),
   `x` invalidate current, space marker, `m` typed note, `q` quit and
   finalize. The status pane shows run id, stage, elapsed time, samples in
   stage, effective Hz and the latest `STATUS` flags. Spoken and printed
   alarms fire on `armed=1`, `mode=MOUSE`, `imu=0`, `calibrated=0` after it was
   1, and on serial disconnect (the tool reconnects with the same identity
   guard). `--no-audio` prints a bell plus the text instead of `afplay`/`say`.

   Rehearse the whole flow first with a retained log:

       python3 tools/capture_session.py --simulate .device-backups/logs/device-monitor-260815-151549.log --no-audio --plan tools/capture_plans/round1.json

   A simulated capture is a rehearsal, not data: `session.json` records
   `mode: simulate` plus the source log's name and SHA-256, and everything
   downstream treats it as such (fixtures named `...-sim`, dated from the
   source log, `provenance.simulated: true`, no cue-derived segments, always
   `review_required`, never promotable, `--accept-runs` prints `REHEARSAL`
   instead of `ACCEPT`).

4. **Turn the capture into fixture candidates**:

       python3 tools/make_trace_fixture.py .device-backups/logs/capture-YYYYMMDDTHHMMSS

   writes `NAME.csv`, `NAME.labels.json`, `NAME.labels.tsv` into
   `v2/traces/candidates/` (git-ignored) with
   `NAME = YYYYMMDD-s<N>-<stage_lower>-<run_kind_lower>`, prints per-session
   replay parity against the firmware `RESULT` line, and lists every fixture
   that needs human review.

5. **Decide worn acceptance by run id** (section 7):

       python3 tools/make_trace_fixture.py --accept-runs V1,V2 --session .device-backups/logs/capture-YYYYMMDDTHHMMSS

   The designated set must be one of the plan's `acceptance.designated_runs`
   (the plan is the one recorded in the capture's `session.json`); anything
   else, or a capture without a plan, is `REJECT` and the `ACCEPTANCE` line
   names the rule that was applied.

## 5b. Wireless equivalence acceptance (once, bench, before worn sessions)

One desk run over `--tcp` after the telemetry OTA, no cable ever:

1. Per-stage `effective_rate_hz` within 5% of the retained USB bench baseline
   (about 169-170 Hz).
2. `EVENT,TELEMETRY,DROPPED` total 0 (tolerate under 0.1% of rows).
3. Zero unknown-class lines beyond the boot banner.
4. The device-time `usec` gap histogram comparable to the retained USB logs
   (ground truth is device-side and transport-independent by construction).
5. `split_sessions` plus per-session replay parity green on the TCP `raw.log`.
6. One 30-second free-run rest capture completes end to end (section 12).

Record the numbers in `PROJECT_KNOWLEDGE.md` as observations. Only then run
worn sessions.

## 6. Device checklist

* Identify the unit before anything else. The tested StickS3 is the OTA
  identity `sticks3-ptt.local` with MAC `AC:27:6E:D2:68:B8`. It is **never**
  the bedside `bedside-countdown-s3` device. The USB descriptor check in the
  capture tool is the second lock: manufacturer `Colibrino`, product
  `Colibrino StickS3 Prototype`, serial number equal to
  `COLIBRINO_USB_SERIAL`.
* No upload, no flash, no OTA during a capture day. The capture tool is a
  monitor. `platformio` is not part of this protocol.
* Never hold Button A for 2 seconds or longer. A long hold in `IR PROBE` mode
  powers the IR rail; a long hold in `MOTION` mode cycles to `MOUSE`; a long
  hold in `MOUSE` mode arms HID. Never arm. The tool alarms on `armed=1` and
  `mode=MOUSE`; if either appears, stop and unplug.
* Button use during capture: after boot the device is in `IR PROBE`; one tap
  moves it to `MOTION`; the next tap starts a probe; after `CAPTURE_COMPLETE`
  a tap starts the next probe.
* BOOT block: plug in with hands off and let at least 60 seconds of `STATUS`
  lines accumulate. When the block shows `armed=0 ir=0 calibrated=1
  imu_blink=0 ota=READY` the pending "third OTA post-reboot CDC confirmation"
  item is closed by this capture (`session.json` `boot.boot_ok`). Note the
  firmware commit that is installed in the run notes if it is known.
* Record the live USB descriptor: run `--discover` on the day and keep the
  `session.json` identity block (it is redacted; the full serial stays only in
  the ignored `.env`).
* Display fingerprint during R0: while the device is on the bench and the
  probe is in `BLINK_FIRMLY`, the screen must read `blink twice; pause 1 sec`
  and `blink twice; then repeat`. That is the coded-pattern build. If it reads
  anything else, stop; the wrong firmware is installed.
* Mount: glasses right temple unless a run says otherwise; write the mount in
  the run notes when it differs from the plan. Remount only at the marked
  break.

## 7. Worn acceptance by run id

Acceptance is decided by designated runs only, with the mechanism in
`make_trace_fixture.py --accept-runs`. It slices each designated run's latest
valid attempt into its own log, replays it through the current detector build
and prints a per-run table plus one aggregate verdict.

Round one is accepted when all of the following hold:

1. `V1` and `V2` (Session A) or `V2` and `V3` (fresh remounts) both replay
   `RESULT=PASS` with clean controls: `--accept-runs V1,V2 --session <A>` or
   `--accept-runs V2,V3 --session <A> --session <B>` prints `ACCEPT`.
2. Zero control-stage sequences in every valid run of the session (the
   `SUMMARY ... controls_clean=yes` line of `make_trace_fixture.py` over the
   whole capture).
3. `U` yields 0 / 0 / 0 (uniform four blinks must not be a click).
4. Per-run replay parity: the firmware `RESULT` and the host replay agree for
   every valid run of the capture (`PARITY ... MATCH`). A mismatch means a
   different detector build or dropped lines and blocks acceptance until
   explained.
5. No fatigue stop, no alarm during the designated runs.

Runs that are not designated never count toward acceptance, whether they pass
or fail; they are evidence, not verdicts. Redo attempts (`r`) invalidate the
earlier attempt; only the latest valid attempt of a run id is considered.

The tool enforces the designation itself: it loads the plan recorded in each
`--session` directory's `session.json`, requires the designated set to equal
one of `acceptance.designated_runs` (`V1+V2` or `V2+V3` in `round1.json`;
without that list it falls back to "at least two `V_standard` runs whose plan
expects `BLINK_FIRMLY >= 2`"), rejects captures without a plan or with
disagreeing acceptance blocks, and prints the rule in the `ACCEPTANCE` line
(`rule_ok`, `runs_ok`, `verdict`). Runs from a simulated capture yield
`verdict=REHEARSAL`, never `ACCEPT`.

## 8. When to trigger the VL53L4CD-class evaluation

Evaluate a compact sensor with a documented near-eye safety case (ST
VL53L4CD-class Time-of-Flight, Class 1 940 nm as documented) before any bulky
TCRT5000 module when any of these happens across Sessions A and B:

* the designated V runs cannot pass consistently (two of three V runs fail
  after fresh remounts), or
* the operator reports the coded gesture as tiring (fatigue 3/5 or worse
  before the caps are reached), or
* any control-stage sequence appears in a valid run (still or head), or
* replay parity cannot be restored.

The trigger opens an evaluation, not a purchase or a mount decision. Every
option still needs a schematic, optical review and physical mount test.

## 9. Timing alignment is estimated provenance

The device prints its own `millis()`; the host records `monotonic_ns` per
line. Per run the tool estimates
`offset_ns = 1st percentile over IMU rows of (host_ns - device_ms * 1e6)`
(method `lower_envelope_p01`) and stores offset plus residual statistics in
`session.json` `alignment[]`, labelled `ESTIMATED`. USB and host scheduling
only ever add delay, so the lower envelope tracks the true offset to a few
milliseconds; it is not a clock synchronization and must not be presented as
one. Cue timestamps in fixtures use the stage-relative mapping
(`t_offset_ms + slip_ms` after the stage line) and are refined to the gyro
first-difference peak within `[cue + 50, cue + 650] ms`; the refined peak, not
the cue, is the fixture anchor.

## 10. Privacy and commit policy

* Public by default: `R0`, `V*`, `HB*`, `U`, `C*`, `HS*`, `R1`, `SB`, `CF2`.
* Private: `CF1` (chewing, talking). Its fixtures carry
  `commit_class: private`, live under `v2/traces/private/` if kept at all, and
  the promotion gate refuses them.
* Forbidden in any committed fixture or label: MAC addresses in any form, the
  device serial, the OTA hostname, IPv4 addresses, `/Users/` paths, SSID or
  password keys, and time-of-day (`HH:MM:SS`). `make_trace_fixture.py` scans
  every output before writing and refuses on any hit, printing the rule name
  and never the matched text.
* Raw captures (`sticks3/.device-backups/**`) and `.env` are never committed.
* Clearance: each run in `session.json` has `cleared: false` by default. The
  owner flips it after review; a fixture's `provenance.cleared_on` is set to
  the review date only then.

## 11. Fixture promotion (manual, gated)

1. `make_trace_fixture.py` writes candidates into `v2/traces/candidates/`
   (git-ignored) and prints `REVIEW REQUIRED` for anything with an ambiguous or
   missed cue peak, a parity mismatch, a coded-group count or a whole-stage
   `CLICK_CANDIDATE` expectation that disagrees with the replay's stage
   sequence count (for example a `V` run whose data holds zero coded
   patterns), per-cue `IMPULSE` expectations exceeding the detector-accepted
   impulses in the stage, a simulated capture, a plan file changed since the
   capture, or an unmarked source.
2. A human reviews the labels, corrects segments and expectations, sets
   `provenance.labeler` to `owner` or `owner_reviewed`, sets
   `provenance.cleared_on`, and clears `review_required` only when every reason
   is resolved. `labels.tsv` must remain the derivation of `labels.json`
   (`make_trace_fixture.derive_tsv`).
3. `make_trace_fixture.py --promote NAME` copies the three files into
   `v2/traces/` and appends their SHA-256 to `v2/traces/MANIFEST.sha256`. It
   refuses while `review_required` is true, while the labeler is still
   `agent`, while `cleared_on` is null, for `private` fixtures, for fixtures
   with `provenance.simulated: true`, on any de-identification hit, or if a
   promoted file with that name already exists (promoted fixtures are
   immutable).
4. The commit that adds promoted fixtures is a human commit; nothing here
   automates it.

## 12. Free-run capture (built; hardware validation pending)

Built as specified by the earlier design sketch, delivered in the same
authorized fourth OTA as the telemetry mirror:

* `ImuProbeStage::kCaptureFreeRun`, stage string `CAPTURE_FREE_RUN`;
  `imuProbeCapturing()` includes it, so the ordinary
  `IMU,<ms>,<usec>,CAPTURE_FREE_RUN,...` rows stream during the block.
* Entry: start a guided probe (tap), then a **second tap during the
  `PREPARE_STILL` screen** converts it to free-run. The conversion emits the
  `EVENT,IMU_PROBE_STAGE,CAPTURE_FREE_RUN` transition line and deliberately
  does NOT emit a second `EVENT,IMU_PROBE_STARTED` line, so every tool keeps
  seeing exactly one session.
* Stop: tap again, or the hard 90-second cap. Either way the block ends with
  `EVENT,IMU_PROBE_COMPLETE,samples=N` and
  `RESULT,IMU_BLINK,NOT_PROVEN,still=..,blink=..,head=..,free=..`.
* Safety: free-run **never** sets `imu_blink_validated` (the RESULT is always
  NOT_PROVEN); detector sequences during the block are counted in the
  separate `free=` field and never in the validation predicate; the device
  cannot leave Motion Monitor while the block runs; `lockForOta()` cancels it
  like every probe.
* Use it for the segments the guided windows cannot hold: 30-second
  contiguous rest baselines and long confounders. Free-run fixtures are
  always `review_required` and are labeled from the spoken cues only; a
  free-run session never counts toward worn acceptance (`--accept-runs`
  ignores it).
