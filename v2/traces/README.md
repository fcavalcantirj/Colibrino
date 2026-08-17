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

`NAME` carries only a run id and a segment role, e.g. `run07-guided`. The
first csv line must be exactly the header above (checked by ctest
`fixtures_manifest`).

### `NAME.labels.tsv`

Tab-separated, first line exactly:

```
id	kind	stage	start_ms	end_ms	expect_impulses	expect_clicks	expect_source	note
```

One row per segment. `start_ms` / `end_ms` are device milliseconds
(inclusive window on the event's own `t_ms`); `expect_*` is an integer or `*`
(unconstrained). Lines starting with `#` are ignored.

Enumerations (mirrored in `labels.schema.json`):

- `run_kind`: `guided_probe` (the firmware's three-window IMU probe) · `free_run` (button-started capture, optional)
- `mount`: `worn` · `bench` · `handheld`
- `labeler`: `host_cue` (cue times from the host script) · `human` · `tool`
- `stage`: `KEEP_HEAD_STILL` · `BLINK_FIRMLY` · `MOVE_HEAD` · `NONE`
- `expect_source`: `cue_window` (`[cue + 50 ms, cue + 650 ms]` until refined) · `human_refined` (±60 ms) · `operator_marker` · `derived`
- segment `kind`: `rest` · `head_sweep` · `hard_blink` · `soft_blink` · `confounder` · `coded_group` · `uniform_four` · `other`

Golden expectations by kind (owner-ratified round-1 semantics):

| kind | blink-dsp (`expect_impulses`) | blink-code (`expect_clicks`) |
|------|-------------------------------|------------------------------|
| `hard_blink` | 1 per cue window | 0 |
| `head_sweep` | 0 (above the head gate) | 0 |
| `coded_group` | `*` | 1 |
| `uniform_four` | `*` | 0 |
| `rest`, `soft_blink`, `confounder` | `*` (sub-pattern impulses are legitimate) | 0 |

## Privacy rules (public repository)

Fixture files and test output must contain **no** MAC address, hostname, IP,
SSID, absolute path, account name or time of day. Timestamps are device
milliseconds only. Raw device logs stay in the git-ignored
`sticks3/.device-backups/`; staging areas `traces/candidates/` and
`traces/private/` are git-ignored. Head bumps are public; chewing / talking /
walking confounders are private by default.

## Promotion steps (human-run)

1. Capture with the monitor-only host tool (no upload, no serial commands:
   the StickS3 CDC is write-only) into `traces/candidates/`.
2. Convert the `IMU,...` rows of one guided session to `NAME.csv`; write
   `NAME.labels.json` (cue times aligned to device ms as *estimated
   provenance*; refine by hand where ambiguous); derive `NAME.labels.tsv`.
3. Scrub: grep the three files for MAC / hostname / IP / paths; check the
   csv header line.
4. Move the three files into `traces/`, run `tools/update_manifest.sh`,
   then `ctest --test-dir build-host -R 'fixtures_manifest|golden'`.
5. Commit the four files together with a one-line message naming the run id.
   Fixtures are immutable afterwards; a correction is a new fixture.
