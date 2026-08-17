#!/usr/bin/env python3
"""Shared, stdlib-only helpers for the StickS3 v2 trace capture tooling.

Used by capture_session.py (live monitor / simulate), make_trace_fixture.py
(raw log -> fixture candidates) and tools/tests/test_capture_tools.py.

Everything here is host-side and read-only with respect to the device: the
StickS3 CDC channel is write-only from the firmware's point of view, so these
helpers only parse the lines the firmware prints.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

CAPTURE_STAGES = ("KEEP_HEAD_STILL", "BLINK_FIRMLY", "MOVE_HEAD")
PREPARE_FOR_STAGE = {
    "PREPARE_STILL": "KEEP_HEAD_STILL",
    "PREPARE_BLINKS": "BLINK_FIRMLY",
    "PREPARE_HEAD": "MOVE_HEAD",
}
RUN_KINDS = (
    "R0_bench",
    "V_standard",
    "HB_hard_singles",
    "U_uniform_four",
    "C_cued_coded",
    "HS_sweeps",
    "R_rest",
    "SB_soft",
    "CF2_bumps",
    "CF1_confounders",
    "BN_boundary",
)
MOUNTS = ("bench", "glasses_right_temple", "glasses_left_temple", "headband_front")
LABELERS = ("owner", "owner_reviewed", "agent")
EXPECT_SOURCES = ("human-labeled", "instruction-window")
SEGMENT_KINDS = (
    "rest",
    "natural_blink",
    "hard_blink",
    "coded_pattern",
    "uniform_four",
    "head_sweep",
    "confounder",
)
EXPECT_KINDS = ("IMPULSE", "CLICK_CANDIDATE")
EXPECT_OPS = ("eq", "le", "ge")
COMMIT_CLASSES = ("public", "private")

USB_MANUFACTURER = "Colibrino"
USB_PRODUCT = "Colibrino StickS3 Prototype"
ENV_SERIAL_KEY = "COLIBRINO_USB_SERIAL"

_IMU_RE = re.compile(
    r"^IMU,(\d+),(\d+),([A-Z_]+),([^,\s]+),([^,\s]+),([^,\s]+),([^,\s]+),([^,\s]+),([^,\s]+)\s*$"
)
_STATUS_RE = re.compile(r"^STATUS,(\d+),(.*)$")
_EVENT_RE = re.compile(r"^EVENT,([A-Z0-9_]+)(?:,(.*))?$")
_RESULT_RE = re.compile(
    r"^RESULT,IMU_BLINK,(PASS|NOT_PROVEN),still=(\d+),blink=(\d+),head=(\d+)"
)


# --------------------------------------------------------------------------
# Repository helpers
# --------------------------------------------------------------------------


def tools_dir() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def sticks3_dir() -> str:
    return os.path.dirname(tools_dir())


def repo_root() -> str:
    return os.path.dirname(sticks3_dir())


def git_head(path: Optional[str] = None) -> Optional[str]:
    """`git rev-parse HEAD` of the repository containing `path` (or this file)."""
    cwd = path or tools_dir()
    try:
        out = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=cwd,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    return out.stdout.strip() or None


def parse_env_file(path: str) -> Dict[str, str]:
    """Parse KEY=VALUE lines. Callers must only ever surface the keys they need."""
    values: Dict[str, str] = {}
    if not os.path.isfile(path):
        return values
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[len("export ") :].strip()
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            values[key] = value
    return values


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# --------------------------------------------------------------------------
# Line parsing
# --------------------------------------------------------------------------


@dataclass
class ImuRow:
    line_index: int
    ms: int
    usec: int
    stage: str
    raw_values: Tuple[str, str, str, str, str, str]
    values: Tuple[float, float, float, float, float, float]

    @property
    def gyro(self) -> Tuple[float, float, float]:
        return self.values[0], self.values[1], self.values[2]


def parse_imu(line: str, line_index: int = -1) -> Optional[ImuRow]:
    match = _IMU_RE.match(line)
    if match is None:
        return None
    raw_values = tuple(match.group(i) for i in range(4, 10))
    try:
        values = tuple(float(v) for v in raw_values)
    except ValueError:
        return None
    return ImuRow(
        line_index=line_index,
        ms=int(match.group(1)),
        usec=int(match.group(2)),
        stage=match.group(3),
        raw_values=raw_values,  # type: ignore[arg-type]
        values=values,  # type: ignore[arg-type]
    )


def parse_status(line: str) -> Optional[Tuple[int, Dict[str, str]]]:
    match = _STATUS_RE.match(line)
    if match is None:
        return None
    fields: Dict[str, str] = {}
    for item in match.group(2).split(","):
        if "=" in item:
            key, value = item.split("=", 1)
            fields[key.strip()] = value.strip()
    return int(match.group(1)), fields


def parse_event(line: str) -> Optional[Tuple[str, List[str]]]:
    match = _EVENT_RE.match(line)
    if match is None:
        return None
    args = match.group(2)
    return match.group(1), (args.split(",") if args else [])


def parse_result(line: str) -> Optional[Dict[str, object]]:
    match = _RESULT_RE.match(line)
    if match is None:
        return None
    return {
        "verdict": match.group(1),
        "still": int(match.group(2)),
        "blink": int(match.group(3)),
        "head": int(match.group(4)),
    }


def classify_line(line: str) -> str:
    if line.startswith("IMU,"):
        return "IMU"
    if line.startswith("STATUS,"):
        return "STATUS"
    if line.startswith("EVENT,"):
        return "EVENT"
    if line.startswith("RESULT,"):
        return "RESULT"
    if line.startswith("CSV:"):
        return "CSV"
    if line.startswith("IR,"):
        return "IR"
    return "OTHER"


# --------------------------------------------------------------------------
# Session splitting (mirrors sticks3/tools/replay_imu_capture.cpp)
# --------------------------------------------------------------------------


@dataclass
class StageBlock:
    name: str
    rows: List[ImuRow] = field(default_factory=list)
    event_line: Optional[int] = None  # line index of EVENT,IMU_PROBE_STAGE,<name>

    @property
    def first_ms(self) -> Optional[int]:
        return self.rows[0].ms if self.rows else None

    @property
    def last_ms(self) -> Optional[int]:
        return self.rows[-1].ms if self.rows else None

    def effective_rate_hz(self) -> Optional[float]:
        if len(self.rows) < 2:
            return None
        span_ms = self.rows[-1].ms - self.rows[0].ms
        if span_ms <= 0:
            return None
        return (len(self.rows) - 1) * 1000.0 / span_ms


@dataclass
class ProbeSession:
    index: int  # 1-based, in log order
    start_line: int
    end_line: int = -1
    explicit_start: bool = False
    stages: Dict[str, StageBlock] = field(default_factory=dict)
    stage_order: List[str] = field(default_factory=list)
    prepare_lines: Dict[str, int] = field(default_factory=dict)
    result: Optional[Dict[str, object]] = None
    result_line: Optional[int] = None
    complete_samples: Optional[int] = None
    candidate_events: List[Tuple[int, str, str]] = field(default_factory=list)

    def stage(self, name: str) -> StageBlock:
        if name not in self.stages:
            self.stages[name] = StageBlock(name=name)
            self.stage_order.append(name)
        return self.stages[name]

    def imu_rows(self) -> List[ImuRow]:
        rows: List[ImuRow] = []
        for name in self.stage_order:
            rows.extend(self.stages[name].rows)
        return rows

    def line_range(self) -> Tuple[int, int]:
        return self.start_line, self.end_line


def split_sessions(lines: Sequence[str]) -> List[ProbeSession]:
    """Split raw device lines into guided probe sessions.

    Boundary rules are identical to replay_imu_capture.cpp: an explicit
    `EVENT,IMU_PROBE_STARTED` line opens a session; as a fallback a
    KEEP_HEAD_STILL row directly after MOVE_HEAD rows also opens one.
    """
    sessions: List[ProbeSession] = []
    session_open = False
    previous_stage_name: Optional[str] = None
    pending_stage_events: Dict[str, int] = {}
    pending_prepare: Dict[str, int] = {}

    def open_session(index: int, explicit: bool) -> ProbeSession:
        session = ProbeSession(index=len(sessions) + 1, start_line=index, explicit_start=explicit)
        session.end_line = index
        sessions.append(session)
        return session

    for index, raw in enumerate(lines):
        line = raw.rstrip("\r\n")
        if line.startswith("EVENT,IMU_PROBE_STARTED"):
            open_session(index, explicit=True)
            session_open = True
            previous_stage_name = None
            pending_stage_events = {}
            pending_prepare = {}
            continue

        event = parse_event(line) if line.startswith("EVENT,") else None
        if event is not None:
            name, args = event
            if name == "IMU_PROBE_STAGE" and args:
                stage_name = args[0]
                if stage_name in CAPTURE_STAGES:
                    if session_open and sessions:
                        sessions[-1].stage(stage_name).event_line = index
                        sessions[-1].end_line = index
                    else:
                        pending_stage_events[stage_name] = index
                elif stage_name in PREPARE_FOR_STAGE:
                    if session_open and sessions:
                        sessions[-1].prepare_lines[stage_name] = index
                        sessions[-1].end_line = index
                    else:
                        pending_prepare[stage_name] = index
            elif name == "IMU_PROBE_COMPLETE" and session_open and sessions:
                for arg in args:
                    if arg.startswith("samples="):
                        try:
                            sessions[-1].complete_samples = int(arg.split("=", 1)[1])
                        except ValueError:
                            pass
                sessions[-1].end_line = index
            elif name in ("IMU_BLINK_SEQUENCE_CANDIDATE", "IMU_DOUBLE_BLINK_CANDIDATE"):
                if session_open and sessions:
                    stage_name = args[0] if args else ""
                    sessions[-1].candidate_events.append((index, name, stage_name))
                    sessions[-1].end_line = index
            continue

        if line.startswith("RESULT,"):
            result = parse_result(line)
            if result is not None and session_open and sessions:
                sessions[-1].result = result
                sessions[-1].result_line = index
                sessions[-1].end_line = index
            continue

        row = parse_imu(line, index)
        if row is None:
            continue
        still_after_head = previous_stage_name == "MOVE_HEAD" and row.stage == "KEEP_HEAD_STILL"
        if not session_open or still_after_head:
            open_session(index, explicit=False)
            session_open = True
            if pending_stage_events or pending_prepare:
                for stage_name, event_index in pending_stage_events.items():
                    sessions[-1].stage(stage_name).event_line = event_index
                sessions[-1].prepare_lines.update(pending_prepare)
                pending_stage_events = {}
                pending_prepare = {}
        session = sessions[-1]
        if row.stage not in CAPTURE_STAGES:
            continue
        session.stage(row.stage).rows.append(row)
        session.end_line = index
        previous_stage_name = row.stage

    return sessions


def read_lines(path: str) -> List[str]:
    with open(path, "r", encoding="utf-8", errors="replace", newline="") as handle:
        return handle.read().splitlines()


# --------------------------------------------------------------------------
# Time alignment (estimated provenance)
# --------------------------------------------------------------------------

ALIGNMENT_METHOD = "lower_envelope_p01"


def estimate_alignment(pairs: Iterable[Tuple[int, int]]) -> Optional[Dict[str, object]]:
    """Estimate host_ns = device_ms*1e6 + offset_ns from (host_ns, device_ms).

    The device sends lines slightly after the sample time; USB and host
    scheduling only ever add delay, so the lower envelope of
    (host_ns - device_ms*1e6) tracks the true offset. The 1st percentile is
    used instead of the minimum to be robust to a stray early line.
    """
    diffs = sorted(host_ns - device_ms * 1_000_000 for host_ns, device_ms in pairs)
    if not diffs:
        return None
    count = len(diffs)

    def pick(fraction: float) -> int:
        idx = int(round(fraction * (count - 1)))
        return diffs[max(0, min(count - 1, idx))]

    offset_ns = pick(0.01)
    residuals_ms = [(d - offset_ns) / 1e6 for d in diffs]
    return {
        "method": ALIGNMENT_METHOD,
        "provenance": "ESTIMATED",
        "samples": count,
        "offset_ns": int(offset_ns),
        "residual_ms": {
            "min": round(residuals_ms[0], 3),
            "p01": round((pick(0.01) - offset_ns) / 1e6, 3),
            "p50": round((pick(0.50) - offset_ns) / 1e6, 3),
            "p99": round((pick(0.99) - offset_ns) / 1e6, 3),
            "max": round(residuals_ms[-1], 3),
        },
    }


# --------------------------------------------------------------------------
# De-identification guard
# --------------------------------------------------------------------------

DEID_RULES: List[Tuple[str, "re.Pattern[str]"]] = [
    ("mac_address", re.compile(r"(?<![0-9A-Za-z])[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}(?![0-9A-Za-z])")),
    ("device_mac_literal", re.compile(r"(?i)AC276ED268B8")),
    ("device_hostname", re.compile(r"(?i)sticks3-ptt")),
    ("ipv4_address", re.compile(r"(?<![0-9.])(?:25[0-5]|2[0-4]\d|1?\d?\d)(?:\.(?:25[0-5]|2[0-4]\d|1?\d?\d)){3}(?![0-9.])")),
    ("home_path", re.compile(r"/Users/")),
    ("ssid_like_key", re.compile(r"(?i)(?:\bssid\b|wifi_ssid|wifi_pass|\bpsk\b|passphrase|password)")),
    ("time_of_day", re.compile(r"(?<!\d)(?:[01]\d|2[0-3]):[0-5]\d:[0-5]\d(?!\d)")),
]


def deid_scan(text: str) -> List[str]:
    """Return the names of every de-identification rule that fires on `text`."""
    fired: List[str] = []
    for name, pattern in DEID_RULES:
        if pattern.search(text):
            fired.append(name)
    return fired


# --------------------------------------------------------------------------
# Replay tool wrapper
# --------------------------------------------------------------------------

REPLAY_SOURCE = os.path.join("tools", "replay_imu_capture.cpp")
DETECTOR_SOURCE = os.path.join("src", "imu_blink_detector.cpp")


def build_replay_tool(output_path: str, sticks3_root: Optional[str] = None) -> str:
    root = sticks3_root or sticks3_dir()
    cmd = [
        "c++",
        "-std=c++17",
        "-Iinclude",
        REPLAY_SOURCE,
        DETECTOR_SOURCE,
        "-o",
        output_path,
    ]
    subprocess.run(cmd, cwd=root, check=True, capture_output=True, text=True)
    return output_path


_REPLAY_STAGE_RE = re.compile(
    r"^SESSION (\d+) (KEEP_HEAD_STILL|BLINK_FIRMLY|MOVE_HEAD) samples=(\d+) impulses=(\d+) sequences=(\d+)$"
)
_REPLAY_RESULT_RE = re.compile(r"^SESSION (\d+) RESULT=(PASS|NOT_PROVEN)$")
_REPLAY_SUMMARY_RE = re.compile(
    r"^SUMMARY sessions=(\d+) controls_clean=(yes|no) validation_passes=(\d+)$"
)


def run_replay(replay_bin: str, log_path: str) -> Dict[str, object]:
    """Run the compiled replay tool and parse its report."""
    proc = subprocess.run(
        [replay_bin, log_path], capture_output=True, text=True, timeout=600
    )
    sessions: Dict[int, Dict[str, object]] = {}
    summary: Optional[Dict[str, object]] = None
    for line in proc.stdout.splitlines():
        match = _REPLAY_STAGE_RE.match(line)
        if match:
            idx = int(match.group(1))
            entry = sessions.setdefault(idx, {"stages": {}, "result": None})
            entry["stages"][match.group(2)] = {  # type: ignore[index]
                "samples": int(match.group(3)),
                "impulses": int(match.group(4)),
                "sequences": int(match.group(5)),
            }
            continue
        match = _REPLAY_RESULT_RE.match(line)
        if match:
            idx = int(match.group(1))
            entry = sessions.setdefault(idx, {"stages": {}, "result": None})
            entry["result"] = match.group(2)
            continue
        match = _REPLAY_SUMMARY_RE.match(line)
        if match:
            summary = {
                "sessions": int(match.group(1)),
                "controls_clean": match.group(2) == "yes",
                "validation_passes": int(match.group(3)),
            }
    return {
        "exit_code": proc.returncode,
        "sessions": [sessions[k] for k in sorted(sessions)],
        "summary": summary,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    }


def replay_counts(entry: Dict[str, object]) -> Dict[str, int]:
    stages = entry.get("stages", {})  # type: ignore[assignment]
    return {
        "still": int(stages.get("KEEP_HEAD_STILL", {}).get("sequences", 0)),  # type: ignore[union-attr]
        "blink": int(stages.get("BLINK_FIRMLY", {}).get("sequences", 0)),  # type: ignore[union-attr]
        "head": int(stages.get("MOVE_HEAD", {}).get("sequences", 0)),  # type: ignore[union-attr]
    }


# --------------------------------------------------------------------------
# Plan loading
# --------------------------------------------------------------------------

PLAN_SCHEMA = "colibrino-v2-capture-plan/1"


def load_plan(path: str) -> Dict[str, object]:
    with open(path, "r", encoding="utf-8") as handle:
        plan = json.load(handle)
    if plan.get("schema") != PLAN_SCHEMA:
        raise ValueError(f"unexpected plan schema {plan.get('schema')!r}; want {PLAN_SCHEMA}")
    for session in plan.get("sessions", []):
        seen = set()
        for run in session.get("runs", []):
            run_id = run.get("run_id")
            if not run_id or run_id in seen:
                raise ValueError(f"duplicate or missing run_id in plan session {session.get('session_id')}")
            seen.add(run_id)
            kind = run.get("run_kind")
            if run.get("probe", True):
                if kind not in RUN_KINDS:
                    raise ValueError(f"run {run_id}: run_kind {kind!r} not in {RUN_KINDS}")
                if run.get("commit_class") not in COMMIT_CLASSES:
                    raise ValueError(f"run {run_id}: commit_class must be one of {COMMIT_CLASSES}")
            elif kind is not None:
                raise ValueError(f"non-probe entry {run_id} must have run_kind null")
    return plan


def plan_session(plan: Dict[str, object], session_id: str) -> Dict[str, object]:
    for session in plan.get("sessions", []):  # type: ignore[union-attr]
        if session.get("session_id") == session_id:
            return session
    raise KeyError(f"plan has no session {session_id!r}")


def json_dumps_stable(data: object) -> str:
    return json.dumps(data, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
