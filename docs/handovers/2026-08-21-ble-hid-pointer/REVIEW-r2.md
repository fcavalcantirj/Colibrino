---
verdict: APPROVED
round: 2
---

# Review r2

## Verdicts against the incident requirements

1. **Root cause (INCIDENT-1): satisfied as an evidence plan** — honestly still [UNVERIFIED] and labeled HIGH, which is the correct posture. The differential is rigorous: crash window bracketed to the BLE bring-up span, H1 (panic inside NimBLE host/controller init, live SYSINIT asserts) leading, heap exhaustion argued down with real numbers (true internal-DRAM accounting exposing PIO's hidden `.dram0.dummy` is a keeper). Two structural discoveries stand out and are adopted permanently: (a) `initArduino()` marks the OTA image VALID before `setup()` runs, so a setup-stage crash can never roll back today — that is WHY the boot loop persisted; (b) the USB PHY handoff explains the flapping console. The coredump-partition readback (dump likely still sitting there, untouched by 254fb7e boots) is the cheapest possible root-cause path.
2. **Cable lane (INCIDENT-2): satisfied** — honest about the one-console-window-per-hard-reset limit, breadcrumbs printed before `USB.begin()` as mitigation, `LAST_CRASH` self-report on the next boot, UART0 tap as the full solution (question forwarded to Felipe).
3. **Scope (INCIDENT-3): closed — the deviation was MY error, not the Builder's.** I diffed `254fb7e..521bc26` (device build → branch tip), which included eight master commits of my own authorship (`7970a26`, `02249a5`). `git diff master...agent/ble-hid-pointer` contains nothing under `sticks3/tools/**`; merge-base = master tip. PLAN-r1's exclusion list was honoured. Apology on the record.
4. **Ritual (INCIDENT-4): satisfied** — this document.

Also verified and welcomed: USB composite on `254fb7e` is now [REAL] (enumerates as Colibrino, CDC streams STATUS) — a long-standing unknown closed read-only; the SM_SC_ONLY functional defect (would have silently broken Just-Works HID reads on macOS) is exactly the class of finding the review lane exists for.

## Answers to the Builder's questions

1. **Step A first.** Read-only, one download-mode entry, and it informs the decision table BEFORE the candidate is built. Step B follows as the safety-net validation. (Felipe's go gates each.)
2. **Scope amendments 1–4: approved.** SC-only flag removal: approved. Host-task stack 8192: yes — cheap insurance.
3. **B before C: yes.** Order: A (readback) → implement → B (instrumentation-only OTA, proves rollback chain + self-reporting live) → decode/fix per the decision table → C (BLE candidate over cable + console).
4. **Health numbers: accepted as proposed** (healthy = setup + 10 s loop; 45 s deadline; 3 attempts; 1 s boot console window, Architect keeps the right to set it to 0 after validation).
5. **UART tap: forwarded to Felipe** — question pending: is a 3.3 V USB-TTL adapter available for the HAT2-Bus tap (G43/G44/GND)? Not a blocker; the breadcrumb lane suffices without it.
6. **INCIDENT-3 closed** per verdict 3 above.
7. **Rollback canary: deferred** — good idea, Felipe's call, after the feature lands; not in the critical path.

## Verdict

**APPROVED.** The Builder remains the primary session. Execution order: Step 0 gates → Step 1 (boot-health + breadcrumbs + SC-only fix commit) → Step A on Felipe's go → B → decision table → C → PLAN-r1 Steps 5–7. Every hardware touch — including the read-only download-mode readback — gets its own explicit go from Felipe. The three permanent safety gates (verifyRollbackLater override with C linkage, `check_image.py` static checks, boot self-reporting) are adopted as standing requirements for ALL future firmware work, not just this feature.
