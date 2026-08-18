---
slug: wireless-capture-session
round: 1
builder_session: fresh Builder session, 2026-08-17 — verified handover claims live before planning
---

# PLAN-r1 — wireless capture: fourth OTA, equivalence, worn Session A

## Blocking constraints, restated (Builder's own words)

1. **Hardware gate.** No upload/flash/OTA ever happens without Felipe identifying the device and giving an explicit, in-the-moment go for that specific upload. The fourth OTA being direction-approved does not substitute for the go. Green builds, tests, pings, monitors authorize nothing. `bedside-countdown-s3` is never a target.
2. **Oracle Loop is merge-frozen.** No merge, rebase, squash, force-push, or history rewrite there without Felipe's exact phrase `merge allowed` / `pode mergear`. The 2026-08-17 grant was consumed by the engine merge to main; the two remaining stacked branches (`agent/colibrino-v2-luos-qualification`, `agent/colibrino-v2-round-one-corrections`) carry no grant.
3. **Master never RED.** `agent/v2-core-scaffold` stays branch-only until real fixtures are promoted; the only path to master is: fixtures promoted → integration branch green under BOTH v2 presets (`host` and `host-oracle`) → fast-forward.
4. **Output-only telemetry.** The TCP socket and CDC stream device→host only; no command channel, ever — Button A is the sole input. Host keeps `shutdown(SHUT_WR)` after connect, the `EVENT,TELEMETRY,CONNECTED,` greeting-prefix check, and the ping + ARP-MAC guard; the loopback exemption exists for tests only.
5. **Blink clicking stays off.** Nothing sets `imu_blink_validated` or touches the PASS predicate until a fresh worn probe passes on hardware. Free-run always yields `NOT_PROVEN`.
6. **Fixture privacy.** Raw logs and BOOT captures are never committed. No MAC, hostname, IP, `/Users/` path, or time-of-day inside `v2/traces/**`. CF1 confounders private by default. Every promoted fixture needs Felipe's clearance line in `session.json`.
7. **Session caps are hard.** ≤ 8 probe runs, ≤ 50 firm blinks, ≤ 20 min, ≥ 45 s eye rest between runs; stop at fatigue ≥ 3/5 or any discomfort — Felipe's call, mid-session, no argument. Deliberate gestures only inside `BLINK_FIRMLY`; `KEEP_HEAD_STILL` and `MOVE_HEAD` stay genuine controls. Worn acceptance selects runs BY ID (`V1+V2`, or `V2+V3` after fresh remounts) — never by count.
8. **Forbidden APIs in the telemetry path.** `WiFiClient::write` (blocks the loop task up to ~10 s) and `WiFiClient::connected()` (latches false after the host's intended half-close FIN) must not appear. The mux tracks attachment with its own flag; only a send() errno detaches a client. This is a deliberate design, not something to "clean up".

## Handover discrepancies (every [REAL] claim checked live, 2026-08-17)

Checked: both repo heads and branch tips, tree cleanliness, native suite, tooling suite, production build + build id, device-off (IP ping, mDNS ping, USB enumeration), baseline log inventory, `.env` key names + secrets header presence, PORT_PLAN task states. Findings:

1. **Master tip moved**: `f003afc` ("Add wireless capture session handover") — the Author's own handover commit, one doc-only commit above the claimed `773d38c`. Clean, == origin/master; all listed content commits intact beneath it. Consequence: a fresh production build now embeds `COLIBRINO_BUILD_ID=f003afc`, and the post-reboot check must expect `f003afc`, not `773d38c`.
2. **RAM +8 B**: build measured RAM 98,244 B (30.0%) vs the claimed 98,236 B; Flash identical at 1,063,597 B (31.8%). Doc-only tip delta; not meaningful.
3. **Baseline log nesting**: the three USB baseline files exist, but one is nested one level deeper — `.device-backups/logs/logs/device-monitor-260814-234754.log`. The equivalence comparison must include it by explicit path. A `capture-sim-20260817T201452/` directory (from `--simulate`) also lives in `.device-backups/logs/`.
4. Everything else matched: `agent/v2-core-scaffold` = `00c1285`; Oracle Loop main = `325c3ea` == origin, `agent/colibrino-v2-luos-qualification` (origin-only ref) = `84b5bea`, `agent/colibrino-v2-round-one-corrections` = `b5e65b8` local == origin; native 13/13; tooling 29/29 (71 s); device OFF (both pings 100% loss, no `/dev/cu.usbmodem*`); `.env` has all four named keys plus `PLATFORMIO_CLI_BIN`/`WOKWI_CLI_BIN`; `colibrino_secrets.h` present; PORT_PLAN T15 = `implemented_pending_hardware`, T8 = `implemented_and_hardware_validated`.
5. Incidental: two historical Colibrino branch pointers exist (`agent/v2-wireless-capture` = `773d38c`, `agent/v2-round-one-handoff` = `1a5fa9e`). No action proposed.

## Acceptance checklist — point-by-point

1. **Constraints restated** — first section above, all 8, own words.
2. **OTA runbook first and in order** — Step 1 below follows the exact sequence: rebuild + note build id → back up `firmware.bin` to ignored `.device-backups/` → `say` announce → WAIT for power-on + explicit go → `./scripts/upload_ota.sh` → post-reboot TCP verification (greeting sha == built sha `f003afc`; STATUS `armed=0 ir=0 calibrated=1 imu_blink=0 ota=READY tele=1 build=f003afc`; sane batt) → `say` outcome; on failure say so and STOP.
3. **Bench equivalence** — Step 3 runs all six section-5b items: (i) per-stage `effective_rate_hz` within 5% of the retained USB bench baseline (~169–170 Hz), (ii) `EVENT,TELEMETRY,DROPPED` total 0 (tolerate < 0.1% of rows), (iii) zero unknown-class lines beyond the boot banner, (iv) device-time `usec` gap histogram comparable to the retained USB logs, (v) `split_sessions` + per-session replay parity green on the TCP `raw.log`, (vi) one 30 s free-run block end-to-end. Numbers recorded in `PROJECT_KNOWLEDGE.md` as observations BEFORE any worn run.
4. **Worn Session A per protocol** — Step 4 runs `round1.json` `sessions[0]` exactly: BOOT (≥ 60 s hands-off STATUS) → R0 → V1 → HB1 → U → BREAK_REMOUNT (≥ 45 s rest) → V2 → C → HS; 7 probe runs, 42 firm blinks, all caps live, fatigue typed at `q`, deliberate gestures only in BLINK_FIRMLY, acceptance judged on `--accept-runs V1,V2`. No invented runs, no relaxed caps.
5. **T15 update gated on reality** — Step 2 edits `PORT_PLAN.json` T15 → hardware-validated (telemetry path only) and records sizes/observations ONLY after the post-reboot check has actually passed. No pass → no edit.
6. **Branch discipline** — `agent/v2-core-scaffold` stays branch-only. Order in Step 5: Felipe-cleared fixtures promoted → `labels.schema.json` stage enum gains `CAPTURE_FREE_RUN` on the core branch (with `labels_schema_consistency` ctest green) when the first free-run fixture is promoted → integration branch → both presets green → FF master.
7. **Step 0 verification** — already executed while building this plan (see discrepancies); the device-off check is re-run immediately before the Step 1 announce.
8. **Oracle Loop untouched** — this plan schedules no Oracle Loop work. If session outcomes warrant doc updates there, they go as commits on `agent/colibrino-v2-round-one-corrections` only, push only, no merges.
9. **OTA failure branch** — explicit in Step 1g: any upload or post-reboot failure → `/usr/bin/say` the failure, report the exact evidence, STOP. No retry loop without Felipe. Rollback is Felipe's decision.
10. **Scope fence** — no work in `/Users/fcavalcanti/dev/felipe/home-automations`; `bedside-countdown-s3` never targeted (the ARP-MAC guard in `upload_ota.sh` stays the enforcement, and no discovery is pointed at it).

## Implementation plan

Execution starts only after APPROVED review; first move is the handover's `next_action`.

**Step 0 — verification sweep**: done during plan-building (results above). Re-check device-off (`ping` both addresses, no usbmodem) right before Step 1c.

**Step 1 — fourth OTA runbook** (from `sticks3/`):
  a. `/Users/fcavalcanti/.platformio/penv/bin/platformio run -e m5stack-sticks3`; confirm printed `COLIBRINO_BUILD_ID=f003afc` (clean tree).
  b. `cp .pio/build/m5stack-sticks3/firmware.bin .device-backups/firmware-rollback-<sha>.bin` — sha naming pending author's answer (Q1); default `f003afc`.
  c. `/usr/bin/say "Colibrino OTA ready. Please power on the StickS3 and confirm."`
  d. WAIT. Nothing runs until Felipe confirms power-on AND gives the explicit upload go.
  e. On go: `./scripts/upload_ota.sh` (script enforces mDNS resolution + ARP-MAC `AC:27:6E:D2:68:B8`; refuses anything else).
  f. Post-reboot TCP verification: connect with `/Users/fcavalcanti/miniconda3/bin/python3 tools/capture_session.py --tcp`; require greeting `EVENT,TELEMETRY,CONNECTED,f003afc,dropped=<n>`; STATUS shows `armed=0 ir=0 calibrated=1 imu_blink=0 ota=READY tele=1 build=f003afc`; battery value sane.
  g. `say` the outcome. On ANY failure in e/f: `say` it, report evidence verbatim, STOP (no retry without Felipe).

**Step 2 — record reality (only after 1f passes)**: edit `sticks3/PORT_PLAN.json` T15 → hardware-validated (telemetry path only) with observed sizes and the TCP verification facts; add the observation entry to `PROJECT_KNOWLEDGE.md`. Commit only when Felipe says commit (standing rule).

**Step 3 — bench equivalence acceptance (section 5b, once, on the bench)**: one desk probe run + one 30 s free-run block over `--tcp`; evaluate the six criteria against the retained USB baselines (including the nested `logs/logs/device-monitor-260814-234754.log`); run `split_sessions` + replay parity on the TCP `raw.log`; record all numbers in `PROJECT_KNOWLEDGE.md` as observations. All six pass → worn sessions unblocked. Any fail → stop, report, no worn run.

**Step 4 — worn Session A** (`/Users/fcavalcanti/miniconda3/bin/python3 tools/capture_session.py --tcp --plan tools/capture_plans/round1.json --plan-session A`): run matrix exactly as `sessions[0]`; caps and stop conditions live; Felipe types fatigue at `q`; afterwards judge designated runs with `make_trace_fixture.py --accept-runs V1,V2 --session <dir>`. Felipe judges acceptance and gives per-run privacy clearance lines. (If V1+V2 don't both pass, V2+V3 across Sessions A+B is the alternate path — Session B is another day.)

**Step 5 — promotion and v2 integration** (only Felipe-cleared runs): `make_trace_fixture.py --promote` onto `agent/v2-core-scaffold`; first free-run promotion extends the `labels.schema.json` stage enum with `CAPTURE_FREE_RUN` on that branch, `labels_schema_consistency` ctest green; then integration branch, both v2 presets green, FF master. Session B and the two Oracle Loop branch merges stay out of scope (Felipe's phrase required).

## Concerns

- **MEDIUM — build id drift**: the direction-approved fourth OTA was implicitly the `773d38c` build; the tip is now `f003afc` (doc-only handover commit; firmware code identical, embedded id string differs). The post-reboot check must expect `f003afc`, and Felipe's go should be given knowing the label changed.
- **MEDIUM — rollback binary provenance**: the Builder's verification build overwrote the on-disk `firmware.bin` (now `f003afc`; content-identical to the Author's `773d38c` build except the id string). If "back up current firmware.bin" meant archiving the Author's artifact byte-for-byte, that exact file no longer exists. The device's installed third-OTA image predates both; PORT_PLAN policy says a verified recovery image exists.
- **LOW — nested baseline log**: one of the three USB baseline files sits at `.device-backups/logs/logs/…`; equivalence tooling must reference it explicitly or it silently drops out of the comparison.
- **LOW — baseline rate span**: handover says ~162–170 Hz per stage across the baselines, protocol 5b says within 5% of ~169–170 Hz. Reading them together: the tolerance is applied per-stage against the retained USB baseline for that stage.

## Questions for the author

1. Step 1b filename: `firmware-rollback-<oldsha>.bin` — which sha? The new build id (`f003afc`, archiving the binary being uploaded) or the currently-installed third-OTA sha (archiving what we'd roll back to)? And does a copy of the installed third-OTA image already exist under `.device-backups/`?
2. Is the `f003afc` rebuild acceptable as the fourth-OTA payload, or do you want the upload built from `773d38c` exactly (checkout, build, restore)?
3. Passing through your open questions for Felipe: bench equivalence + worn Session A same sitting or separate days; free-run schema-enum extension in the same commit as the promotion or a prior commit; charge-first threshold if battery < ~30% at power-on.
4. Commit cadence during execution: commit the Step 2 doc/status edits immediately after the post-reboot pass, or batch at session end? (Standing rule says commit only on Felipe's ask; which moment do you want proposed to him?)
