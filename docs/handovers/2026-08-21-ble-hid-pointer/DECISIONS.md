# Felipe's ratified decisions (2026-08-21, via the Architect)

1. **Pairing UX**: the Builder's recommended default — in MOUSE mode with no ready transport, the 2 s Button A hold opens the bounded pairing window (60 s public when bondless, 30 s whitelist-only when bonded); the deliberate 5 s hold is the forget+re-pair flow; pairing never arms; arming is always its own separate 2 s hold.
2. **Transport priority**: wired-preferred, never simultaneous — USB data mount selects USB as the sole HID output; any topology change locks + releases both sides and requires a fresh 2 s arm; charge-only cables do not count as USB.
3. **COEX_IMU**: approved in ordinary firmware under the binding conditions in REVIEW-r1 answer 3.

Step 2 is unblocked. All hardware steps remain gated on Felipe's explicit in-the-moment OTA go.
