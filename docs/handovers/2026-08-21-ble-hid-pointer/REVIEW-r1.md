---
verdict: APPROVED
round: 1
---

# Review r1

## Checklist verdicts

1. Constraints restated: satisfied — all 8, accurate, and item 2 strengthens mine (zero-button report on auth failure before disconnect; no automatic failover may preserve the armed state). Adopted.
2. Stack decision: satisfied — NimBLE-Arduino pinned 2.5.1 with primary-source evidence, built-in Bluedroid as explicit non-silent fallback behind the identical adapter, T-vK rejected on lifecycle/advertising/ownership facts. The no-implicit-fallback rule (no core upgrade, no partition change) is exactly right.
3. Architecture: satisfied — single `MouseOutputRouter` choke point; portable C++17 `MouseOutputPolicy` with native + Wokwi coverage; callbacks publish atomic facts and the cooperative loop makes all safety transitions (correct concurrency model for this codebase); idempotent ordered `forceLock(reason)`; bounded 8–10 ms report cadence with overflow-drop so a stopped head can never produce delayed motion; STATUS tails and MOUSE display specified.
4. Pairing design: satisfied — bounded windows (60 s public only when bondless, 30 s whitelist-only reconnect), encryption+bond both confirmed before report-capable, deliberate 5 s forget+pair, timeout stays locked, screen states defined; correctly presented as a proposal for Felipe.
5. Coexistence plan: satisfied — 30 s `COEX_IMU` armed-overlap with explicit pass criteria against the recorded ~170 Hz / zero-drop baseline, plus the 10-minute soak.
6. Budget: satisfied — prediction bands, post-build measurement list, and hard stop thresholds that trigger review rather than silent tuning.
7. Test plan: satisfied — enumerated native cases (the mode-entry-hold-cannot-carry case is a catch I did not ask for and endorse), Wokwi additions, the full physical sequence, and Stage 2 wireless as final acceptance run by Felipe.
8. Rollout: satisfied — branch, cadence, exact OTA runbook with WAIT and post-reboot `ble=IDLE hid=NONE` check, stop/no-retry failure branch.
9. Docs gated on reality: satisfied — "software compiles but physical acceptance fails" keeps the branch unmerged and the status truthful.
10. Scope fence: satisfied — explicit exclusion list including the mux, capture tooling, detector constants, and Oracle Loop.

Handover discrepancy handling: correct — master moved to `d6594f4` because of my own handover commit (same authoring failure mode as the 2026-08-17 handover; the Builder caught it, again). No-secrets baseline sizes and the sdkconfig coexistence flag are useful verified additions.

## Answers to the Builder's questions

1. **Pairing UX** — presented to Felipe with my endorsement of your default (reuse of the known 2 s hold, pairing never arms, 5 s deliberate forget). His answer returns before Step 2; Step 1 may start immediately on APPROVED since the pure policy layer parameterizes both open choices.
2. **Transport priority** — presented to Felipe with my endorsement of wired-preferred, lock-on-every-switch. Simultaneous delivery stays out.
3. **COEX_IMU — Architect decision: APPROVED in ordinary firmware**, on these conditions (all already in your text, now binding): emission only while TCP-attached AND BLE-selected-secure AND armed; hard 30 s cap per physical arming with no self-retrigger; explicit start/end counters; no peer address or any new identifier in any record. A test-flag build and second OTA would double hardware risk for no safety gain; the bounded observable is consistent with the output-only telemetry philosophy.
4. **Async loss (MEDIUM)** — agreed, and your framing is adopted: macOS releasing host-side button state on disconnect is a Step 5 verification item and a release blocker if it fails.
5. **Cadence mismatch (MEDIUM)** — design accepted; pointer feel and stop-response remain hardware gates, not software claims.
6. **Compatibility (LOW)** — labeling correct; first compile is the gate.

## Verdict

**APPROVED.** The Builder session is now the primary session for this feature. First move is the handover's `next_action` continuation: Step 1 — branch `agent/ble-hid-pointer` from the refreshed tip and build the pure fail-closed policy with its native and Wokwi coverage. The two Felipe-reserved choices (pairing UX, transport priority) will be relayed through this Architect before Step 2 begins. Hardware steps remain gated on Felipe's explicit in-the-moment go, always via OTA.
