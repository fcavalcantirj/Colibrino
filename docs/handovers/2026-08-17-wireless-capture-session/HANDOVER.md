---
slug: wireless-capture-session
date: 2026-08-17
status: approved
round: 0
author_session: Colibrino/Oracle-Loop round-one session (validated both repos, corrected 14 doc contradictions, built the v2 core scaffold, capture tooling, and the wireless telemetry firmware; ~81% context used)
---

# Handover — Colibrino wireless capture: OTA, verification, and first cable-free trace session

## Mission

Colibrino is an accessibility head mouse (M5Stack StickS3, BMI270, fail-closed HID). Round one needs fresh worn IMU trace fixtures for the v2 pure-core golden oracles. The owner decided capture is CABLE-FREE forever: a USB cable is a physical anchor on the ~20 g head mount and distorts the very blink impulses being measured. This session built and merged everything software-side: a read-only TCP telemetry mirror + free-run capture stage in the firmware, the `--tcp` capture transport, and all docs. "Done" for the successor = (1) the fourth authorized OTA is uploaded and verified post-reboot over TCP, (2) the no-cable equivalence acceptance passes on a bench run, (3) worn Session A runs wireless and its designated V-runs are judged, (4) fixtures promoted and the v2 core integrated per the standing plan. Every hardware step is gated on Felipe.

## Where things stand (verified)

- [REAL] Colibrino `master` = `773d38c` == origin/master; clean tree — verified 2026-08-17, `git log --oneline -7 --decorate`. Contains (newest first): `773d38c` tooling review fixes, `b81dcfb` TCP transport + free-run tooling, `1c625c8` firmware review fixes, `3182128` Oracle-main-merge fact, `3416fce` wireless docs, `e1b1c48` telemetry firmware, `1a5fa9e` prior handoff commit.
- [REAL] Branch-only, NEVER merged: `agent/v2-core-scaffold` = `00c1285` (v2 pure host core; host preset 16/16 with 2 golden tests skipped for lack of fixtures; host-oracle preset fails exactly those 2 by design; reaches master ONLY via a green integration branch).
- [REAL] Oracle Loop: `main` = `325c3ea` — Felipe granted the exact phrase `merge allowed` on 2026-08-17 and his merge tool fast-forwarded the engine branch through `3c67984` (includes the ratified v2 oracle map `b932e38` and v2 SPEC) to main. Verified via `git log 3cf1c30..origin/main`. Still UNMERGED above main: `agent/colibrino-v2-luos-qualification` (`84b5bea`) and `agent/colibrino-v2-round-one-corrections` (tip `b5e65b8`).
- [REAL] Production firmware builds on `master` with the secrets header: RAM 98,236 B (30.0%), Flash 1,063,597 B (31.8%); build id injected (`COLIBRINO_BUILD_ID=<short-sha>[+dirty]` printed during build). Without the header: RAM 35,536 B, Flash 589,137 B (pure CDC pass-through variant). Verified 2026-08-17 via `platformio run -e m5stack-sticks3`.
- [REAL] Native suite 13/13 (`platformio test -e native`); Wokwi gate untouched this phase (portable logic unchanged; last full pass earlier same day: 11 checks, `COLIBRINO_SIM_PASS`).
- [REAL] Host tooling tests: 29/29 (`cd sticks3 && /Users/fcavalcanti/miniconda3/bin/python3 -m unittest tools/tests/test_capture_tools.py`). Includes: TCP loopback adapter (greeting required, SHUT_WR proven by server-side recv seeing only EOF, reconnect after mid-stream close, no-greeting abort), free-run session handling, STATUS build/batt/tele compat both directions, DROPPED alarm, `127.evil.example` guard-bypass regression.
- [REAL] Device identity: hostname `sticks3-ptt.local`, MAC `AC:27:6E:D2:68:B8` (this session pinged it at 192.168.0.194 and matched ARP before Felipe turned it off). The separate `bedside-countdown-s3` (LilyGO T-Display-S3) must NEVER be targeted; `scripts/upload_ota.sh` refuses it by ARP-MAC guard.
- [REAL] Device is currently OFF. Felipe said he will power it on when told (announce readiness with `/usr/bin/say`).
- [REAL] The retained USB baseline logs (ignored, `sticks3/.device-backups/logs/`): 3 files, ~162-170 Hz effective per stage; the 260815 file has 5 guided sessions. These are the equivalence-comparison baseline.
- [TEST] TelemetryMux firmware behavior (half-close survival, drop counting, ring resync, rate-limited server begin) — adversarially reviewed against the installed Arduino-ESP32 2.0.17 / lwIP sources and fixed; compile-verified only. NOT hardware-validated.
- [TEST] Free-run stage (`CAPTURE_FREE_RUN`): entry = second Button-A tap during PREPARE_STILL (no second STARTED line — the STAGE transition is the marker); stop = tap or 90 s cap; RESULT always `NOT_PROVEN,...,free=N`; never sets `imu_blink_validated`. Synthetic-log verified through the whole toolchain.
- [UNVERIFIED] Wireless equivalence numbers (effective rate, DROPPED) — by design unknown until the bench run.

