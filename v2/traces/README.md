# Promoted trace fixtures (the golden oracle)

This directory holds the **immutable, versioned fixture set** the golden tests
run against. It is empty until the human promotes real recordings; while it is
empty `blink_dsp_golden` / `blink_code_golden` **skip** under the developer
preset and **fail** under the oracle gate (`-DCOLIBRINO_V2_ORACLE=ON`). An
empty oracle never passes. Nothing here is edited by the loop.

## Artifact set (one fixture = three files, all listed in `MANIFEST.sha256`)

| file | producer | content |
|------|----------|---------|
| `NAME.csv` | capture tool (`sticks3/tools/`) | raw BMI270 stream, one row per sample: `t_ms,t_us,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g` (device ms + us kept for provenance; nominal 200 Hz, bursty dropouts) |
| `NAME.labels.json` | human / host script | canonical labels: run metadata + segments with ground-truth expectations (`labels.schema.json`) |
| `NAME.labels.tsv` | derived from the JSON by tooling | flat form the C tests read (no JSON parser in C) |
| `MANIFEST.sha256` | `tools/update_manifest.sh` | `<sha256>  <relative path>` for every fixture file; the tests treat a listed `NAME.csv` + `NAME.labels.tsv` as one promoted pair |

`NAME` is `YYYYMMDD-s<session>-<stage_lower>-<run_kind_lower>` (plus an
optional suffix, e.g. `20260815-s3-blink_firmly-hb_hard_singles`), assigned
by `sticks3/tools/make_trace_fixture.py`. The first csv line must be exactly
the header above (checked by ctest `fixtures_manifest`).

### `NAME.labels.tsv`

Derived deterministically from `NAME.labels.json` by
`sticks3/tools/make_trace_fixture.py` (`derive_tsv`); the C golden tests read
only this file (no JSON parser in C). Tab-separated, first line exactly:

```
# segment_name	kind	t_start_ms	t_end_ms	expect_kind	op	value	tolerance_ms
```

One row per expectation (a segment with two expectations yields two rows).
`t_start_ms` / `t_end_ms` are device milliseconds; the window is inclusive on
the event's own `t_ms` and is widened by `tolerance_ms` on both sides.
`expect_kind` is `IMPULSE` (checked by `blink_dsp_golden`) or
`CLICK_CANDIDATE` (checked by `blink_code_golden`); `op` is `eq`, `le`, or
`ge`; `value` is a non-negative integer. Any other token is a malformed label
and fails the run. Lines starting with `#` after the header are ignored.

Enumerations (closed lists; `labels.schema.json` and
`sticks3/tools/capture_common.py` must agree, pinned by the
`labels_schema_consistency` test):

- `run_kind`: `R0_bench` · `V_standard` · `HB_hard_singles` · `U_uniform_four` · `C_cued_coded` · `HS_sweeps` · `R_rest` · `SB_soft` · `CF2_bumps` · `CF1_confounders` · `BN_boundary`
- `mount`: `bench` · `glasses_right_temple` · `glasses_left_temple` · `headband_front`
- `labeler`: `owner` · `owner_reviewed` · `agent`
- `stage`: `KEEP_HEAD_STILL` · `BLINK_FIRMLY` · `MOVE_HEAD`
- `expect_source`: `human-labeled` · `instruction-window` (`[cue + 50 ms, cue + 650 ms]` until refined by a human to about ±60 ms)
- segment `kind`: `rest` · `natural_blink` · `hard_blink` · `coded_pattern` · `uniform_four` · `head_sweep` · `confounder`
- `commit_class`: `public` · `private`

## Privacy rules (public repository)

Fixture files and test output must contain **no** MAC address, hostname, IP,
SSID, absolute path, account name or time of day. Timestamps are device
milliseconds only. Raw device logs stay in the git-ignored
`sticks3/.device-backups/`; staging areas `traces/candidates/` and
`traces/private/` are git-ignored. Head bumps are public; chewing / talking /
walking confounders are private by default.

## Promotion steps (human-run)

1. Capture with `sticks3/tools/capture_session.py` (monitor-only: no upload,
   no serial commands; the StickS3 CDC is write-only) following
   `docs/V2_TRACE_CAPTURE_PROTOCOL.md`; raw logs stay under the ignored
   `sticks3/.device-backups/logs/`.
2. Run `sticks3/tools/make_trace_fixture.py <capture-dir>`: it writes
   `NAME.csv`, `NAME.labels.json`, and the derived `NAME.labels.tsv` into
   `traces/candidates/`, runs the de-identification guard, replays every
   session through the production detector, and lists `review_required`
   reasons (ambiguous cue refinements, parity mismatches). Refine ambiguous
   labels by hand; a fixture stays a candidate until its clearance line is set.
3. Promote with `make_trace_fixture.py --promote NAME` (refuses on
   review_required, missing clearance, private class, or any identifier hit)
   or move the three files by hand after the same checks.
4. Run `tools/update_manifest.sh`, then
   `ctest --test-dir build-host -R 'fixtures_manifest|golden'` and the
   `host-oracle` preset.
5. Commit the four files together with a one-line message naming the run id.
   Fixtures are immutable afterwards; a correction is a new fixture.
