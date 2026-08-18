---
verdict: APPROVED
round: 1
---

# Review r1

## Checklist verdicts

1. Constraints restated: satisfied — all 8, accurate, own words; item 2 even sharpens mine (the consumed grant).
2. OTA runbook order: satisfied — Step 1 a-g matches the required sequence exactly, with the corrected expected sha `f003afc`.
3. Bench equivalence: satisfied — all six 5b criteria plus the 30 s free-run block, numbers recorded before any worn run, explicit stop-on-fail.
4. Worn Session A per protocol: satisfied — `sessions[0]` verbatim, caps live, gestures only in BLINK_FIRMLY, acceptance by run ID via `--accept-runs V1,V2`, no invented runs.
5. T15 gated on reality: satisfied — "No pass -> no edit."
6. Branch discipline: satisfied — promotion -> schema enum on the core branch with the consistency ctest green -> integration branch -> both presets -> FF master.
7. Step 0 verification: satisfied — executed during planning with a thorough discrepancy report (better than asked); device-off re-check before the announce.
8. Oracle Loop untouched: satisfied — push-only doc commits at most.
9. OTA failure branch: satisfied — say, report verbatim, STOP, rollback is Felipe's decision.
10. Scope fence: satisfied.

The three discrepancies found are accepted as corrections to the handover: master tip is `f003afc` (my own handover commit — I failed to anticipate that committing the handover would move the tip the build id embeds; the Builder caught it), RAM 98,244 B (the 8 B delta is the different embedded id-string length; not meaningful), and the nested third baseline log path.

## Answers to the builder's questions

1. **Rollback filename**: archive the binary being UPLOADED, named by its content: `firmware-upload-f003afc.bin`. A byte-exact copy of the installed third-OTA app binary does not exist (the build dir was rebuilt many times); do not pretend otherwise. The real recovery lane is: (a) the full 8 MB pre-Colibrino flash image already under `.device-backups/` (sha256 recorded in PROJECT_KNOWLEDGE), and (b) rebuilding any known-good git sha and re-uploading via OTA. State that in the runbook note; no rollback binary of the third OTA is to be fabricated.
2. **`f003afc` as payload: yes.** Firmware sources are identical between `773d38c` and `f003afc` (doc-only commit); embedding the sha of the tree actually uploaded is more honest. Expect `f003afc` in the greeting and STATUS. Tell Felipe the label changed when asking for the go (your MEDIUM concern, correctly raised).
3. **Pass-throughs — FELIPE'S CALLS, with my recommendations**: bench equivalence + worn Session A in the same sitting is fine if he feels fresh (the bench run is minutes); schema enum extension in the SAME commit as the first free-run promotion (keeps `labels_schema_consistency` atomic); charge first if batt < 30% — recommend yes.
4. **Commit cadence**: propose to Felipe one small commit immediately after the post-reboot pass (Step 2 alone, so the hardware fact is durable even if the session stops there), everything else batched at session end. His mid-session word overrides.

## Closing

Builder is now the primary session. First move: the handover's `next_action` — re-run the device-off check, then from `sticks3/`: production build (confirm `COLIBRINO_BUILD_ID=f003afc`), archive `firmware-upload-f003afc.bin` under ignored `.device-backups/`, then `/usr/bin/say "Colibrino OTA ready. Please power on the StickS3 and confirm."` — and WAIT for Felipe's power-on and explicit go.
