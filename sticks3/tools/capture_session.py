#!/usr/bin/env python3
"""Monitor-only StickS3 CDC capture for v2 trace collection.

Modes
  --discover            enumerate USB serial descriptors (no port is opened)
                        and print the COLIBRINO_USB_SERIAL line for .env
  (default)             verified live capture into
                        sticks3/.device-backups/logs/capture-YYYYMMDDTHHMMSS/
  --simulate raw.log    replay an existing device log through the same
                        parser/state machine with no serial port
  --tcp [HOST[:PORT]]   cable-free capture over the firmware telemetry mirror
                        (TelemetryMux, port 35533). The host defaults to the
                        ignored .env COLIBRINO_OTA_HOST (else
                        sticks3-ptt.local). Before connecting the target is
                        identity-checked exactly like scripts/upload_ota.sh:
                        ping resolves the IP and its ARP MAC must equal the
                        ignored .env COLIBRINO_OTA_EXPECTED_MAC
                        (case-insensitive). Loopback targets skip ping+ARP
                        (tests) but every connection - loopback included -
                        must open with the firmware greeting line
                        EVENT,TELEMETRY,CONNECTED,<build> or it is refused.
                        The socket is half-closed (SHUT_WR) immediately after
                        connect so this host physically cannot send a byte.

Safety properties
  * The device CDC channel is write-only from the firmware side; this tool
    never writes to the port and never uses 1200 baud. The port is opened at
    115200 baud with DTR and RTS asserted (steady, exactly like
    `pio device monitor`): the firmware's native TinyUSB CDC drops every
    write while DTR is low, so a de-asserted DTR would receive nothing. The
    modem lines are never toggled after open (the ESP32-S3 reset dance needs
    a DTR/RTS sequence, not a steady level).
  * A port is opened only when its USB descriptor matches manufacturer,
    product and the serial number stored as COLIBRINO_USB_SERIAL in the
    ignored repo-root .env. --port only disambiguates verified candidates.
  * Everything is written under the git-ignored .device-backups directory.

Keys during capture (tty only)
  n/p next/previous run     r redo current run (marks previous attempt invalid)
  x invalidate current run  space marker    m typed note    q quit + finalize
"""

from __future__ import annotations

import argparse
import datetime as _dt
import ipaddress
import json
import os
import queue
import select
import shutil
import socket
import subprocess
import sys
import textwrap
import threading
import time
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capture_common as cc  # noqa: E402

try:  # pyserial is optional: --simulate and tests run without it
    import serial  # type: ignore
    from serial.tools import list_ports  # type: ignore

    HAVE_PYSERIAL = True
except Exception:  # pragma: no cover - depends on the host environment
    serial = None  # type: ignore
    list_ports = None  # type: ignore
    HAVE_PYSERIAL = False

PYSERIAL_HINT = (
    "pyserial is not installed for this interpreter. Install it with\n"
    "  python3 -m pip install pyserial\n"
    "or run with /Users/<you>/miniconda3/bin/python3 if that environment has it."
)

AFPLAY = "/usr/bin/afplay"
TINK = "/System/Library/Sounds/Tink.aiff"
SAY = "/usr/bin/say"
# Operator-facing wording for each plan mount; announced on every probe run
# selection so a mount change is never implicit.
MOUNT_TEXT = {
    "bench": "Device stays flat on the table. Do not wear it.",
    "glasses_right_temple": "Put the glasses on your face. Device on the right temple.",
    "glasses_left_temple": "Put the glasses on your face. Device on the left temple.",
    "headband_front": "Wear the headband. Device at the front of your head.",
}
BAUD = 115200
BOOT_WINDOW_MS = 60_000
SESSION_SCHEMA = "colibrino-v2-capture-session/1"
# Display-only stage durations mirroring the firmware constants in
# sticks3/src/main.cpp (kImuPrepareMs, kImuStillCaptureMs, kImuBlinkCaptureMs,
# kImuHeadCaptureMs, kImuFreeRunCaptureMs). Drives the operator countdown; a
# drift against the device is harmless (the device clock is ground truth).
STAGE_SECONDS = {
    "PREPARE_STILL": 3.0,
    "PREPARE_BLINKS": 3.0,
    "PREPARE_HEAD": 3.0,
    "KEEP_HEAD_STILL": 6.0,
    "BLINK_FIRMLY": 15.0,
    "MOVE_HEAD": 12.0,
    "CAPTURE_FREE_RUN": 90.0,
}
# --verbose-status restores the old full-field status line for debugging.
VERBOSE_STATUS = False


def now_ns() -> int:
    return time.monotonic_ns()


# --------------------------------------------------------------------------
# Audio: beeps are fire-and-forget, speech is serialized on a worker thread
# --------------------------------------------------------------------------


