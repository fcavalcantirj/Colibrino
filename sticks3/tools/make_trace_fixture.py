#!/usr/bin/env python3
"""Turn StickS3 device logs / capture directories into v2 trace fixture candidates.

Usage
  make_trace_fixture.py INPUT [INPUT ...] [--out DIR] [--replay-bin BIN]
      [--capture-date YYYY-MM-DD] [--mount M] [--run-kind K] [--labeler L]
      [--firmware-commit SHA]
      INPUT is a raw device-monitor log or a capture_session.py directory.
      Writes NAME.csv, NAME.labels.json and NAME.labels.tsv per probe session
      and stage into <colibrino-root>/v2/traces/candidates/ (git-ignored).

  make_trace_fixture.py --accept-runs V1,V2 --session DIR [--session DIR ...]
      Worn-acceptance check: only the designated run ids count. Each run's
      latest valid attempt is sliced into its own log and replayed; every one
      must be RESULT=PASS with clean controls for the aggregate ACCEPT.

  make_trace_fixture.py --promote NAME [--dest v2/traces]
      Manual promotion gate for a reviewed candidate (refuses while
      review_required is true, while the labeler is still "agent", while
      cleared_on is null, or for private fixtures).

Fixture CSV values are copied verbatim from the device log. All outputs pass
the de-identification guard before anything is written.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import sys
import tempfile
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capture_common as cc  # noqa: E402

LABELS_SCHEMA = "colibrino-v2-trace-labels/1"
BOARD_MODEL = "M5Stack StickS3 (BMI270)"
NOMINAL_RATE_HZ = 200
PREROLL_MS = 2000
CSV_HEADER = "t_ms,t_us,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g"
TSV_HEADER = "# segment_name\tkind\tt_start_ms\tt_end_ms\texpect_kind\top\tvalue\ttolerance_ms"
REFINE_WINDOW_MS = (50, 650)
REFINE_AMBIGUOUS_RATIO = 0.8  # second peak within 20 % of the first
REFINE_MIN_SEPARATION_MS = 100
REFINE_MIN_PEAK_DPS = 0.5
REFINE_MIN_PEAK_MEDIAN_RATIO = 3.0

DEFAULT_STAGE_LABELS = {
    "KEEP_HEAD_STILL": {"kind": "rest", "whole": {"expect": {"CLICK_CANDIDATE": {"eq": 0}}, "tolerance_ms": 0}},
    "BLINK_FIRMLY": {"kind": "coded_pattern", "whole": {"expect": {"CLICK_CANDIDATE": {"ge": 2}}, "tolerance_ms": 0}},
    "MOVE_HEAD": {"kind": "head_sweep", "whole": {"expect": {"CLICK_CANDIDATE": {"eq": 0}}, "tolerance_ms": 0}},
}

_LOG_DATE_RE = re.compile(r"device-monitor-(\d{2})(\d{2})(\d{2})-\d{6}")
_CAPTURE_DATE_RE = re.compile(r"capture(?:-sim)?-(\d{4})(\d{2})(\d{2})T\d{6}")


class FixtureError(Exception):
    pass


# --------------------------------------------------------------------------
# Input loading
# --------------------------------------------------------------------------


class SourceInput:
    def __init__(self, path: str) -> None:
        self.path = os.path.abspath(path)
        if os.path.isdir(self.path):
            self.kind = "capture-session"
            self.raw_path = os.path.join(self.path, "raw.log")
            if not os.path.isfile(self.raw_path):
                raise FixtureError(f"{path}: capture directory has no raw.log")
            self.session_json = self._load_json(os.path.join(self.path, "session.json"))
            self.markers = self._load_markers(os.path.join(self.path, "markers.jsonl"))
        else:
            self.kind = "device-monitor-log"
            self.raw_path = self.path
            self.session_json = None
            self.markers = []
        if not os.path.isfile(self.raw_path):
            raise FixtureError(f"{path}: not found")
        self.lines = cc.read_lines(self.raw_path)
        self.sha256 = cc.sha256_file(self.raw_path)
        self.sessions = cc.split_sessions(self.lines)
        self.plan_runs: Dict[str, Dict[str, object]] = {}
        self._load_plan_runs()

    @staticmethod
    def _load_json(path: str):
        if not os.path.isfile(path):
            return None
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)

    @staticmethod
    def _load_markers(path: str) -> List[Dict[str, object]]:
        markers: List[Dict[str, object]] = []
        if not os.path.isfile(path):
            return markers
        with open(path, "r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                try:
                    markers.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
        return markers

    def _load_plan_runs(self) -> None:
        if not self.session_json:
            return
        plan_meta = self.session_json.get("plan")
        if not plan_meta or not plan_meta.get("path"):
            return
        plan_path = os.path.join(cc.repo_root(), str(plan_meta["path"]))
        if not os.path.isfile(plan_path):
            plan_path = os.path.join(cc.sticks3_dir(), "tools", "capture_plans", os.path.basename(str(plan_meta["path"])))
        if not os.path.isfile(plan_path):
            return
        try:
            plan = cc.load_plan(plan_path)
            session = cc.plan_session(plan, str(plan_meta.get("session_id", "A")))
        except (ValueError, KeyError, OSError):
            return
        for run in session.get("runs", []):
            self.plan_runs[str(run["run_id"])] = run

    def capture_date(self) -> Optional[str]:
        base = os.path.basename(self.path)
        match = _CAPTURE_DATE_RE.search(base)
        if match:
            return f"{match.group(1)}-{match.group(2)}-{match.group(3)}"
        match = _LOG_DATE_RE.search(os.path.basename(self.raw_path))
        if match:
            return f"20{match.group(1)}-{match.group(2)}-{match.group(3)}"
        return None

    def run_for_session(self, session_index: int) -> Optional[Dict[str, object]]:
        """Return {run_id, run_kind, mount, commit_class, attempt, spec} for a probe session."""
        if not self.session_json:
            return None
        for run in self.session_json.get("runs", []):
            for attempt in run.get("attempts", []):
                if attempt.get("session_index") == session_index:
                    return {
                        "run_id": run.get("run_id"),
                        "run_kind": run.get("run_kind"),
                        "mount": run.get("mount"),
                        "commit_class": run.get("commit_class"),
                        "run_status": run.get("status"),
                        "attempt": attempt,
                        "spec": self.plan_runs.get(str(run.get("run_id")), {}),
                    }
        return None

    def cues_for(self, session_index: int, stage: str) -> List[Dict[str, object]]:
        return [
            m
            for m in self.markers
            if m.get("type") == "cue" and m.get("session_index") == session_index and m.get("stage") == stage
        ]


# --------------------------------------------------------------------------
# Refinement: cue -> peak of first-difference gyro magnitude
# --------------------------------------------------------------------------


def first_difference_series(rows: Sequence[cc.ImuRow]) -> List[Tuple[int, float]]:
    series: List[Tuple[int, float]] = []
    for prev, cur in zip(rows, rows[1:]):
        dx = cur.values[0] - prev.values[0]
        dy = cur.values[1] - prev.values[1]
        dz = cur.values[2] - prev.values[2]
        series.append((cur.ms, math.sqrt(dx * dx + dy * dy + dz * dz)))
    return series


def refine_cue(series: Sequence[Tuple[int, float]], cue_ms: float, median_level: float) -> Dict[str, object]:
    lo = cue_ms + REFINE_WINDOW_MS[0]
    hi = cue_ms + REFINE_WINDOW_MS[1]
    window = [(ms, val) for ms, val in series if lo <= ms <= hi]
    result: Dict[str, object] = {
        "cue_ms": round(cue_ms, 1),
        "search_window_ms": [round(lo, 1), round(hi, 1)],
        "peak_ms": None,
        "peak_value_dps": None,
        "second_peak_ms": None,
        "second_peak_value_dps": None,
        "refine_status": "missed",
    }
    if not window:
        return result
    peak_ms, peak_val = max(window, key=lambda item: item[1])
    result["peak_ms"] = peak_ms
    result["peak_value_dps"] = round(peak_val, 4)
    threshold = max(REFINE_MIN_PEAK_DPS, REFINE_MIN_PEAK_MEDIAN_RATIO * median_level)
    if peak_val < threshold:
        return result
    # Second local peak, at least REFINE_MIN_SEPARATION_MS away from the first.
    second = None
    for ms, val in window:
        if abs(ms - peak_ms) < REFINE_MIN_SEPARATION_MS:
            continue
        if second is None or val > second[1]:
            second = (ms, val)
    if second is not None and second[1] >= REFINE_AMBIGUOUS_RATIO * peak_val:
        result["second_peak_ms"] = second[0]
        result["second_peak_value_dps"] = round(second[1], 4)
        result["refine_status"] = "ambiguous"
    else:
        result["refine_status"] = "ok"
    return result


# --------------------------------------------------------------------------
# Segment construction
# --------------------------------------------------------------------------


def _expect_ok(expect: Dict[str, object]) -> bool:
    if not isinstance(expect, dict) or not expect:
        return False
    for kind, spec in expect.items():
        if kind not in cc.EXPECT_KINDS or not isinstance(spec, dict) or not spec:
            return False
        for op, value in spec.items():
            if op not in cc.EXPECT_OPS or not isinstance(value, int):
                return False
    return True


def _segment(name: str, kind: str, start: float, end: float, expect: Dict[str, object], tolerance_ms: int, source: str, **extra) -> Dict[str, object]:
    if kind not in cc.SEGMENT_KINDS:
        raise FixtureError(f"segment kind {kind!r} is not allowed")
    if not _expect_ok(expect):
        raise FixtureError(f"segment {name}: malformed expect {expect!r}")
    if source not in cc.EXPECT_SOURCES:
        raise FixtureError(f"segment {name}: expect_source {source!r} not allowed")
    seg: Dict[str, object] = {
        "name": name,
        "kind": kind,
        "t_start_ms": int(round(start)),
        "t_end_ms": int(round(end)),
        "expect": expect,
        "tolerance_ms": int(tolerance_ms),
        "expect_source": source,
    }
    seg.update(extra)
    return seg


def build_segments(
    stage_block: cc.StageBlock,
    label_spec: Dict[str, object],
    cues: Sequence[Dict[str, object]],
    review: List[str],
) -> Tuple[List[Dict[str, object]], List[Dict[str, object]]]:
    """Segments + refinement records for one stage block."""
    rows = stage_block.rows
    t0 = rows[0].ms
    t1 = rows[-1].ms
    segments: List[Dict[str, object]] = []
    refinements: List[Dict[str, object]] = []
    stage_kind = str(label_spec.get("kind", "rest"))
    whole = label_spec.get("whole")
    if isinstance(whole, dict):
        segments.append(
            _segment(
                f"{stage_block.name.lower()}-whole",
                stage_kind,
                t0,
                t1,
                whole.get("expect", {}),
                int(whole.get("tolerance_ms", 0)),
                "instruction-window",
            )
        )
    for window in label_spec.get("windows", []) or []:
        segments.append(
            _segment(
                str(window["name"]),
                str(window.get("kind", stage_kind)),
                t0 + float(window["t_start_ms"]),
                min(t1, t0 + float(window["t_end_ms"])),
                window.get("expect", {}),
                int(window.get("tolerance_ms", 0)),
                "instruction-window",
            )
        )

    per_cue = label_spec.get("per_cue")
    per_group = label_spec.get("per_group")
    if cues and (per_cue or per_group):
        series = first_difference_series(rows)
        values = sorted(v for _, v in series)
        median_level = values[len(values) // 2] if values else 0.0
        by_group: Dict[str, List[Dict[str, object]]] = {}
        for cue in sorted(cues, key=lambda c: float(c.get("t_offset_ms", 0))):
            if cue.get("sound") != "beep":
                continue
            offset = float(cue.get("t_offset_ms", 0)) + float(cue.get("slip_ms", 0) or 0)
            cue_ms = t0 + offset
            refinement = refine_cue(series, cue_ms, median_level)
            refinement.update({"tag": cue.get("tag"), "group": cue.get("group"), "cue_mapping": "stage_event_relative"})
            refinements.append(refinement)
            status = refinement["refine_status"]
            if status != "ok":
                review.append(f"cue {cue.get('tag')}: refine_status={status}")
            if isinstance(per_cue, dict):
                if status == "ok":
                    start = float(refinement["peak_ms"]) - 100  # type: ignore[arg-type]
                    end = float(refinement["peak_ms"]) + 250  # type: ignore[arg-type]
                else:
                    start = cue_ms + REFINE_WINDOW_MS[0]
                    end = cue_ms + REFINE_WINDOW_MS[1]
                segments.append(
                    _segment(
                        f"cue-{cue.get('tag')}",
                        str(per_cue.get("kind", "hard_blink")),
                        max(t0, start),
                        min(t1, end),
                        per_cue.get("expect", {}),
                        int(per_cue.get("tolerance_ms", 100)),
                        "instruction-window",
                        cue_ms=round(cue_ms, 1),
                        peak_ms=refinement["peak_ms"],
                        refine_status=status,
                    )
                )
            group = cue.get("group")
            if group:
                by_group.setdefault(str(group), []).append(refinement)
        if isinstance(per_group, dict):
            for group_name, items in by_group.items():
                anchors = [float(i["peak_ms"]) if i["refine_status"] == "ok" else float(i["cue_ms"]) for i in items]  # type: ignore[arg-type]
                start = min(anchors) - float(per_group.get("pad_before_ms", 100))
                end = max(anchors) + float(per_group.get("pad_after_ms", 900))
                segments.append(
                    _segment(
                        f"group-{group_name}",
                        str(per_group.get("kind", stage_kind)),
                        max(t0, start),
                        min(t1, end),
                        per_group.get("expect", {}),
                        int(per_group.get("tolerance_ms", 200)),
                        "instruction-window",
                        cues=len(items),
                    )
                )
    return segments, refinements


# --------------------------------------------------------------------------
# TSV derivation
# --------------------------------------------------------------------------


def derive_tsv(labels: Dict[str, object]) -> str:
    lines = [TSV_HEADER]
    for seg in labels.get("segments", []):  # type: ignore[union-attr]
        for expect_kind in sorted(seg["expect"]):  # type: ignore[index]
            for op in sorted(seg["expect"][expect_kind]):  # type: ignore[index]
                value = seg["expect"][expect_kind][op]  # type: ignore[index]
                lines.append(
                    "\t".join(
                        [
                            str(seg["name"]),
                            str(seg["kind"]),
                            str(seg["t_start_ms"]),
                            str(seg["t_end_ms"]),
                            str(expect_kind),
                            str(op),
                            str(value),
                            str(seg["tolerance_ms"]),
                        ]
                    )
                )
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# Fixture generation
# --------------------------------------------------------------------------


def ensure_replay_bin(explicit: Optional[str]) -> Tuple[str, Optional[str]]:
    """Return (path, tempdir_or_None)."""
    if explicit:
        if not os.path.isfile(explicit):
            raise FixtureError(f"--replay-bin {explicit} not found")
        return explicit, None
    tmp = tempfile.mkdtemp(prefix="colibrino-replay-")
    path = os.path.join(tmp, "replay_imu_capture")
    cc.build_replay_tool(path)
    return path, tmp


def stage_fixture_name(date: str, session_index: int, stage: str, run_kind: str, suffix: str = "") -> str:
    name = f"{date.replace('-', '')}-s{session_index}-{stage.lower()}-{run_kind.lower()}"
    if suffix:
        if not re.match(r"^[a-z0-9]{1,8}$", suffix):
            raise FixtureError("--name-suffix must be 1-8 lowercase letters or digits")
        name += f"-{suffix}"
    return name


def make_fixtures_for_input(
    source: SourceInput,
    out_dir: str,
    replay_bin: str,
    args,
    printer,
) -> Dict[str, object]:
    date = args.capture_date or source.capture_date()
    if not date or not re.match(r"^\d{4}-\d{2}-\d{2}$", date):
        raise FixtureError(f"{os.path.basename(source.path)}: cannot derive capture date; pass --capture-date YYYY-MM-DD")
    replay = cc.run_replay(replay_bin, source.raw_path)
    replay_sessions: List[Dict[str, object]] = replay["sessions"]  # type: ignore[assignment]
    if len(replay_sessions) != len(source.sessions):
        printer(
            f"WARNING: replay tool found {len(replay_sessions)} sessions but the splitter found {len(source.sessions)}; parity uses index order"
        )
    written: List[str] = []
    review_list: List[Tuple[str, List[str]]] = []
    parity_matches = 0
    parity_mismatches = 0
    parity_unavailable = 0
    controls_clean_all = True
    firmware_commit = args.firmware_commit
    if firmware_commit is None and source.session_json:
        firmware_commit = None  # firmware build is not knowable from a capture; keep null unless given
    capture_tool_commit = source.session_json.get("colibrino_head") if source.session_json else None
    pending_writes: List[Tuple[str, str]] = []

    for session in source.sessions:
        idx = session.index
        rep = replay_sessions[idx - 1] if idx - 1 < len(replay_sessions) else None
        rep_counts = cc.replay_counts(rep) if rep else None
        rep_verdict = rep.get("result") if rep else None
        controls_clean = bool(rep_counts and rep_counts["still"] == 0 and rep_counts["head"] == 0)
        if rep_counts and not controls_clean:
            controls_clean_all = False
        firmware = session.result
        if firmware is None or rep_counts is None:
            parity = "no_firmware_result" if firmware is None else "no_replay"
            parity_unavailable += 1
        elif (
            firmware["still"] == rep_counts["still"]
            and firmware["blink"] == rep_counts["blink"]
            and firmware["head"] == rep_counts["head"]
            and firmware["verdict"] == rep_verdict
        ):
            parity = "match"
            parity_matches += 1
        else:
            parity = "mismatch"
            parity_mismatches += 1
        fw_text = (
            f"still={firmware['still']},blink={firmware['blink']},head={firmware['head']},{firmware['verdict']}"
            if firmware
            else "none"
        )
        rp_text = (
            f"still={rep_counts['still']},blink={rep_counts['blink']},head={rep_counts['head']},{rep_verdict}"
            if rep_counts
            else "none"
        )
        printer(f"PARITY s{idx} firmware={fw_text} replay={rp_text} -> {parity.upper()}")

        run_info = source.run_for_session(idx)
        run_id = None
        mount = args.mount
        commit_class = "public"
        spec: Dict[str, object] = {}
        review_base: List[str] = []
        if run_info:
            run_id = run_info["run_id"]
            run_kind = run_info.get("run_kind") or args.run_kind
            mount = run_info.get("mount") or args.mount
            commit_class = run_info.get("commit_class") or "public"
            spec = run_info.get("spec") or {}
            attempt = run_info.get("attempt") or {}
            if attempt.get("status") != "valid" or run_info.get("run_status") in ("invalid",):
                printer(f"skip s{idx}: run {run_id} attempt is {attempt.get('status')} / run status {run_info.get('run_status')}")
                continue
            if not run_info.get("run_kind"):
                review_base.append("run_kind not in capture plan; CLI default used")
        else:
            run_kind = args.run_kind
            if not args.explicit_run_kind or not args.explicit_mount:
                review_base.append("unmarked log: run_kind/mount are CLI defaults; confirm during review")
        if run_kind not in cc.RUN_KINDS:
            raise FixtureError(f"run_kind {run_kind!r} not in {cc.RUN_KINDS}")
        if mount not in cc.MOUNTS:
            raise FixtureError(f"mount {mount!r} not in {cc.MOUNTS}")
        if args.labeler not in cc.LABELERS:
            raise FixtureError(f"labeler {args.labeler!r} not in {cc.LABELERS}")
        if parity == "mismatch":
            review_base.append("firmware RESULT and host replay disagree (different detector build or dropped lines)")

        for stage_name in cc.CAPTURE_STAGES:
            block = session.stages.get(stage_name)
            if block is None or not block.rows:
                continue
            labels_spec = (spec.get("labels", {}) or {}).get(stage_name) if spec else None
            if not isinstance(labels_spec, dict):
                labels_spec = DEFAULT_STAGE_LABELS[stage_name]
            review = list(review_base)
            cues = source.cues_for(idx, stage_name)
            segments, refinements = build_segments(block, labels_spec, cues, review)
            stage_rep = (rep or {}).get("stages", {}).get(stage_name) if rep else None  # type: ignore[union-attr]
            if isinstance(labels_spec.get("per_group"), dict) and stage_rep is not None:
                expected_groups = len({c.get("group") for c in cues if c.get("group") and c.get("sound") == "beep"})
                group_expect = labels_spec["per_group"].get("expect", {}).get("CLICK_CANDIDATE", {})  # type: ignore[index]
                if group_expect.get("eq") == 1 and expected_groups and stage_rep["sequences"] != expected_groups:  # type: ignore[index]
                    review.append(f"coded groups={expected_groups} but replay sequences={stage_rep['sequences']}")  # type: ignore[index]
            name = stage_fixture_name(date, idx, stage_name, run_kind, args.name_suffix or "")
            csv_text = CSV_HEADER + "\n" + "".join(
                f"{row.ms},{row.usec}," + ",".join(row.raw_values) + "\n" for row in block.rows
            )
            csv_bytes = csv_text.encode("ascii")
            rate = block.effective_rate_hz()
            labels: Dict[str, object] = {
                "schema": LABELS_SCHEMA,
                "name": name,
                "csv": name + ".csv",
                "csv_sha256": cc.sha256_bytes(csv_bytes),
                "csv_rows": len(block.rows),
                "capture": {
                    "board_model": BOARD_MODEL,
                    "firmware_commit": firmware_commit,
                    "nominal_rate_hz": NOMINAL_RATE_HZ,
                    "effective_rate_hz": round(rate, 1) if rate else None,
                    "session_index": idx,
                    "stage": stage_name,
                    "run_kind": run_kind,
                    "run_id": run_id,
                    "mount": mount,
                    "commit_class": commit_class,
                    "capture_date": date,
                    "stage_first_ms": block.rows[0].ms,
                    "stage_last_ms": block.rows[-1].ms,
                },
                "harness": {"preroll_ms": PREROLL_MS},
                "segments": segments,
                "cue_refinement": refinements,
                "replay": {
                    "tool": "sticks3/tools/replay_imu_capture.cpp",
                    "stage_samples": stage_rep["samples"] if stage_rep else None,  # type: ignore[index]
                    "stage_impulses": stage_rep["impulses"] if stage_rep else None,  # type: ignore[index]
                    "stage_sequences": stage_rep["sequences"] if stage_rep else None,  # type: ignore[index]
                    "session_controls_clean": controls_clean if rep_counts else None,
                    "session_replay_result": ({"verdict": rep_verdict, **rep_counts} if rep_counts else None),
                    "session_firmware_result": firmware,
                    "parity": parity,
                },
                "provenance": {
                    "source_sha256": source.sha256,
                    "source_kind": source.kind,
                    "capture_tool_commit": capture_tool_commit,
                    "labeler": args.labeler,
                    "deidentified": True,
                    "cleared_on": None,
                },
                "review_required": bool(review),
                "review_reasons": review,
            }
            json_text = cc.json_dumps_stable(labels)
            tsv_text = derive_tsv(labels)
            for suffix, text in ((".csv", csv_text), (".labels.json", json_text), (".labels.tsv", tsv_text)):
                fired = cc.deid_scan(text)
                if fired:
                    raise FixtureError(
                        f"de-identification guard refused {name}{suffix}: rules fired = {', '.join(fired)}; nothing written"
                    )
                pending_writes.append((os.path.join(out_dir, name + suffix), text))
            if review:
                review_list.append((name, review))
            printer(
                f"FIXTURE {name}: rows={len(block.rows)} hz={round(rate, 1) if rate else '?'} segments={len(segments)} "
                f"replay_seq={stage_rep['sequences'] if stage_rep else '?'} review_required={'yes' if review else 'no'}"  # type: ignore[index]
            )

    # All outputs of this input passed the guard: check collisions, then write.
    for path, text in pending_writes:
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as handle:
                if handle.read() != text:
                    raise FixtureError(
                        f"{os.path.basename(path)} already exists with different content; "
                        "use --out for a fresh directory or --name-suffix to distinguish same-day inputs; nothing written"
                    )
    os.makedirs(out_dir, exist_ok=True)
    for path, text in pending_writes:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        written.append(path)

    if review_list:
        printer("REVIEW REQUIRED:")
        for name, reasons in review_list:
            printer(f"  {name}: " + " | ".join(reasons))
    printer(
        f"SUMMARY input={os.path.basename(source.raw_path) if source.kind == 'device-monitor-log' else os.path.basename(source.path)} "
        f"sessions={len(source.sessions)} controls_clean={'yes' if controls_clean_all else 'no'} "
        f"parity_matches={parity_matches} parity_mismatches={parity_mismatches} parity_unavailable={parity_unavailable} "
        f"fixtures_written={len(written)} review_required={len(review_list)}"
    )
    return {
        "sessions": len(source.sessions),
        "controls_clean": controls_clean_all,
        "parity_matches": parity_matches,
        "parity_mismatches": parity_mismatches,
        "written": written,
        "review_required": [name for name, _ in review_list],
    }


# --------------------------------------------------------------------------
# --accept-runs
# --------------------------------------------------------------------------


def accept_runs(run_ids: Sequence[str], session_dirs: Sequence[str], replay_bin: str, printer) -> bool:
    designated = [r.strip() for r in run_ids if r.strip()]
    if not designated:
        raise FixtureError("--accept-runs needs at least one run id")
    sources = [SourceInput(d) for d in session_dirs]
    rows: List[Dict[str, object]] = []
    for run_id in designated:
        found = None
        for source in sources:
            if not source.session_json:
                continue
            for run in source.session_json.get("runs", []):
                if run.get("run_id") != run_id:
                    continue
                valid_attempts = [a for a in run.get("attempts", []) if a.get("status") == "valid"]
                candidate = valid_attempts[-1] if valid_attempts else None
                found = {"source": source, "run": run, "attempt": candidate}
        if found is None:
            rows.append({"run_id": run_id, "found": False, "verdict": "MISSING"})
            continue
        attempt = found["attempt"]
        if attempt is None:
            rows.append({"run_id": run_id, "found": True, "capture": os.path.basename(found["source"].path), "verdict": "NO_VALID_ATTEMPT", "run_status": found["run"].get("status")})
            continue
        source: SourceInput = found["source"]
        session_index = int(attempt.get("session_index", 0))
        session = next((s for s in source.sessions if s.index == session_index), None)
        if session is None:
            rows.append({"run_id": run_id, "found": True, "capture": os.path.basename(source.path), "verdict": "SESSION_NOT_IN_RAW"})
            continue
        start, end = session.line_range()
        with tempfile.NamedTemporaryFile("w", suffix=".log", prefix=f"run-{run_id}-", delete=False, encoding="utf-8") as handle:
            for line in source.lines[start : end + 1]:
                handle.write(line + "\n")
            slice_path = handle.name
        try:
            replay = cc.run_replay(replay_bin, slice_path)
        finally:
            try:
                os.unlink(slice_path)
            except OSError:
                pass
        entry = replay["sessions"][0] if replay["sessions"] else None  # type: ignore[index]
        counts = cc.replay_counts(entry) if entry else None
        verdict_ok = bool(entry and entry.get("result") == "PASS" and counts and counts["still"] == 0 and counts["head"] == 0)
        rows.append(
            {
                "run_id": run_id,
                "found": True,
                "capture": os.path.basename(source.path),
                "session_index": session_index,
                "run_status": found["run"].get("status"),
                "replay": counts,
                "replay_result": entry.get("result") if entry else None,
                "replay_sessions_in_slice": len(replay["sessions"]),  # type: ignore[arg-type]
                "verdict": "PASS" if verdict_ok else "FAIL",
            }
        )
    printer("run_id\tcapture\tsession\trun_status\treplay(still/blink/head)\treplay_result\tverdict")
    all_ok = True
    for row in rows:
        counts = row.get("replay")
        counts_text = f"{counts['still']}/{counts['blink']}/{counts['head']}" if counts else "-"
        printer(
            "\t".join(
                [
                    str(row.get("run_id")),
                    str(row.get("capture", "-")),
                    str(row.get("session_index", "-")),
                    str(row.get("run_status", "-")),
                    counts_text,
                    str(row.get("replay_result", "-")),
                    str(row.get("verdict")),
                ]
            )
        )
        if row.get("verdict") != "PASS":
            all_ok = False
    printer(f"ACCEPTANCE designated={','.join(designated)} verdict={'ACCEPT' if all_ok else 'REJECT'} (only designated runs count)")
    return all_ok


# --------------------------------------------------------------------------
# --promote
# --------------------------------------------------------------------------


def promote(name: str, candidates_dir: str, dest_dir: str, printer) -> None:
    json_path = os.path.join(candidates_dir, name + ".labels.json")
    if not os.path.isfile(json_path):
        raise FixtureError(f"{name}: no candidate labels at {json_path}")
    with open(json_path, "r", encoding="utf-8") as handle:
        labels = json.load(handle)
    problems = []
    if labels.get("review_required"):
        problems.append("review_required is true")
    if labels.get("provenance", {}).get("labeler") == "agent":
        problems.append("labeler is still 'agent' (needs owner or owner_reviewed)")
    if labels.get("provenance", {}).get("cleared_on") in (None, ""):
        problems.append("provenance.cleared_on is null")
    if labels.get("capture", {}).get("commit_class") == "private":
        problems.append("commit_class is private")
    files = [name + ".csv", name + ".labels.json", name + ".labels.tsv"]
    for fname in files:
        path = os.path.join(candidates_dir, fname)
        if not os.path.isfile(path):
            problems.append(f"missing {fname}")
            continue
        with open(path, "r", encoding="utf-8") as handle:
            fired = cc.deid_scan(handle.read())
        if fired:
            problems.append(f"{fname}: de-identification rules fired = {', '.join(fired)}")
    if derive_tsv(labels) != open(os.path.join(candidates_dir, name + ".labels.tsv"), "r", encoding="utf-8").read():
        problems.append("labels.tsv is not the derivation of labels.json")
    if problems:
        raise FixtureError(f"promotion refused for {name}: " + "; ".join(problems))
    os.makedirs(dest_dir, exist_ok=True)
    manifest_path = os.path.join(dest_dir, "MANIFEST.sha256")
    for fname in files:
        src = os.path.join(candidates_dir, fname)
        dst = os.path.join(dest_dir, fname)
        if os.path.exists(dst):
            raise FixtureError(f"{fname} already exists in {dest_dir}; promoted fixtures are immutable")
        shutil.copyfile(src, dst)
    with open(manifest_path, "a", encoding="utf-8") as handle:
        for fname in files:
            handle.write(f"{cc.sha256_file(os.path.join(dest_dir, fname))}  {fname}\n")
    printer(f"promoted {name} -> {dest_dir} (MANIFEST.sha256 appended)")


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------


def default_out_dir() -> str:
    return os.path.join(cc.repo_root(), "v2", "traces", "candidates")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="*", help="raw device logs or capture_session.py directories")
    parser.add_argument("--out", help="output directory (default <colibrino-root>/v2/traces/candidates)")
    parser.add_argument("--replay-bin", help="compiled replay_imu_capture (built into a temp dir when absent)")
    parser.add_argument("--capture-date", help="YYYY-MM-DD (derived from the input name when absent)")
    parser.add_argument("--mount", default="glasses_right_temple", choices=cc.MOUNTS)
    parser.add_argument("--run-kind", default="V_standard", choices=cc.RUN_KINDS, help="run kind for logs without a capture plan")
    parser.add_argument("--labeler", default="agent", choices=cc.LABELERS)
    parser.add_argument("--firmware-commit", help="firmware commit that produced the log (recorded verbatim, null when unknown)")
    parser.add_argument("--name-suffix", help="1-8 lowercase letters/digits appended to fixture names to separate same-day inputs")
    parser.add_argument("--accept-runs", help="comma separated designated run ids (worn acceptance)")
    parser.add_argument("--session", action="append", default=[], help="capture directory for --accept-runs (repeatable)")
    parser.add_argument("--promote", metavar="NAME", help="promote a reviewed candidate into --dest")
    parser.add_argument("--dest", help="promotion destination (default <colibrino-root>/v2/traces)")
    args = parser.parse_args(argv)
    args.explicit_run_kind = any(a.startswith("--run-kind") for a in (argv if argv is not None else sys.argv[1:]))
    args.explicit_mount = any(a.startswith("--mount") for a in (argv if argv is not None else sys.argv[1:]))

    def printer(text: str) -> None:
        sys.stdout.write(text + "\n")
        sys.stdout.flush()

    tmpdir = None
    try:
        if args.promote:
            promote(args.promote, args.out or default_out_dir(), args.dest or os.path.join(cc.repo_root(), "v2", "traces"), printer)
            return 0
        if args.accept_runs:
            if not args.session:
                raise FixtureError("--accept-runs needs at least one --session DIR")
            replay_bin, tmpdir = ensure_replay_bin(args.replay_bin)
            ok = accept_runs(args.accept_runs.split(","), args.session, replay_bin, printer)
            return 0 if ok else 4
        if not args.inputs:
            parser.print_usage()
            return 2
        replay_bin, tmpdir = ensure_replay_bin(args.replay_bin)
        out_dir = args.out or default_out_dir()
        for path in args.inputs:
            source = SourceInput(path)
            make_fixtures_for_input(source, out_dir, replay_bin, args, printer)
        return 0
    except FixtureError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 5
    finally:
        if tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
