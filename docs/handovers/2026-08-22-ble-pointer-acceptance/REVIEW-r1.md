---
verdict: APPROVED
round: 1
---

# Review r1

## Checklist verdicts

1. Constraints restated: satisfied — all 8, accurate, and the drawn consequences strengthen them ("even read-only `read_flash` is a hardware touch"; "no bench failure is answered by loosening a gate"; scratchpad-only analysis). Adopted.
2. Bench items with observable pass criteria: satisfied — B1–B5 each carry precondition, exact operator action, exact expected lines with bounded timing (≤ 2 s plug-lock, ≤ 3 s unplug-lock), the recording lane, and a pass/fail rule; the `ble_reports`-unchanged check during the USB sweep is a double-report detector I did not ask for and endorse; charge-only-cable honestly "not exercised".
3. Stage 2: satisfied — verbatim macOS path, camera click ON only for that item, stationary + natural-blink controls, selection-only targets, Felipe's verbatim verdict as the only `[REAL]`.
4. Vertical feel: satisfied and better than asked — the bench-capture mining (vertical channel ≈ 70 % of horizontal rate; the unused Z axis carrying as much as horizontal) is exactly the evidence-first posture; the ≤ 90 s `CAPTURE_FREE_RUN` axis-isolation recording, the H-a/H-b decision table with default-preserving config extensions, the A/B verification, and the revert rule are all right. Nothing changes before the decision.
5. Docs truth table: satisfied — per-item outcomes, T16 status moves only on a full pass, failures/not-exercised recorded truthfully, JSON validated.
6. Merge: satisfied — FF-only, exact gate list, master-advanced branch handled by merging master into the published branch (never rebase), wait-for-the-call.
7. Fence: satisfied — complete edit set enumerated; the mux, tools, v2, Oracle Loop, safety-net files, and `platformio.ini` all outside it.

Handover discrepancy handling: exemplary. The dated device-state claim correctly demoted to `[UNVERIFIED until Step 0]` with a hard identity gate (device is simply off — expected). The **`upload_ota.sh` no-`pipefail` gap is a real HIGH finding against code this author wrote yesterday**: the build gate protects `pio run` but the uploader would proceed past a failed build and push the stale `firmware.bin`. Caught by re-verification, exactly what this ritual is for.

## Answers to the Builder's questions

1. **Order** — approved as planned (B1→B2→B3→B4→Stage 2). Stage-2-first is acceptable if Felipe prefers, provided B4's soak runs with the camera click OFF.
2. **B2** — yes: Felipe's iPhone or a second Mac as the foreign central; "not listed on the second central for 20 s" is an acceptable observable; `AUTHENTICATION_FAILED` + no `SECURE` is the alternative evidence if it does appear.
3. **B4 bounds** — accepted as proposed, including the 8 KB last-vs-first-minute heap-drift bound. A violated bound stops the run and comes back here; it is never tuned inside the run.
4. **Vertical** — the axis-isolation recording is approved as a no-code bench item in the same sitting (it only streams IMU rows). The decision table then goes to Felipe/this author. The macOS tracking-speed rejection note is sufficient; keep no Mac-side option open as a vertical-only fix.
5. **Canary** — agreed: merge without it; it stays a separate later item on Felipe's go, and T17's `[TEST]`-only caveat stands.
6. **Docs on partial results** — keep the current status value; no new status proliferation. T16 becomes `implemented_and_hardware_validated` only when B1–B4 and Stage 2 have all passed; anything not exercised keeps the current status and an explicit `[PENDING]`/`not exercised` line.
7. **UART0 tap** — agreed, dropped unless a future diagnosis needs it.
8. **`upload_ota.sh` fix** — **approved now, as its own commit on this branch before the next OTA**: fail on a failed build (no pipeline; test the exit status), require `^CHECK_IMAGE,PASS` in the build log, require a `firmware.bin` newer than the build start; verify with a deliberately failing `COLIBRINO_DRAM_CEILING` build-only dry run (no device contact), then restore and re-run the normal gates. `sh -n` before committing. This is the review authority's go for that one file; every hardware go remains Felipe's, per attempt.

## Verdict

**APPROVED.** The Builder session is now the primary session for the acceptance tail. First move is the handover's `next_action`: Step 0 gates are already green — when Felipe powers the device (single click), run the receive-only TCP identity check and require `EVENT,TELEMETRY,CONNECTED,ff1f7d2` / slot app1 / `img=VALID` before anything else; apply the approved `upload_ota.sh` fix meanwhile. Then B1 with Felipe's hands. Every OTA, flash, download-mode entry, and button press stays gated on Felipe's explicit in-the-moment go.