Verbatim upload command (from `sticks3/`, after `.env` check):

```sh
./scripts/upload_ota.sh
```

Verbatim capture commands (from `sticks3/`):

```sh
/Users/fcavalcanti/miniconda3/bin/python3 tools/capture_session.py --tcp
/Users/fcavalcanti/miniconda3/bin/python3 tools/capture_session.py --tcp --plan tools/capture_plans/round1.json --plan-session A
```

## Blocking constraints (builder: restate these before planning)

1. NEVER upload/flash without Felipe explicitly identifying the device and authorizing THAT upload in the moment. The pending fourth OTA is direction-approved but the moment-of-upload go is still his. Builds, tests, monitors, pings authorize nothing. Never target `bedside-countdown-s3`.
2. Oracle Loop: never merge/rebase/squash/force-push/rewrite anything without Felipe's exact phrase `merge allowed` / `pode mergear`. His merge tool already merged the engine to main — that grant does NOT extend to the two remaining stacked branches.
3. Colibrino invariant: master's tip is never RED. `agent/v2-core-scaffold` stays branch-only until real fixtures are promoted and an integration branch is green under BOTH v2 presets (`host` and `host-oracle`); only then fast-forward master.
4. The telemetry socket and CDC are OUTPUT-ONLY. Never add a command channel; Button A is the sole input. The capture tool must keep `shutdown(SHUT_WR)` + greeting check + ping/ARP-MAC guard; loopback exemption is for tests only.
5. Blink clicking stays gated off: the double-pause-double detector is hardware-unvalidated until a fresh worn probe passes; free-run and telemetry must never touch `imu_blink_validated` or the PASS predicate.
6. Fixture privacy: raw logs and BOOT captures never committed; identifiers (MAC, `AC276ED268B8`, `sticks3-ptt`, IPs, `/Users/`, time-of-day) never in `v2/traces/**`; CF1 confounders (chewing/talking/walking) private by default; promotion needs Felipe's clearance line in `session.json`.
7. Physical session caps: <= 8 runs, <= 50 firm blinks, <= 20 min, >= 45 s eye rest between runs, stop at fatigue >= 3/5 or any discomfort. Deliberate gestures ONLY inside BLINK_FIRMLY; KEEP_HEAD_STILL and MOVE_HEAD stay genuine controls. Worn acceptance selects runs BY ID (V1+V2 or V2+V3 after fresh remounts), never by count.
8. `WiFiClient::write` and `WiFiClient::connected()` are forbidden in the telemetry path (write blocks the loop task up to ~10 s; connected() latches false on the host's expected half-close FIN). The mux tracks attachment itself; only a send() errno ends a client. Do not "simplify" this.

## Accepted residuals / Refuted — don't fix

- USB capture path: superseded by decision, not deleted. Do not resurrect cable-based capture or propose a "quick bench cable test" — Felipe refused twice ("why measure with cable").
- The third OTA's post-reboot CDC confirmation can never be closed as written (no USB will ever be attached); PORT_PLAN T8 records the supersession by the fourth OTA's TCP check. Don't try to "complete" it.
- The onboard IR pair is REJECTED for near-eye blink detection (physical evidence). Don't relabel it viable. TCRT5000 purchase stays deferred; VL53L4CD-class evaluation triggers only per the protocol's acceptance rules.
- `.dasbrow/` artifacts in Oracle Loop stay tracked (dasbrow tooling may read them) — flagged for Felipe, not ours to untrack.
- Free-run fixtures are BLOCKED from promotion by a `FREE_RUN_SCHEMA_NOTE` review reason until `v2/traces/labels.schema.json` (on `agent/v2-core-scaffold`) gains `CAPTURE_FREE_RUN` in its stage enum. Deliberate: the schema lives on the core branch; extend it there when first promoting a free-run fixture, and keep the `labels_schema_consistency` ctest green.
- `EVENT,TELEMETRY,CONNECTED,<build>,dropped=<total>` — the greeting gained a dropped baseline field late; the host prefix check `EVENT,TELEMETRY,CONNECTED,` still matches. Don't tighten the host check to full-line equality.
- The firmware review's minor findings are all already fixed in `1c625c8` (attachment flag, 1 s begin() rate limit, greeting baseline, mid-line resync newline, Wi-Fi-loss detach). Re-review only if you change the mux.

## Hard rules & human-reserved decisions

- The moment-of-upload OTA go IS FELIPE'S CALL — announce via `/usr/bin/say`, wait for his explicit go.
- Fixture privacy clearance per run IS FELIPE'S CALL (clearance line in session.json).
- Worn acceptance / fatigue stop IS FELIPE'S CALL during the session.
- Merging the two remaining Oracle branches IS FELIPE'S CALL (exact phrase only).
- Wokwi token rotation IS FELIPE'S CALL (he said current token is safe to use).
- PTT image restoration to the device IS FELIPE'S CALL (separate later decision).
- Report outcomes truthfully; label [REAL]/[TEST]/[UNVERIFIED]; never claim hardware validation from software evidence.

## Acceptance checklist (the author approves the plan ONLY against these)

1. Plan restates all 8 blocking constraints correctly, in the builder's own words.
2. Plan's first hardware step is the OTA runbook in exactly this order: rebuild + note build id -> back up current `firmware.bin` to ignored `.device-backups/` -> `say` announce -> WAIT for Felipe's power-on + explicit go -> `./scripts/upload_ota.sh` -> post-reboot TCP verification (greeting sha == built sha; STATUS `armed=0 ir=0 calibrated=1 imu_blink=0 ota=READY tele=1 build=<sha>`; sane batt) -> `say` the outcome; on failure say so and STOP.
3. Plan includes the bench equivalence acceptance with the five criteria from `docs/V2_TRACE_CAPTURE_PROTOCOL.md` section 5b (rate within 5% of ~169-170 Hz baseline, DROPPED 0 or < 0.1%, no unknown lines, comparable usec-gap histogram, replay parity) PLUS one 30 s free-run block, and states results go to PROJECT_KNOWLEDGE as observations before any worn run.
4. Plan runs worn Session A per the protocol (run matrix, caps, gestures only in BLINK_FIRMLY, acceptance by run ID) and does NOT invent new runs or relax caps.
5. Plan updates PORT_PLAN T15 to hardware-validated (telemetry path only) and records new sizes/observations ONLY after the post-reboot check actually passes.
6. Plan keeps `agent/v2-core-scaffold` branch-only; fixture promotion -> integration branch -> both presets green -> FF master, in that order; the labels schema enum extension for free-run happens on the core branch with the consistency ctest green.
7. Plan verifies at least these [REAL] claims before acting: both repo heads, native 13/13, tooling 29/29, production build + build id, device still OFF (no usbmodem, ping fails) — and reports discrepancies.
8. Plan touches nothing in Oracle Loop except (optionally) documentation commits on `agent/colibrino-v2-round-one-corrections`, push only.
9. Plan contains an explicit failure branch for the OTA (upload fails or post-reboot check fails): stop, `say` it, report, no retry-loop without Felipe.
10. Plan schedules NO work in `/Users/fcavalcanti/dev/felipe/home-automations` and never targets `bedside-countdown-s3`.

## next_action

Run the Step 0 verification sweep (checklist item 7). Then prepare the OTA: from `sticks3/` run `/Users/fcavalcanti/.platformio/penv/bin/platformio run -e m5stack-sticks3`, note the printed `COLIBRINO_BUILD_ID`, copy `.pio/build/m5stack-sticks3/firmware.bin` to `sticks3/.device-backups/firmware-rollback-<oldsha>.bin`, then announce: `/usr/bin/say "Colibrino OTA ready. Please power on the StickS3 and confirm."` — and WAIT for Felipe.

## Open questions

1. Does Felipe want the bench equivalence run and worn Session A in the same sitting, or separate days? (Session B is another day regardless.)
2. When the first free-run fixture is promoted, extend the schema enum on the core branch in the same commit as the promotion, or as a separate prior commit? (Either is fine; keep `labels_schema_consistency` green.)
3. Battery level unknown after power-on — if batt < ~30% at session start, charge first? (Suggest yes; Felipe decides.)

## Pointers

- Plan file (full phase plan, approved rev): `/Users/fcavalcanti/.claude/plans/include-users-fcavalcanti-dev-oracle-loo-wild-lightning.md`
- Protocol (run matrix, caps, acceptance, section 5b equivalence, section 12 free-run): `docs/V2_TRACE_CAPTURE_PROTOCOL.md`
- Machine-readable status: `sticks3/PORT_PLAN.json` (T8 supersession, T12-T15; `main_note` records the Oracle main merge)
- Firmware: `sticks3/src/telemetry_mux.{h,cpp}`, `sticks3/src/main.cpp`, `sticks3/scripts/build_id.py`
- Tooling: `sticks3/tools/capture_session.py` (`--tcp`, `--discover`, `--simulate`), `capture_common.py`, `make_trace_fixture.py` (`--accept-runs`, `--promote`), `tools/tests/test_capture_tools.py`
- Deep context: `AGENTS.md`, `PROJECT_KNOWLEDGE.md` (changelog top entries for this phase)
- v2 core (branch-only): `agent/v2-core-scaffold` -> `v2/` (presets host / host-oracle; `v2/traces/labels.schema.json`)
- Oracle Loop docs: branch `agent/colibrino-v2-round-one-corrections` (`docs/colibrino-v2-SPEC.md`, `docs/colibrino-v2-ORACLE-MAP.md`, `STATE.md`)
- Secrets/env (names only, never print values): repo-root `.env` (`COLIBRINO_OTA_HOST`, `COLIBRINO_OTA_EXPECTED_MAC`, `COLIBRINO_OTA_SECRETS`, `WOKWI_CLI_TOKEN`), `sticks3/include/colibrino_secrets.h`