class Audio:
    def __init__(self, enabled: bool, sink) -> None:
        self.enabled = enabled and os.path.exists(SAY) and os.path.exists(AFPLAY)
        self._sink = sink
        self._queue: "queue.Queue[Optional[str]]" = queue.Queue()
        self._thread = threading.Thread(target=self._worker, name="speech", daemon=True)
        self._thread.start()

    def _worker(self) -> None:
        while True:
            text = self._queue.get()
            if text is None:
                return
            try:
                subprocess.run([SAY, text], check=False, timeout=60)
            except Exception:
                pass

    def say(self, text: str) -> None:
        # Instructions must be unmissable in the terminal: the status line
        # buries plain prints, so every spoken instruction is also printed as
        # a banner (and the terminal bell rings). Wrapped at 60 columns so a
        # long announcement never soft-wraps mid-word at the terminal edge.
        bar = "=" * 64
        self._sink("\a" + bar)
        for chunk in textwrap.wrap(text.upper(), width=60) or [""]:
            self._sink(">>> " + chunk)
        self._sink(bar)
        if self.enabled:
            self._queue.put(text)

    def beep(self) -> None:
        if self.enabled:
            try:
                subprocess.Popen([AFPLAY, TINK], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                self._sink("\aBEEP")
        else:
            self._sink("\aBEEP")

    def close(self) -> None:
        self._queue.put(None)
        self._thread.join(timeout=2)


# --------------------------------------------------------------------------
# Capture directory writer
# --------------------------------------------------------------------------


class CaptureWriter:
    def __init__(self, directory: str) -> None:
        os.makedirs(directory, exist_ok=True)
        self.directory = directory
        self._raw = open(os.path.join(directory, "raw.log"), "wb", buffering=0)
        self._hosttime = open(os.path.join(directory, "hosttime.tsv"), "w", buffering=1, encoding="utf-8")
        self._hosttime.write("# line_index\thost_monotonic_ns\n")
        self._markers = open(os.path.join(directory, "markers.jsonl"), "a", buffering=1, encoding="utf-8")
        self._lock = threading.Lock()
        self.marker_count = 0

    def write_raw(self, line_index: int, data: bytes, host_ns: int) -> None:
        with self._lock:
            self._raw.write(data)
            if not data.endswith(b"\n"):
                self._raw.write(b"\n")
            self._hosttime.write(f"{line_index}\t{host_ns}\n")

    def marker(self, record: Dict[str, object]) -> None:
        with self._lock:
            self._markers.write(json.dumps(record, sort_keys=True) + "\n")
            self.marker_count += 1

    def write_session(self, data: Dict[str, object]) -> None:
        path = os.path.join(self.directory, "session.json")
        tmp = path + ".tmp"
        with self._lock:
            with open(tmp, "w", encoding="utf-8") as handle:
                handle.write(cc.json_dumps_stable(data))
            os.replace(tmp, path)

    def close(self) -> None:
        with self._lock:
            for handle in (self._raw, self._hosttime, self._markers):
                try:
                    handle.close()
                except Exception:
                    pass


# --------------------------------------------------------------------------
# Cue scheduler
# --------------------------------------------------------------------------


class CueScheduler:
    def __init__(self, audio: Audio, emit, speed: float) -> None:
        self._audio = audio
        self._emit = emit  # callable(record dict)
        self._speed = speed if speed > 0 else None  # None => fire immediately
        self._timers: List[threading.Timer] = []
        self._lock = threading.Lock()

    def cancel(self) -> None:
        with self._lock:
            for timer in self._timers:
                timer.cancel()
            self._timers = []

    def schedule(self, cues: Sequence[Dict[str, object]], context: Dict[str, object], base_host_ns: int) -> None:
        self.cancel()
        with self._lock:
            for cue in cues:
                offset_ms = float(cue.get("t_offset_ms", 0))
                if self._speed is None:
                    # Maximum-speed simulation: fire inline so the marker order
                    # is deterministic and no timer can be cancelled early.
                    self._fire(cue, context, base_host_ns, offset_ms)
                    continue
                delay_s = offset_ms / 1000.0 / self._speed
                planned_ns = base_host_ns + int(delay_s * 1e9)
                timer = threading.Timer(delay_s, self._fire, args=(cue, context, planned_ns, offset_ms))
                timer.daemon = True
                self._timers.append(timer)
                timer.start()

    def _fire(self, cue, context, planned_ns, offset_ms) -> None:
        fired_ns = now_ns()
        sound = cue.get("sound", "beep")
        if sound == "speak":
            self._audio.say(str(cue.get("text", cue.get("tag", ""))))
        else:
            self._audio.beep()
        record = {
            "type": "cue",
            "host_ns": fired_ns,
            "planned_host_ns": planned_ns,
            "slip_ms": round((fired_ns - planned_ns) / 1e6, 3),
            "t_offset_ms": offset_ms,
            "sound": sound,
            "tag": cue.get("tag"),
            "group": cue.get("group"),
            "text": cue.get("text"),
        }
        record.update(context)
        self._emit(record)


# --------------------------------------------------------------------------
# Session state machine (shared by live and --simulate)
# --------------------------------------------------------------------------


class RunRecord:
    def __init__(self, spec: Dict[str, object], order: int) -> None:
        self.spec = spec
        self.order = order
        self.run_id = str(spec["run_id"])
        self.run_kind = spec.get("run_kind")
        self.probe = bool(spec.get("probe", True))
        self.status = "pending"  # pending | valid | invalid | redo | done | skipped
        self.attempts: List[Dict[str, object]] = []
        self.notes: List[Dict[str, object]] = []
        self.cleared = False

    def to_json(self) -> Dict[str, object]:
        return {
            "run_id": self.run_id,
            "run_kind": self.run_kind,
            "probe": self.probe,
            "marker": self.spec.get("marker"),
            "mount": self.spec.get("mount"),
            "commit_class": self.spec.get("commit_class"),
            "firm_blink_count": self.spec.get("firm_blink_count"),
            "status": self.status,
            "cleared": self.cleared,
            "attempts": self.attempts,
            "notes": self.notes,
        }


class SessionState:
    """Parses device lines and tracks runs, stages, alarms and alignment."""

    def __init__(
        self,
        writer: CaptureWriter,
        audio: Audio,
        cues: CueScheduler,
        plan: Optional[Dict[str, object]],
        plan_session_id: Optional[str],
        plan_meta: Optional[Dict[str, object]],
        mode: str,
        printer,
    ) -> None:
        self.writer = writer
        self.audio = audio
        self.cues = cues
        self.plan = plan
        self.plan_session_id = plan_session_id
        self.plan_meta = plan_meta
        self.mode = mode
        self.print = printer
        self.lock = threading.RLock()

        self.line_index = -1
        self.started_ns = now_ns()
        self.identity: Optional[Dict[str, object]] = None
        self.simulated_source: Optional[Dict[str, object]] = None

        # STATUS tracking
        self.latest_status: Optional[Tuple[int, Dict[str, str]]] = None
        self.latest_status_host_ns: Optional[int] = None
        self.status_count = 0
        self.first_status_ms: Optional[int] = None
        self.first_status_host_ns: Optional[int] = None
        self.boot_block: List[Dict[str, object]] = []
        self.boot_seen = False
        self.status_before_first_probe = 0
        self.last_status_ms_before_probe: Optional[int] = None
        self.first_probe_seen = False
        self.calibrated_seen = False
        self.alarm_state: Dict[str, bool] = {}
        self.alarms: List[Dict[str, object]] = []

        # Probe tracking
        self.session_count = 0
        self.stage: Optional[str] = None
        self.stage_host_ns: Optional[int] = None
        self.stage_samples = 0
        self.stage_first_ms: Optional[int] = None
        self.stage_last_ms: Optional[int] = None
        self.probe_open = False
        self.previous_row_stage: Optional[str] = None
        self.last_row_host_ns: Optional[int] = None
        self.recent_rows: List[Tuple[int, int]] = []  # (host_ns, ms) rolling window for Hz

        # Runs
        self.runs: List[RunRecord] = []
        self.run_pointer = 0
        self.extra_runs = 0
        if plan is not None and plan_session_id is not None:
            for order, spec in enumerate(cc.plan_session(plan, plan_session_id).get("runs", [])):
                self.runs.append(RunRecord(spec, order))
        self.current_attempt: Optional[Dict[str, object]] = None
        self.current_attempt_run: Optional[RunRecord] = None
        self.alignment_pairs: List[Tuple[int, int]] = []
        self.alignment: List[Dict[str, object]] = []
        self.notes: List[Dict[str, object]] = []
        self.fatigue: Optional[int] = None
        self.finalized = False
        self.disconnects = 0

        # Wireless telemetry bookkeeping (STATUS build= tail, TCP greeting,
        # EVENT,TELEMETRY,DROPPED counter). Present in every mode so old
        # logs simply leave them at their defaults.
        self.status_build: Optional[str] = None
        self.telemetry_build: Optional[str] = None
        self.telemetry_greetings = 0
        self.telemetry_dropped_total: Optional[int] = None

    # ---- helpers ---------------------------------------------------------

    def current_run(self) -> Optional[RunRecord]:
        if 0 <= self.run_pointer < len(self.runs):
            return self.runs[self.run_pointer]
        return None

    def marker(self, record: Dict[str, object]) -> None:
        record.setdefault("host_ns", now_ns())
        record.setdefault("line_index", self.line_index)
        run = self.current_run()
        record.setdefault("run_id", run.run_id if run else None)
        self.writer.marker(record)

    def _emit_cue(self, record: Dict[str, object]) -> None:
        with self.lock:
            record.setdefault("line_index", self.line_index)
            self.writer.marker(record)
        self.print(f"CUE {record.get('tag')} ({record.get('sound')}) slip={record.get('slip_ms')}ms")

    def _banner(self, text: str) -> None:
        # Unmissable print-only block; used where speech would lag the stage.
        bar = "=" * 64
        self.print(bar)
        for chunk in textwrap.wrap(text, width=60) or [""]:
            self.print(">>> " + chunk)
        self.print(bar)

    def alarm(self, key: str, text: str) -> None:
        if self.alarm_state.get(key):
            return
        self.alarm_state[key] = True
        record = {"type": "alarm", "key": key, "text": text, "host_ns": now_ns()}
        self.alarms.append(record)
        self.marker(dict(record))
        self.print(f"ALARM: {text}")
        self.audio.say("Alarm. " + text)

    def clear_alarm(self, key: str) -> None:
        if self.alarm_state.get(key):
            self.alarm_state[key] = False
            self.marker({"type": "alarm_cleared", "key": key})

    def select_run(self, delta: int, key: str) -> None:
        with self.lock:
            if not self.runs:
                self.print("no plan loaded; runs are created automatically")
                return
            new_pointer = max(0, min(len(self.runs) - 1, self.run_pointer + delta))
            previous = self.current_run()
            if previous is not None and not previous.probe and previous.status == "pending" and delta > 0:
                previous.status = "done"
                self.marker({"type": "marker_done", "run_id": previous.run_id, "marker": previous.spec.get("marker")})
            self.run_pointer = new_pointer
            run = self.current_run()
            assert run is not None
            self.marker({"type": "keypress", "key": key, "run_id": run.run_id})
            self.marker({"type": "run_selected", "run_id": run.run_id, "run_kind": run.run_kind})
            self.print(f"selected run {run.run_id} ({run.run_kind or run.spec.get('marker')})")
            if not run.probe:
                text = str(run.spec.get("instructions", {}).get("operator", ""))
                if text:
                    self.audio.say(text)
            else:
                self.announce_probe_run(run)

    def announce_probe_run(self, run) -> None:
        # One complete, self-sufficient instruction per probe run: mount,
        # blink-stage preview, and exactly how to start. A mount change must
        # never be implicit.
        parts = [f"Next run {run.run_id}."]
        mount_text = MOUNT_TEXT.get(run.spec.get("mount"))
        if mount_text:
            parts.append(mount_text)
        blink_hint = str(run.spec.get("instructions", {}).get("BLINK_FIRMLY", ""))
        if blink_hint:
            parts.append(f"In the blink stage: {blink_hint}")
        parts.append("When ready, tap the blue button once to start. "
                     "If the screen says IR PROBE, tap once for MOTION first.")
        self.audio.say(" ".join(parts))

    def redo_current(self) -> None:
        with self.lock:
            run = self.current_run()
            if run is None:
                return
            for attempt in run.attempts:
                if attempt.get("status") == "valid":
                    attempt["status"] = "invalid"
                    attempt["invalidated_reason"] = "redo"
            run.status = "redo" if run.attempts else run.status
            self.marker({"type": "keypress", "key": "r", "run_id": run.run_id})
            self.marker({"type": "redo", "run_id": run.run_id, "attempts_invalidated": len(run.attempts)})
            self.print(f"run {run.run_id}: previous attempts marked invalid; next probe is a redo")
            self.audio.say(f"Redo {run.run_id}.")

    def invalidate_current(self) -> None:
        with self.lock:
            run = self.current_run()
            if run is None:
                return
            for attempt in run.attempts:
                attempt["status"] = "invalid"
                attempt.setdefault("invalidated_reason", "operator")
            if self.current_attempt is not None and self.current_attempt_run is run:
                self.current_attempt["status"] = "invalid"
                self.current_attempt["invalidated_reason"] = "operator"
            run.status = "invalid"
            self.marker({"type": "keypress", "key": "x", "run_id": run.run_id})
            self.marker({"type": "invalidate", "run_id": run.run_id})
            self.print(f"run {run.run_id} marked invalid")
            self.audio.say(f"{run.run_id} invalid.")

    def add_marker_key(self) -> None:
        with self.lock:
            self.marker({"type": "keypress", "key": "space"})
            self.print("marker")

    def add_note(self, text: str) -> None:
        with self.lock:
            record = {"type": "note", "text": text, "host_ns": now_ns()}
            run = self.current_run()
            if run is not None:
                run.notes.append({"host_ns": record["host_ns"], "text": text})
            self.notes.append({"host_ns": record["host_ns"], "run_id": run.run_id if run else None, "text": text})
            self.marker(dict(record))

    def _next_probe_run(self, start: int) -> Optional[int]:
        for idx in range(start, len(self.runs)):
            run = self.runs[idx]
            if run.probe and run.status in ("pending", "redo"):
                return idx
        return None

    def _run_for_new_probe(self) -> RunRecord:
        run = self.current_run()
        if run is not None and run.probe and run.status in ("pending", "redo"):
            return run
        if run is not None and not run.probe and run.status == "pending":
            run.status = "done"
            self.marker({"type": "marker_done", "run_id": run.run_id, "marker": run.spec.get("marker"), "auto": True})
            self.marker({"type": "warning", "text": f"probe started while marker {run.run_id} was selected; auto-advanced"})
        idx = self._next_probe_run(self.run_pointer)
        if idx is not None:
            self.run_pointer = idx
            self.marker({"type": "run_selected", "run_id": self.runs[idx].run_id, "run_kind": self.runs[idx].run_kind, "auto": True})
            return self.runs[idx]
        self.extra_runs += 1
        prefix = "SIM" if (self.mode == "simulate" and self.plan is None) else "EXTRA"
        spec = {"run_id": f"{prefix}-{self.extra_runs}", "run_kind": None, "probe": True, "commit_class": None}
        record = RunRecord(spec, len(self.runs))
        self.runs.append(record)
        self.run_pointer = len(self.runs) - 1
        self.marker({"type": "run_selected", "run_id": record.run_id, "run_kind": None, "auto": True})
        return record

    # ---- feed --------------------------------------------------------------

    def feed(self, data: bytes, host_ns: int) -> None:
        with self.lock:
            self.line_index += 1
            self.writer.write_raw(self.line_index, data, host_ns)
            line = data.decode("utf-8", errors="replace").rstrip("\r\n")
            kind = cc.classify_line(line)
            if kind == "IMU":
                self._on_imu(line, host_ns)
            elif kind == "STATUS":
                self._on_status(line, host_ns)
            elif kind == "EVENT":
                self._on_event(line, host_ns)
            elif kind == "RESULT":
                self._on_result(line, host_ns)

    def _on_status(self, line: str, host_ns: int) -> None:
        parsed = cc.parse_status(line)
        if parsed is None:
            return
        ms, fields = parsed
        self.status_count += 1
        self.latest_status = parsed
        self.latest_status_host_ns = host_ns
        if self.first_status_ms is None:
            self.first_status_ms = ms
            self.first_status_host_ns = host_ns
            self.boot_seen = ms < BOOT_WINDOW_MS
        if not self.first_probe_seen:
            self.status_before_first_probe += 1
            self.last_status_ms_before_probe = ms
            if self.boot_seen and ms < BOOT_WINDOW_MS and len(self.boot_block) < 120:
                self.boot_block.append({"ms": ms, "fields": fields})
        if fields.get("build"):
            self.status_build = fields["build"]
        if fields.get("calibrated") == "1":
            self.calibrated_seen = True
        self._check_boot_marker(ms, fields)
        # Alarms
        if fields.get("armed") == "1":
            self.alarm("armed", "device reports armed equals one")
        else:
            self.clear_alarm("armed")
        if fields.get("mode") == "MOUSE":
            self.alarm("mode", "device is in mouse mode")
        else:
            self.clear_alarm("mode")
        if fields.get("imu") == "0":
            self.alarm("imu", "IMU reports zero")
        else:
            self.clear_alarm("imu")
        if self.calibrated_seen and fields.get("calibrated") == "0":
            self.alarm("calibrated", "calibration lost mid session")
        else:
            self.clear_alarm("calibrated")

    def _check_boot_marker(self, ms: int, fields: Dict[str, str]) -> None:
        """Complete a BOOT plan marker once enough STATUS has been observed."""
        run = self.current_run()
        if run is None or run.probe or run.spec.get("marker") != "BOOT" or run.status != "pending":
            return
        if self.first_probe_seen or self.first_status_ms is None:
            return
        needed = float(run.spec.get("min_status_seconds", 60))
        expected = run.spec.get("expected_status", {}) or {}
        observed_s = (ms - self.first_status_ms) / 1000.0
        matches = all(fields.get(k) == v for k, v in expected.items())
        if not self.boot_seen:
            if not self.alarm_state.get("boot_not_from_plugin"):
                self.alarm_state["boot_not_from_plugin"] = True
                self.marker({"type": "warning", "text": f"first STATUS arrived at device uptime {self.first_status_ms / 1000.0:.0f} s; not a from-plug-in BOOT block (press n to skip BOOT)"})
                self.print(f"BOOT: device uptime already {self.first_status_ms / 1000.0:.0f} s at first STATUS; this is not a from-plug-in boot block (press n to skip)")
            return
        if observed_s >= needed and matches:
            run.status = "done"
            run.attempts.append({"status_seconds": observed_s, "last_status_ms": ms, "expected_status_matched": True})
            self.marker({"type": "boot_complete", "run_id": run.run_id, "status_seconds": observed_s})
            self.print(f"BOOT block complete: {observed_s:.0f} s of STATUS with {' '.join(f'{k}={v}' for k, v in expected.items())}")
            self.audio.say("Boot block complete.")

    def _on_event(self, line: str, host_ns: int) -> None:
        parsed = cc.parse_event(line)
        if parsed is None:
            return
        name, args = parsed
        if name == "IMU_PROBE_STARTED":
            self.first_probe_seen = True
            self.session_count += 1
            self.cues.cancel()
            run = self._run_for_new_probe()
            attempt = {
                "attempt": len(run.attempts) + 1,
                "session_index": self.session_count,
                "status": "valid",
                "start_line": self.line_index,
                "end_line": None,
                "host_ns_started": host_ns,
                "result": None,
                "complete_samples": None,
                "stages": {},
            }
            run.attempts.append(attempt)
            self.current_attempt = attempt
            self.current_attempt_run = run
            self.alignment_pairs = []
            self.probe_open = True
            self.stage = None
            self.previous_row_stage = None
            self.marker({"type": "probe_started", "run_id": run.run_id, "run_kind": run.run_kind, "attempt": attempt["attempt"], "session_index": self.session_count, "host_ns": host_ns})
            self.print(f"PROBE {self.session_count} started -> run {run.run_id} attempt {attempt['attempt']}")
            self.audio.say(f"Run {run.run_id}.")
            return
        if name == "IMU_PROBE_STAGE" and args:
            stage = args[0]
            self.stage = stage
            self.stage_host_ns = host_ns
            self.stage_samples = 0
            self.stage_first_ms = None
            self.stage_last_ms = None
            run = self.current_attempt_run
            self.marker({"type": "stage", "stage": stage, "run_id": run.run_id if run else None, "session_index": self.session_count, "host_ns": host_ns})
            self.cues.cancel()
            spec = run.spec if run else {}
            instructions = spec.get("instructions", {}) if isinstance(spec, dict) else {}
            if stage in cc.PREPARE_FOR_STAGE:
                text = instructions.get(cc.PREPARE_FOR_STAGE[stage])
                if text:
                    self.marker({"type": "instruction", "stage": cc.PREPARE_FOR_STAGE[stage], "text": text})
                    self.audio.say(f"GET READY: {text} Starts in 3 seconds.")
            elif stage in cc.CAPTURE_STAGES or stage == cc.FREE_RUN_STAGE:
                # The GET READY banner fired during the prepare stage; this one
                # marks the exact moment to act, synchronized with the stage.
                if stage == cc.FREE_RUN_STAGE:
                    self._banner("NOW: FREE RUN RECORDING. TAP ONCE TO STOP.")
                else:
                    text = instructions.get(stage)
                    if text:
                        seconds = STAGE_SECONDS.get(stage)
                        header = f"NOW ({seconds:.0f} SECONDS): " if seconds else "NOW: "
                        self._banner(header + str(text).upper())
                cue_list = spec.get("cues", {}).get(stage, []) if isinstance(spec, dict) else []
                if cue_list:
                    context = {"run_id": run.run_id if run else None, "stage": stage, "session_index": self.session_count, "stage_host_ns": host_ns}
                    self.cues.schedule(cue_list, context, host_ns)
            elif stage == "CAPTURE_COMPLETE":
                self._banner("RUN DONE. HANDS OFF. NEXT INSTRUCTION COMING.")
            return
        if name == "IMU_PROBE_COMPLETE":
            samples = None
            for arg in args:
                if arg.startswith("samples="):
                    try:
                        samples = int(arg.split("=", 1)[1])
                    except ValueError:
                        samples = None
            if self.current_attempt is not None:
                self.current_attempt["complete_samples"] = samples
                self.current_attempt["end_line"] = self.line_index
            self._close_probe(host_ns)
            return
        if name in ("IMU_BLINK_SEQUENCE_CANDIDATE", "IMU_DOUBLE_BLINK_CANDIDATE"):
            self.marker({"type": "candidate", "event": name, "stage": args[0] if args else None, "count": args[1] if len(args) > 1 else None, "session_index": self.session_count, "host_ns": host_ns})
            return
        if name == "TELEMETRY" and args:
            if args[0] == "CONNECTED":
                self.telemetry_greetings += 1
                self.telemetry_build = args[1] if len(args) > 1 else None
                self.marker({"type": "telemetry_connected", "build": self.telemetry_build, "host_ns": host_ns})
            elif args[0] == "DROPPED" and len(args) > 1:
                try:
                    total = int(args[1])
                except ValueError:
                    return
                previous = self.telemetry_dropped_total
                self.telemetry_dropped_total = total
                if previous is None or total > previous:
                    # Every increase must be announced: re-arm the key so
                    # alarm() does not dedupe consecutive rises.
                    self.alarm_state["telemetry_dropped"] = False
                    self.alarm("telemetry_dropped", f"telemetry mirror dropped lines, total {total}")
            return

    def _on_result(self, line: str, host_ns: int) -> None:
        result = cc.parse_result(line)
        if result is None:
            return
        run = self.current_attempt_run
        if self.current_attempt is not None:
            self.current_attempt["result"] = result
            self.current_attempt["end_line"] = self.line_index
        self.marker({"type": "result", "run_id": run.run_id if run else None, "session_index": self.session_count, "result": result, "host_ns": host_ns})
        counts = f"still={result['still']} blink={result['blink']} head={result['head']}"
        self.print(f"RESULT {run.run_id if run else '?'}: {result['verdict']} {counts}")
        # Humane announcement: for a control run the firmware's NOT_PROVEN with
        # zero counts is exactly correct and must not read as failure. The
        # RESULT log line above stays verbatim.
        run_id = run.run_id if run else "run"
        expected_blinks = run.spec.get("firm_blink_count") if run is not None and isinstance(run.spec, dict) else None
        clean = result["still"] == 0 and result["blink"] == 0 and result["head"] == 0
        if result["verdict"] == "PASS":
            self.audio.say(f"Result pass. {counts}.")
        elif expected_blinks == 0 and clean:
            self.audio.say(f"Run {run_id} clean. Exactly as expected.")
        elif expected_blinks == 0:
            self.audio.say(f"Check run {run_id}: unexpected events. {counts}.")
        else:
            self.audio.say(f"Run {run_id} recorded. Blink patterns seen: {result['blink']}. Not proven is normal during capture.")
        self._finish_run_after_result()

    def _close_probe(self, host_ns: int) -> None:
        if not self.probe_open:
            return
        self.probe_open = False
        self.cues.cancel()
        run = self.current_attempt_run
        attempt = self.current_attempt
        if run is not None and attempt is not None:
            estimate = cc.estimate_alignment(self.alignment_pairs)
            if estimate is not None:
                estimate = dict(estimate)
                estimate.update({"run_id": run.run_id, "attempt": attempt["attempt"], "session_index": attempt["session_index"], "simulated": self.mode == "simulate"})
                self.alignment.append(estimate)
                attempt["alignment"] = estimate
            if run.status in ("pending", "redo") and attempt.get("status") == "valid":
                run.status = "valid"
            self.marker({"type": "probe_complete", "run_id": run.run_id, "attempt": attempt["attempt"], "session_index": attempt["session_index"], "host_ns": host_ns, "samples": attempt.get("complete_samples")})

    def _finish_run_after_result(self) -> None:
        run = self.current_attempt_run
        if run is None:
            return
        # Auto-advance to the next plan entry so a following probe lands on it.
        if self.runs and self.run_pointer + 1 < len(self.runs) and self.current_run() is run:
            self.run_pointer += 1
            nxt = self.current_run()
            if nxt is not None:
                self.marker({"type": "run_selected", "run_id": nxt.run_id, "run_kind": nxt.run_kind, "auto": True})
                if not nxt.probe:
                    text = str(nxt.spec.get("instructions", {}).get("operator", ""))
                    self.print(f"next: {nxt.run_id} ({nxt.spec.get('marker')}) - {text}")
                    if text:
                        self.audio.say(text)
                else:
                    self.print(f"next run: {nxt.run_id} ({nxt.run_kind}) - press n/p to change")
                    self.announce_probe_run(nxt)

    def _on_imu(self, line: str, host_ns: int) -> None:
        row = cc.parse_imu(line, self.line_index)
        if row is None:
            return
        # Fallback session boundary shared with replay_imu_capture.cpp.
        still_after_head = self.previous_row_stage == "MOVE_HEAD" and row.stage == "KEEP_HEAD_STILL"
        if not self.probe_open or still_after_head:
            self.marker({"type": "warning", "text": "IMU rows without EVENT,IMU_PROBE_STARTED; opening implicit session"})
            self._on_event("EVENT,IMU_PROBE_STARTED", host_ns)
        if row.stage in cc.CAPTURE_STAGES:
            if self.stage != row.stage:
                # Stage line missed (or log without EVENT lines): synthesize.
                self.stage = row.stage
                self.stage_host_ns = host_ns
                self.stage_samples = 0
                self.stage_first_ms = None
                self.stage_last_ms = None
                self.marker({"type": "stage", "stage": row.stage, "session_index": self.session_count, "host_ns": host_ns, "implicit": True})
            self.previous_row_stage = row.stage
            self.stage_samples += 1
            if self.stage_first_ms is None:
                self.stage_first_ms = row.ms
            self.stage_last_ms = row.ms
            self.alignment_pairs.append((host_ns, row.ms))
            if self.current_attempt is not None:
                stage_stats = self.current_attempt["stages"].setdefault(row.stage, {"samples": 0, "first_ms": row.ms, "last_ms": row.ms})  # type: ignore[index]
                stage_stats["samples"] += 1
                stage_stats["last_ms"] = row.ms
        self.last_row_host_ns = host_ns
        self.recent_rows.append((host_ns, row.ms))
        if len(self.recent_rows) > 400:
            del self.recent_rows[: len(self.recent_rows) - 400]

    # ---- reporting -----------------------------------------------------

    def effective_hz(self) -> Optional[float]:
        if len(self.recent_rows) < 2:
            return None
        span_ms = self.recent_rows[-1][1] - self.recent_rows[0][1]
        if span_ms <= 0:
            return None
        return (len(self.recent_rows) - 1) * 1000.0 / span_ms

    def status_line(self) -> str:
        if VERBOSE_STATUS:
            return self._status_line_verbose()
        # Operator-first status: always states what to do RIGHT NOW, from the
        # same state that drives the banners, with a countdown. Between runs it
        # names the next action instead of showing the finished stage.
        stage = self.stage or "IDLE"
        elapsed = (now_ns() - self.stage_host_ns) / 1e9 if self.stage_host_ns else 0.0
        in_stage = self.probe_open and (
            stage in cc.PREPARE_FOR_STAGE or stage in cc.CAPTURE_STAGES or stage == cc.FREE_RUN_STAGE
        )
        run = (self.current_attempt_run if in_stage else None) or self.current_run()
        run_text = run.run_id if run else "-"
        spec = run.spec if run is not None and isinstance(run.spec, dict) else {}
        instructions = spec.get("instructions", {}) if isinstance(spec, dict) else {}

        tail_parts = []
        hz = self.effective_hz()
        if hz:
            tail_parts.append(f"{hz:.0f} Hz")
        if self.latest_status is not None:
            fields = self.latest_status[1]
            batt = fields.get("batt")
            if batt is not None:
                tail_parts.append(f"batt {batt}")
            if fields.get("armed") == "1":
                tail_parts.append("ALARM ARMED")
            if fields.get("mode") == "MOUSE":
                tail_parts.append("ALARM MODE=MOUSE")
        tail = " | ".join(tail_parts)

        if in_stage and stage in cc.PREPARE_FOR_STAGE:
            left = max(0.0, STAGE_SECONDS.get(stage, 3.0) - elapsed)
            body = f"GET READY {left:.0f}s: {cc.PREPARE_FOR_STAGE[stage].replace('_', ' ').lower()} next"
        elif in_stage and stage == cc.FREE_RUN_STAGE:
            body = f"NOW {elapsed:.0f}s: free run recording, tap once to stop"
        elif in_stage:
            text = str(instructions.get(stage) or stage.replace("_", " ").lower())
            left = max(0.0, STAGE_SECONDS.get(stage, 0.0) - elapsed)
            body = f"NOW {left:.0f}s: {text}"
        elif run is not None and run.probe and run.status in ("pending", "redo"):
            mount = str(spec.get("mount") or "")
            prefix = "glasses on, " if mount.startswith("glasses") else ("device on table, " if mount == "bench" else "")
            body = f"NEXT: {prefix}tap button once to start"
        else:
            body = "WAITING: follow the latest banner"
        line = f"{run_text} | {body}"
        return f"{line} | {tail}" if tail else line

    def _status_line_verbose(self) -> str:
        run = self.current_run()
        run_text = run.run_id if run else "-"
        stage = self.stage or "IDLE"
        elapsed = (now_ns() - self.stage_host_ns) / 1e9 if self.stage_host_ns else 0.0
        hz = self.effective_hz()
        hz_text = f"{hz:5.1f}" if hz else "  -  "
        flags = "-"
        if self.latest_status is not None:
            fields = self.latest_status[1]
            flags = " ".join(
                f"{k}={fields.get(k, '?')}"
                for k in ("mode", "armed", "imu", "calibrated", "imu_blink", "ir", "ota", "imu_stage")
                if k in fields
            )
        return f"run={run_text} stage={stage} t={elapsed:5.1f}s samples={self.stage_samples} hz={hz_text} | {flags}"

    def boot_summary(self) -> Dict[str, object]:
        last = self.boot_block[-1] if self.boot_block else None
        boot_ok = None
        if last is not None:
            fields = last["fields"]  # type: ignore[index]
            boot_ok = (
                fields.get("armed") == "0"
                and fields.get("ir") == "0"
                and fields.get("calibrated") == "1"
                and fields.get("imu_blink") == "0"
                and fields.get("ota") == "READY"
            )
        seconds = None
        if self.first_status_ms is not None and self.last_status_ms_before_probe is not None:
            seconds = (self.last_status_ms_before_probe - self.first_status_ms) / 1000.0
        return {
            "seen": self.boot_seen,
            "first_status_ms": self.first_status_ms,
            "status_lines_before_first_probe": self.status_before_first_probe,
            "boot_block_status_lines": len(self.boot_block),
            "boot_block": self.boot_block[:5] + ([self.boot_block[-1]] if len(self.boot_block) > 5 else []),
            "last_boot_status": last,
            "boot_ok": boot_ok,
            "boot_ok_rule": "armed=0 ir=0 calibrated=1 imu_blink=0 ota=READY",
            "status_seconds_observed_before_first_probe": seconds,
        }

    def to_json(self) -> Dict[str, object]:
        return {
            "schema": SESSION_SCHEMA,
            "capture_dir": os.path.basename(self.writer.directory),
            "mode": self.mode,
            "colibrino_head": cc.git_head(),
            "plan": self.plan_meta,
            "identity": self.identity,
            "simulated_source": self.simulated_source,
            "boot": self.boot_summary(),
            "runs": [run.to_json() for run in self.runs],
            "notes": self.notes,
            "alarms": self.alarms,
            "alignment": self.alignment,
            "alignment_note": "offset_ns is an ESTIMATED lower-envelope alignment (host_monotonic_ns ~= device_ms*1e6 + offset_ns); residual_ms describes host delay spread. Not a measured clock sync.",
            "fatigue": self.fatigue,
            "status_build": self.status_build,
            "telemetry": {
                "greetings": self.telemetry_greetings,
                "connected_build": self.telemetry_build,
                "dropped_total": self.telemetry_dropped_total,
            },
            "line_count": self.line_index + 1,
            "status_count": self.status_count,
            "probe_sessions": self.session_count,
            "markers_count": self.writer.marker_count,
            "disconnects": self.disconnects,
            "finalized": self.finalized,
        }


# --------------------------------------------------------------------------
# Serial identity guard
# --------------------------------------------------------------------------


def describe_port(info) -> Dict[str, object]:
    return {
        "device": getattr(info, "device", None),
        "vid": f"0x{info.vid:04x}" if getattr(info, "vid", None) is not None else None,
        "pid": f"0x{info.pid:04x}" if getattr(info, "pid", None) is not None else None,
        "manufacturer": getattr(info, "manufacturer", None),
        "product": getattr(info, "product", None),
        "serial_number": getattr(info, "serial_number", None),
    }


def find_verified_ports(expected_serial: str) -> List[Dict[str, object]]:
    verified = []
    for info in list_ports.comports():  # type: ignore[union-attr]
        desc = describe_port(info)
        if (
            (desc["manufacturer"] or "") == cc.USB_MANUFACTURER
            and (desc["product"] or "") == cc.USB_PRODUCT
            and str(desc["serial_number"] or "").lower() == expected_serial.lower()
        ):
            verified.append(desc)
    return verified


def redact_identity(desc: Dict[str, object]) -> Dict[str, object]:
    serial_number = str(desc.get("serial_number") or "")
    return {
        "vid": desc.get("vid"),
        "pid": desc.get("pid"),
        "manufacturer": desc.get("manufacturer"),
        "product": desc.get("product"),
        "serial_last4": serial_number[-4:] if serial_number else None,
        "serial_length": len(serial_number),
        "port_basename": os.path.basename(str(desc.get("device") or "")),
    }


def open_verified_port(desc: Dict[str, object]):
    """Open the verified port read-only in practice: 115200 baud, DTR and RTS
    asserted once at open (the firmware's TinyUSB CDC only transmits while the
    host holds DTR; `pio device monitor` produced the retained logs with the
    same pyserial defaults), never toggled afterwards, never 1200 baud, and
    nothing is ever written."""
    ser = serial.Serial()  # type: ignore[union-attr]
    ser.port = desc["device"]
    ser.baudrate = BAUD
    ser.dtr = True
    ser.rts = True
    ser.timeout = 0.2
    ser.open()
    return ser


def discover() -> int:
    if not HAVE_PYSERIAL:
        print(PYSERIAL_HINT, file=sys.stderr)
        return 2
    ports = list(list_ports.comports())  # type: ignore[union-attr]
    if not ports:
        print("no serial ports enumerated")
        return 1
    matches = []
    for info in ports:
        desc = describe_port(info)
        print(
            f"{desc['device']}: vid={desc['vid']} pid={desc['pid']} manufacturer={desc['manufacturer']!r} "
            f"product={desc['product']!r} serial_number={desc['serial_number']!r}"
        )
        if (desc["manufacturer"] or "") == cc.USB_MANUFACTURER and (desc["product"] or "") == cc.USB_PRODUCT:
            matches.append(desc)
    print("(descriptor read only; no port was opened)")
    if not matches:
        print(f"no port reports manufacturer {cc.USB_MANUFACTURER!r} and product {cc.USB_PRODUCT!r}")
        return 1
    for desc in matches:
        print(f"add this line to the ignored repo-root .env ({os.path.join(cc.repo_root(), '.env')}):")
        print(f"{cc.ENV_SERIAL_KEY}={desc['serial_number']}")
    return 0


# --------------------------------------------------------------------------
# Terminal keys
# --------------------------------------------------------------------------


class Keys:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled and sys.stdin.isatty()
        self._fd = sys.stdin.fileno() if self.enabled else None
        self._saved = None

    def __enter__(self):
        if self.enabled:
            import termios
            import tty

            self._saved = termios.tcgetattr(self._fd)
            tty.setcbreak(self._fd)
        return self

    def __exit__(self, *exc):
        self.restore()

    def restore(self) -> None:
        if self.enabled and self._saved is not None:
            import termios

            termios.tcsetattr(self._fd, termios.TCSADRAIN, self._saved)

    def poll(self) -> Optional[str]:
        if not self.enabled:
            return None
        ready, _, _ = select.select([sys.stdin], [], [], 0)
        if not ready:
            return None
        return sys.stdin.read(1)

    def read_line(self, prompt: str) -> str:
        if not self.enabled:
            return ""
        import termios
        import tty

        termios.tcsetattr(self._fd, termios.TCSADRAIN, self._saved)
        try:
            sys.stdout.write("\n" + prompt)
            sys.stdout.flush()
            return sys.stdin.readline().strip()
        finally:
            tty.setcbreak(self._fd)


# --------------------------------------------------------------------------
# Drivers
# --------------------------------------------------------------------------


def make_printer(pane: bool):
    def printer(text: str) -> None:
        if pane:
            sys.stdout.write("\r\033[K" + text + "\n")
        else:
            sys.stdout.write(text + "\n")
        sys.stdout.flush()

    return printer


def handle_key(key: str, state: SessionState, keys: Keys) -> bool:
    """Returns True when the operator asked to quit."""
    if key == "n":
        state.select_run(+1, "n")
    elif key == "p":
        state.select_run(-1, "p")
    elif key == "r":
        state.redo_current()
    elif key == "x":
        state.invalidate_current()
    elif key == " ":
        state.add_marker_key()
    elif key == "m":
        text = keys.read_line("note> ")
        if text:
            state.add_note(text)
    elif key == "q":
        return True
    return False


def finalize(state: SessionState, keys: Keys, writer: CaptureWriter) -> None:
    if keys.enabled and state.fatigue is None:
        answer = keys.read_line("fatigue 1-5 (enter to skip)> ")
        if answer.isdigit() and 1 <= int(answer) <= 5:
            state.fatigue = int(answer)
    state.cues.cancel()
    state.finalized = True
    state.marker({"type": "quit"})
    writer.write_session(state.to_json())


def run_simulate(args, plan, plan_meta) -> int:
    source = args.simulate
    if not os.path.isfile(source):
        print(f"simulate source not found: {source}", file=sys.stderr)
        return 2
    stamp = _dt.datetime.now().strftime("%Y%m%dT%H%M%S")
    out_root = args.out_dir or os.path.join(cc.sticks3_dir(), ".device-backups", "logs")
    directory = os.path.join(out_root, f"capture-sim-{stamp}")
    suffix = 1
    while os.path.exists(directory):
        suffix += 1
        directory = os.path.join(out_root, f"capture-sim-{stamp}-{suffix}")
    writer = CaptureWriter(directory)
    pane = sys.stdout.isatty() and not args.quiet
    printer = make_printer(pane)
    audio = Audio(enabled=not args.no_audio, sink=printer)
    speed = float(args.sim_speed)
    cues = CueScheduler(audio, lambda rec: state._emit_cue(rec), speed)
    state = SessionState(writer, audio, cues, plan, args.plan_session if plan else None, plan_meta, "simulate", printer)
    state.simulated_source = {"basename": os.path.basename(source), "sha256": cc.sha256_file(source)}
    printer(f"simulate: {os.path.basename(source)} -> {directory} (speed x{speed if speed > 0 else 'max'})")
    keys = Keys(enabled=not args.no_keys)
    quit_requested = False
    last_pane = 0.0
    last_flush = time.monotonic()
    previous_ms: Optional[int] = None
    sim_wall_start = time.monotonic()
    sim_device_elapsed_ms = 0.0
    with keys:
        with open(source, "rb") as handle:
            for raw in handle:
                # Pace on device timestamps when present (gaps capped at 5 s of
                # device time so idle minutes between probes do not stall).
                if speed > 0:
                    text = raw.decode("utf-8", errors="replace")
                    ms = None
                    for prefix in ("IMU,", "STATUS,"):
                        if text.startswith(prefix):
                            try:
                                ms = int(text.split(",")[1])
                            except (IndexError, ValueError):
                                ms = None
                            break
                    if ms is not None:
                        if previous_ms is not None and ms > previous_ms:
                            sim_device_elapsed_ms += min(ms - previous_ms, 5000)
                        previous_ms = ms
                        target = sim_wall_start + sim_device_elapsed_ms / 1000.0 / speed
                        ahead = target - time.monotonic()
                        if ahead > 0.001:
                            time.sleep(ahead)
                host_ns = now_ns()
                state.feed(raw, host_ns)
                if state.line_index % 25 == 0:
                    key = keys.poll()
                    if key is not None and handle_key(key, state, keys):
                        quit_requested = True
                        break
                now = time.monotonic()
                if pane and now - last_pane > 0.25:
                    sys.stdout.write("\r\033[K" + state.status_line()[: max(20, shutil.get_terminal_size((120, 24)).columns - 1)])
                    sys.stdout.flush()
                    last_pane = now
                if now - last_flush > 5:
                    writer.write_session(state.to_json())
                    last_flush = now
        if pane:
            sys.stdout.write("\n")
        if not quit_requested:
            # Let outstanding cue timers land before finalizing.
            time.sleep(0.05 if speed > 0 else 0.0)
        finalize(state, keys, writer)
    audio.close()
    writer.close()
    printer(f"simulate complete: {state.session_count} probe sessions, {state.line_index + 1} lines -> {directory}")
    return 0


def run_live(args, plan, plan_meta) -> int:
    if not HAVE_PYSERIAL:
        print(PYSERIAL_HINT, file=sys.stderr)
        return 2
    env_path = args.env or os.path.join(cc.repo_root(), ".env")
    env = cc.parse_env_file(env_path)
    expected = env.get(cc.ENV_SERIAL_KEY, "").strip()
    if not expected:
        print(
            f"{cc.ENV_SERIAL_KEY} is not set in {env_path}. Run --discover and add the printed line to that ignored file.",
            file=sys.stderr,
        )
        return 2

    def locate() -> Optional[Dict[str, object]]:
        candidates = find_verified_ports(expected)
        if not candidates:
            return None
        if len(candidates) == 1:
            if args.port and os.path.realpath(args.port) != os.path.realpath(str(candidates[0]["device"])):
                print(f"--port {args.port} is not the verified device; ignoring the flag", file=sys.stderr)
            return candidates[0]
        if args.port:
            for cand in candidates:
                if os.path.realpath(str(cand["device"])) == os.path.realpath(args.port):
                    return cand
            print(f"--port {args.port} is not among the {len(candidates)} verified candidates", file=sys.stderr)
            return None
        print(f"{len(candidates)} verified candidates; pass --port to choose: " + ", ".join(str(c['device']) for c in candidates), file=sys.stderr)
        return None

    desc = locate()
    if desc is None:
        if not args.wait_for_port:
            print("no verified Colibrino StickS3 port (manufacturer/product/serial must all match); refusing to open anything", file=sys.stderr)
            return 3
        print("waiting for the verified device to enumerate (Ctrl-C to abort)...")
        while desc is None:
            time.sleep(1.0)
            desc = locate()

    stamp = _dt.datetime.now().strftime("%Y%m%dT%H%M%S")
    out_root = args.out_dir or os.path.join(cc.sticks3_dir(), ".device-backups", "logs")
    directory = os.path.join(out_root, f"capture-{stamp}")
    writer = CaptureWriter(directory)
    pane = sys.stdout.isatty() and not args.quiet
    printer = make_printer(pane)
    audio = Audio(enabled=not args.no_audio, sink=printer)
    cues = CueScheduler(audio, lambda rec: state._emit_cue(rec), 1.0)
    state = SessionState(writer, audio, cues, plan, args.plan_session if plan else None, plan_meta, "live", printer)
    state.identity = redact_identity(desc)
    printer(f"capture -> {directory}")
    printer(f"identity ok: vid={desc['vid']} pid={desc['pid']} manufacturer={desc['manufacturer']!r} product={desc['product']!r} serial=...{state.identity['serial_last4']}")
    state.marker({"type": "session_start", "identity": state.identity})

    try:
        ser = open_verified_port(desc)
    except Exception as exc:
        printer(f"could not open the verified port ({type(exc).__name__}); nothing was written to the device")
        state.marker({"type": "port_open_failed", "error": type(exc).__name__})
        writer.write_session(state.to_json())
        audio.close()
        writer.close()
        return 4
    state.marker({"type": "port_opened", "port_basename": state.identity["port_basename"]})
    keys = Keys(enabled=not args.no_keys)
    buffer = b""
    last_pane = 0.0
    last_flush = time.monotonic()
    quit_requested = False
    try:
        with keys:
            while not quit_requested:
                try:
                    chunk = ser.read(max(1, ser.in_waiting or 1))
                except Exception as exc:  # disconnect
                    state.disconnects += 1
                    state.marker({"type": "disconnect", "error": type(exc).__name__})
                    state.alarm("disconnect", "serial disconnected")
                    try:
                        ser.close()
                    except Exception:
                        pass
                    ser = None
                    while ser is None and not quit_requested:
                        key = keys.poll()
                        if key is not None and handle_key(key, state, keys):
                            quit_requested = True
                            break
                        time.sleep(1.0)
                        again = locate()
                        if again is not None:
                            try:
                                ser = open_verified_port(again)
                            except Exception:
                                ser = None
                                continue
                            state.identity = redact_identity(again)
                            state.marker({"type": "reconnect", "identity": state.identity})
                            state.clear_alarm("disconnect")
                            printer("reconnected to the verified device")
                            buffer = b""
                    continue
                if chunk:
                    host_ns = now_ns()
                    buffer += chunk
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        state.feed(line + b"\n", host_ns)
                key = keys.poll()
                if key is not None and handle_key(key, state, keys):
                    quit_requested = True
                now = time.monotonic()
                if pane and now - last_pane > 0.25:
                    sys.stdout.write("\r\033[K" + state.status_line()[: max(20, shutil.get_terminal_size((120, 24)).columns - 1)])
                    sys.stdout.flush()
                    last_pane = now
                if now - last_flush > 5:
                    writer.write_session(state.to_json())
                    last_flush = now
            if pane:
                sys.stdout.write("\n")
            finalize(state, keys, writer)
    except KeyboardInterrupt:
        keys.restore()
        state.finalized = True
        state.marker({"type": "quit", "reason": "interrupt"})
        writer.write_session(state.to_json())
    finally:
        try:
            if ser is not None:
                ser.close()
        except Exception:
            pass
        audio.close()
        writer.close()
    printer(f"capture finalized -> {directory}")
    return 0


# --------------------------------------------------------------------------
# TCP transport (wireless telemetry mirror)
# --------------------------------------------------------------------------

TCP_DEFAULT_PORT = 35533
TCP_DEFAULT_HOST = "sticks3-ptt.local"
TCP_GREETING_PREFIX = b"EVENT,TELEMETRY,CONNECTED,"
TCP_GREETING_TIMEOUT_S = 3.0
TCP_LIVENESS_TIMEOUT_S = 5.0  # device STATUS is guaranteed 1 Hz
TCP_RECONNECT_INTERVAL_S = 0.5


class TcpGuardError(Exception):
    """Refusal of the TCP identity/greeting guard. Messages never contain
    .env values, full MAC addresses, or resolved IPs."""


def _tcp_is_loopback(host: str) -> bool:
    """True only for genuinely local targets: the literal name 'localhost' or
    an ADDRESS literal that parses as loopback (127.0.0.0/8, ::1). A DNS name
    is never exempt - '127.evil.example' is a resolvable hostname, not a
    loopback address, and must take the full ping+ARP identity lane."""
    if host.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(host.strip("[]").split("%", 1)[0]).is_loopback
    except ValueError:
        return False


def parse_tcp_target(value: Optional[str], env: Dict[str, str]) -> Tuple[str, int]:
    """HOST[:PORT] -> (host, port); bare --tcp defaults the host from the
    ignored .env COLIBRINO_OTA_HOST (else sticks3-ptt.local), port 35533."""
    host = ""
    port = TCP_DEFAULT_PORT
    text = (value or "").strip()
    if text:
        if text.count(":") == 1:
            host, port_text = text.split(":", 1)
            try:
                port = int(port_text)
            except ValueError:
                raise TcpGuardError(f"--tcp port {port_text!r} is not a number")
        else:
            host = text
    if not host:
        host = env.get(cc.ENV_OTA_HOST_KEY, "").strip() or TCP_DEFAULT_HOST
    return host, port


def resolve_tcp_host(host: str, env_path: str) -> Tuple[str, Optional[str]]:
    """Identity guard, run BEFORE every connect (mirrors scripts/upload_ota.sh):
    ping resolves the IP, `arp -n` must report the MAC named by the ignored
    .env COLIBRINO_OTA_EXPECTED_MAC (case-insensitive). Loopback targets skip
    ping+ARP (for tests); the greeting requirement still applies to them.
    Returns (ip, mac_last4)."""
    if _tcp_is_loopback(host):
        return ("127.0.0.1" if host.lower() == "localhost" else host, None)
    env = cc.parse_env_file(env_path)
    expected_mac = env.get(cc.ENV_OTA_MAC_KEY, "").strip().lower()
    if not expected_mac:
        raise TcpGuardError(
            f"{cc.ENV_OTA_MAC_KEY} is not set in {env_path}; refusing to open a TCP capture to non-loopback hardware"
        )
    try:
        ping = subprocess.run(
            ["ping", "-c", "1", "-W", "2000", host],
            capture_output=True, text=True, timeout=15,
        )
    except (OSError, subprocess.SubprocessError):
        raise TcpGuardError("ping failed; cannot resolve the capture host")
    ip = ""
    for line in ping.stdout.splitlines():
        if line.startswith("PING") and "(" in line and ")" in line:
            ip = line.split("(", 1)[1].split(")", 1)[0]
            break
    if not ip:
        raise TcpGuardError("capture host did not resolve; wake the Colibrino StickS3 and retry")
    try:
        arp = subprocess.run(["arp", "-n", ip], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        raise TcpGuardError("arp lookup failed; cannot verify the capture hardware")
    actual_mac = ""
    for line in arp.stdout.splitlines():
        parts = line.split()
        if "at" in parts:
            index = parts.index("at")
            if index + 1 < len(parts):
                actual_mac = parts[index + 1].lower()
            break
    if not actual_mac or actual_mac != expected_mac:
        raise TcpGuardError(
            "refusing TCP capture: resolved hardware does not match "
            f"{cc.ENV_OTA_MAC_KEY} (identity guard; nothing was connected)"
        )
    mac_last4 = actual_mac.replace(":", "").replace("-", "")[-4:]
    return ip, mac_last4


def tcp_connect_verified(host: str, port: int, env_path: str):
    """Guarded connect: resolve+ARP check, connect, immediate SHUT_WR (this
    host physically cannot send afterwards), then REQUIRE the firmware
    greeting line within 3 s. The MAC proves the hardware, the greeting
    proves the telemetry service. Returns (sock, buffered_bytes, mac_last4);
    the buffer starts with the complete greeting line."""
    ip, mac_last4 = resolve_tcp_host(host, env_path)
    try:
        sock = socket.create_connection((ip, port), timeout=5)
    except OSError as exc:
        raise TcpGuardError(f"connect failed ({type(exc).__name__})")
    if _tcp_is_loopback(host):
        # Belt and braces: the loopback lane skipped ping+ARP, so the actual
        # peer must really be loopback or the exemption was not earned.
        try:
            peer = ipaddress.ip_address(sock.getpeername()[0].split("%", 1)[0])
            peer_ok = peer.is_loopback
        except (OSError, ValueError, IndexError):
            peer_ok = False
        if not peer_ok:
            sock.close()
            raise TcpGuardError("loopback target reached a non-loopback peer; refusing")
    try:
        sock.shutdown(socket.SHUT_WR)
    except OSError:
        sock.close()
        raise TcpGuardError("could not half-close the socket; refusing a writable stream")
    sock.settimeout(0.2)
    buffer = b""
    deadline = time.monotonic() + TCP_GREETING_TIMEOUT_S
    while b"\n" not in buffer:
        if time.monotonic() >= deadline:
            sock.close()
            raise TcpGuardError("no telemetry greeting within 3 s; aborting")
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            chunk = b""
        if not chunk:
            sock.close()
            raise TcpGuardError("connection closed before the telemetry greeting; aborting")
        buffer += chunk
    if not buffer.split(b"\n", 1)[0].startswith(TCP_GREETING_PREFIX):
        sock.close()
        raise TcpGuardError("first line is not the telemetry greeting; aborting")
    return sock, buffer, mac_last4


def run_tcp(args, plan, plan_meta) -> int:
    env_path = args.env or os.path.join(cc.repo_root(), ".env")
    env = cc.parse_env_file(env_path)
    try:
        host, port = parse_tcp_target(args.tcp, env)
    except TcpGuardError as exc:
        print(f"tcp capture refused: {exc}", file=sys.stderr)
        return 2
    try:
        sock, buffer, mac_last4 = tcp_connect_verified(host, port, env_path)
    except TcpGuardError as exc:
        print(f"tcp capture refused: {exc}", file=sys.stderr)
        return 3

    # Only a verified, greeted connection ever creates a capture directory.
    stamp = _dt.datetime.now().strftime("%Y%m%dT%H%M%S")
    out_root = args.out_dir or os.path.join(cc.sticks3_dir(), ".device-backups", "logs")
    directory = os.path.join(out_root, f"capture-{stamp}")
    suffix = 1
    while os.path.exists(directory):
        suffix += 1
        directory = os.path.join(out_root, f"capture-{stamp}-{suffix}")
    writer = CaptureWriter(directory)
    pane = sys.stdout.isatty() and not args.quiet
    printer = make_printer(pane)
    audio = Audio(enabled=not args.no_audio, sink=printer)
    cues = CueScheduler(audio, lambda rec: state._emit_cue(rec), 1.0)
    state = SessionState(writer, audio, cues, plan, args.plan_session if plan else None, plan_meta, "tcp", printer)

    def redacted_tcp_identity(last4: Optional[str]) -> Dict[str, object]:
        return {
            "transport": "tcp",
            "mac_last4": last4,
            "port": port,
            "host_is_loopback": _tcp_is_loopback(host),
        }

    state.identity = redacted_tcp_identity(mac_last4)
    state.marker({"type": "session_start", "identity": state.identity})
    printer(f"tcp capture -> {directory}")
    printer(f"identity ok: transport=tcp port={port} mac=...{mac_last4 or '(loopback)'}; stream is receive-only (SHUT_WR)")

    keys = Keys(enabled=not args.no_keys)
    give_up_s = float(getattr(args, "tcp_give_up", 0.0) or 0.0)
    quit_requested = False
    last_pane = 0.0
    last_flush = time.monotonic()

    def feed_buffered() -> None:
        nonlocal buffer, last_line_mono
        host_ns = now_ns()
        fed = False
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            state.feed(line + b"\n", host_ns)
            fed = True
        if fed:
            last_line_mono = time.monotonic()

    try:
        with keys:
            last_line_mono = time.monotonic()
            feed_buffered()  # the greeting line goes through state.feed like any line
            while not quit_requested:
                dead = False
                try:
                    chunk = sock.recv(4096)
                except socket.timeout:
                    chunk = None
                except OSError:
                    chunk = None
                    dead = True
                if chunk == b"":
                    dead = True
                elif chunk:
                    buffer += chunk
                    feed_buffered()
                if not dead and time.monotonic() - last_line_mono > TCP_LIVENESS_TIMEOUT_S:
                    state.marker({"type": "warning", "text": "no telemetry line for 5 s (device STATUS is 1 Hz); treating the stream as lost"})
                    dead = True
                if dead:
                    state.disconnects += 1
                    state.marker({"type": "disconnect", "transport": "tcp"})
                    state.alarm("disconnect", "telemetry stream lost")
                    try:
                        sock.close()
                    except Exception:
                        pass
                    sock = None
                    buffer = b""
                    give_up_at = time.monotonic() + give_up_s if give_up_s > 0 else None
                    while sock is None and not quit_requested:
                        key = keys.poll()
                        if key is not None and handle_key(key, state, keys):
                            quit_requested = True
                            break
                        if give_up_at is not None and time.monotonic() >= give_up_at:
                            printer(f"tcp reconnect gave up after {give_up_s:.1f} s; finalizing")
                            state.marker({"type": "reconnect_gave_up", "seconds": give_up_s})
                            quit_requested = True
                            break
                        time.sleep(TCP_RECONNECT_INTERVAL_S)
                        try:
                            # Guarded reconnect: the resolve+ARP+greeting gauntlet
                            # runs again in full for every attempt.
                            sock, buffer, mac_last4 = tcp_connect_verified(host, port, env_path)
                        except TcpGuardError:
                            sock = None
                            continue
                        state.identity = redacted_tcp_identity(mac_last4)
                        state.marker({"type": "reconnect", "identity": state.identity})
                        state.clear_alarm("disconnect")
                        printer("reconnected to the verified telemetry service")
                        last_line_mono = time.monotonic()
                        feed_buffered()
                    continue
                key = keys.poll()
                if key is not None and handle_key(key, state, keys):
                    quit_requested = True
                now = time.monotonic()
                if pane and now - last_pane > 0.25:
                    sys.stdout.write("\r\033[K" + state.status_line()[: max(20, shutil.get_terminal_size((120, 24)).columns - 1)])
                    sys.stdout.flush()
                    last_pane = now
                if now - last_flush > 5:
                    writer.write_session(state.to_json())
                    last_flush = now
            if pane:
                sys.stdout.write("\n")
            finalize(state, keys, writer)
    except KeyboardInterrupt:
        keys.restore()
        state.finalized = True
        state.marker({"type": "quit", "reason": "interrupt"})
        writer.write_session(state.to_json())
    finally:
        try:
            if sock is not None:
                sock.close()
        except Exception:
            pass
        audio.close()
        writer.close()
    printer(f"tcp capture finalized -> {directory}")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--discover", action="store_true", help="enumerate USB serial descriptors and exit (no port is opened)")
    parser.add_argument("--plan", help="declarative run plan JSON (e.g. tools/capture_plans/round1.json)")
    parser.add_argument("--plan-session", default="A", help="plan session id to run (default A)")
    parser.add_argument("--port", help="disambiguate when more than one verified candidate exists (never bypasses verification)")
    parser.add_argument("--wait-for-port", action="store_true", help="poll until the verified device enumerates")
    parser.add_argument("--no-audio", action="store_true", help="print a bell and text instead of afplay/say")
    parser.add_argument("--no-keys", action="store_true", help="do not read keys from the terminal")
    parser.add_argument("--quiet", action="store_true", help="no live status pane")
    parser.add_argument("--verbose-status", action="store_true", help="old full-field status line instead of the operator view")
    parser.add_argument("--simulate", metavar="RAW_LOG", help="replay an existing device log without a serial port")
    parser.add_argument(
        "--tcp",
        nargs="?",
        const="",
        default=None,
        metavar="HOST[:PORT]",
        help="capture over the wireless telemetry mirror (default host from the ignored .env COLIBRINO_OTA_HOST else sticks3-ptt.local, port 35533; identity-guarded via ping+ARP like scripts/upload_ota.sh)",
    )
    parser.add_argument(
        "--tcp-give-up",
        type=float,
        default=0.0,
        metavar="SECONDS",
        help="finalize after this many seconds of failed TCP reconnection (0 = keep trying until q)",
    )
    parser.add_argument("--sim-speed", default=50.0, type=float, help="simulate pacing factor (0 = as fast as possible)")
    parser.add_argument("--out-dir", help="override the capture root (default sticks3/.device-backups/logs)")
    parser.add_argument("--env", help="override the .env path (default repo-root .env)")
    args = parser.parse_args(argv)

    global VERBOSE_STATUS
    VERBOSE_STATUS = bool(getattr(args, "verbose_status", False))

    if args.discover:
        return discover()

    plan = None
    plan_meta = None
    if args.plan:
        plan = cc.load_plan(args.plan)
        cc.plan_session(plan, args.plan_session)
        rel = os.path.relpath(os.path.abspath(args.plan), cc.repo_root())
        plan_meta = {"path": rel, "sha256": cc.sha256_file(args.plan), "session_id": args.plan_session, "name": plan.get("name")}

    if args.simulate:
        return run_simulate(args, plan, plan_meta)
    if args.tcp is not None:
        return run_tcp(args, plan, plan_meta)
    return run_live(args, plan, plan_meta)


if __name__ == "__main__":
    sys.exit(main())
