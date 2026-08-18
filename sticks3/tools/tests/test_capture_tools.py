#!/usr/bin/env python3
"""Unit tests for the v2 trace capture tooling (stdlib unittest only).

Run from sticks3/:  python3 -m unittest tools/tests/test_capture_tools.py -v

Retained device logs under sticks3/.device-backups/logs/ are git-ignored and
private; tests that need them are skipped when they are absent. Synthetic
logs are generated here so the acceptance and refinement logic is testable on
any machine that has a C++17 compiler for the replay tool.

[TEST] Everything in this file is synthetic or replayed; nothing here is a
hardware validation.
"""

from __future__ import annotations

import io
import json
import math
import os
import random
import shutil
import socket
import sys
import tempfile
import threading
import types
import unittest
from contextlib import redirect_stdout, redirect_stderr

TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STICKS3_DIR = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)

import capture_common as cc  # noqa: E402
import capture_session  # noqa: E402
import make_trace_fixture as mtf  # noqa: E402

LOG_DIR = os.path.join(STICKS3_DIR, ".device-backups", "logs")
RETAINED_LOGS = {
    "260814-232730": os.path.join(LOG_DIR, "device-monitor-260814-232730.log"),
    "260815-151549": os.path.join(LOG_DIR, "device-monitor-260815-151549.log"),
    "260814-234754": os.path.join(LOG_DIR, "logs", "device-monitor-260814-234754.log"),
}
EXPECTED_SESSIONS = {"260814-232730": 1, "260815-151549": 5, "260814-234754": 1}
PLAN_PATH = os.path.join(TOOLS_DIR, "capture_plans", "round1.json")

STAGE_MS = {"KEEP_HEAD_STILL": 6000, "BLINK_FIRMLY": 15000, "MOVE_HEAD": 12000, "CAPTURE_FREE_RUN": 10000}
PREPARE_MS = 3000
IMPULSE_MS = 60
BASE = (0.3, -0.2, 0.1)


# --------------------------------------------------------------------------
# Synthetic device log generator
# --------------------------------------------------------------------------


def coded_pattern(start_ms: int):
    """Impulse start times for double, pause, double (500 / 1100 / 500 ms)."""
    return [start_ms, start_ms + 500, start_ms + 1600, start_ms + 2100]


def _status(ms: int, imu_stage: str = "IDLE", mode: str = "MOTION", build: str = None, batt: int = 87, tele: int = 1) -> str:
    line = (
        f"STATUS,{ms},board=StickS3,mode={mode},armed=0,imu=1,calibrated=1,imu_blink=0,ir=0,ota=READY,"
        f"ir_stage=IDLE,imu_stage={imu_stage},gyro_x=0.061,gyro_y=-0.061,gyro_z=0.000"
    )
    if build is not None:
        # Wireless-telemetry firmware appends ,build=<sha>,batt=<pct>,tele=<0|1>.
        line += f",build={build},batt={batt},tele={tele}"
    return line


def _gyro(t_rel: int, kind: str, impulses):
    if kind == "sweep":
        return (30.0 * math.sin(2 * math.pi * 0.5 * t_rel / 1000.0), 4.0, -2.0)
    noise = 0.0610 if (t_rel // 5) % 2 == 0 else -0.0610
    active = any(start <= t_rel < start + IMPULSE_MS for start in impulses)
    bump = 1.8 if active else 0.0
    return (BASE[0] + bump + noise, BASE[1] - noise, BASE[2] + noise)


def _rows(lines, ms: int, stage: str, kind: str, impulses):
    duration = STAGE_MS[stage]
    t_rel = 0
    sample = 0
    while t_rel < duration:
        if sample % 7 != 6:  # bursty dropout every seventh sample (~171 Hz)
            gx, gy, gz = _gyro(t_rel, kind, impulses)
            lines.append(
                f"IMU,{ms + t_rel},{(ms + t_rel) * 1000 + 94},{stage},{gx:.4f},{gy:.4f},{gz:.4f},-0.05347,0.99268,-0.12402"
            )
        t_rel += 5
        sample += 1
    return ms + duration


def emit_session(lines, ms: int, spec) -> int:
    """spec = {stage: (kind, impulse_starts_relative)}, kind in quiet|sweep."""
    lines.append("CSV: IMU,ms,usec,stage,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g")
    lines.append("EVENT,IMU_PROBE_STARTED")
    samples = 0
    for prepare, stage in (("PREPARE_STILL", "KEEP_HEAD_STILL"), ("PREPARE_BLINKS", "BLINK_FIRMLY"), ("PREPARE_HEAD", "MOVE_HEAD")):
        lines.append(f"EVENT,IMU_PROBE_STAGE,{prepare}")
        for k in range(3):
            lines.append(_status(ms + 1000 * k, prepare))
        ms += PREPARE_MS
        lines.append(f"EVENT,IMU_PROBE_STAGE,{stage}")
        before = len(lines)
        kind, impulses = spec[stage]
        ms = _rows(lines, ms, stage, kind, impulses)
        samples += len(lines) - before
    lines.append("EVENT,IMU_PROBE_STAGE,CAPTURE_COMPLETE")
    lines.append(f"EVENT,IMU_PROBE_COMPLETE,samples={samples}")
    still, blink, head = spec.get("expected", (0, 0, 0))
    verdict = "PASS" if still == 0 and blink >= 2 and head == 0 else "NOT_PROVEN"
    lines.append(f"RESULT,IMU_BLINK,{verdict},still={still},blink={blink},head={head}")
    for k in range(5):
        lines.append(_status(ms + 1000 * k, "CAPTURE_COMPLETE"))
    return ms + 5000


def build_synthetic_log(path: str, with_v2: bool = False):
    """Four probe sessions (five with `with_v2`):

    s1 quiet everywhere                     (R0 slot under plan A)
    s2 two coded patterns in BLINK -> PASS  (V1 slot)
    s3 hard singles at cue+250 ms, cue 5 missing, cue 6 doubled (HB1 slot)
    s4 coded pattern in STILL + two in BLINK -> control violation (U slot)
    s5 (optional) two coded patterns in BLINK -> PASS (V2 slot after the
       BREAK_REMOUNT marker, which the capture tool auto-completes)
    """
    lines = []
    for k in range(65):  # boot STATUS block, 1 Hz from 1.03 s
        lines.append(_status(1030 + 1000 * k, "IDLE", "IR PROBE"))
    ms = 70_000
    quiet = ("quiet", [])
    sweep = ("sweep", [])
    ms = emit_session(lines, ms, {"KEEP_HEAD_STILL": quiet, "BLINK_FIRMLY": quiet, "MOVE_HEAD": sweep, "expected": (0, 0, 0)})
    ms = emit_session(
        lines,
        ms,
        {"KEEP_HEAD_STILL": quiet, "BLINK_FIRMLY": ("quiet", coded_pattern(2000) + coded_pattern(7000)), "MOVE_HEAD": sweep, "expected": (0, 2, 0)},
    )
    singles = [750 + 1500 * k + 250 for k in range(6)]
    singles = singles[:4] + [singles[5] - 50, singles[5] + 200]  # cue 5 missing, cue 6 doubled
    ms = emit_session(lines, ms, {"KEEP_HEAD_STILL": quiet, "BLINK_FIRMLY": ("quiet", singles), "MOVE_HEAD": sweep, "expected": (0, 0, 0)})
    ms = emit_session(
        lines,
        ms,
        {
            "KEEP_HEAD_STILL": ("quiet", coded_pattern(2000)),
            "BLINK_FIRMLY": ("quiet", coded_pattern(2000) + coded_pattern(7000)),
            "MOVE_HEAD": sweep,
            "expected": (1, 2, 0),
        },
    )
    if with_v2:
        ms = emit_session(
            lines,
            ms,
            {"KEEP_HEAD_STILL": quiet, "BLINK_FIRMLY": ("quiet", coded_pattern(2500) + coded_pattern(8000)), "MOVE_HEAD": sweep, "expected": (0, 2, 0)},
        )
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


def emit_free_run_session(lines, ms: int, free_count: int = 2) -> int:
    """One free-run session, byte-shaped like the firmware emits it: STARTED,
    PREPARE_STILL, then the second Button-A tap converts the guided start into
    CAPTURE_FREE_RUN (no second STARTED line - the stage transition is the
    marker), rows stream with that stage, and the RESULT is always NOT_PROVEN
    with the trailing ,free=<n> field."""
    lines.append("CSV: IMU,ms,usec,stage,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g")
    lines.append("EVENT,IMU_PROBE_STARTED")
    lines.append("EVENT,IMU_PROBE_STAGE,PREPARE_STILL")
    for k in range(2):
        lines.append(_status(ms + 1000 * k, "PREPARE_STILL", build="e1b1c48"))
    ms += 1500  # second tap lands mid-preparation
    lines.append("EVENT,IMU_PROBE_STAGE,CAPTURE_FREE_RUN")
    before = len(lines)
    impulses = coded_pattern(2000) + coded_pattern(6500)  # what the wearer did
    ms = _rows(lines, ms, "CAPTURE_FREE_RUN", "quiet", impulses)
    samples = len(lines) - before
    lines.append("EVENT,IMU_PROBE_STAGE,CAPTURE_COMPLETE")
    lines.append(f"EVENT,IMU_PROBE_COMPLETE,samples={samples}")
    lines.append(f"RESULT,IMU_BLINK,NOT_PROVEN,still=0,blink=0,head=0,free={free_count}")
    for k in range(5):
        lines.append(_status(ms + 1000 * k, "CAPTURE_COMPLETE", build="e1b1c48"))
    return ms + 5000


def build_free_run_log(path: str, leading_quiet_session: bool = False):
    """Boot STATUS block (with the new build/batt/tele tail) followed by an
    optional quiet guided session (so a plan's R0 slot is consumed) and one
    free-run session."""
    lines = []
    for k in range(65):
        lines.append(_status(1030 + 1000 * k, "IDLE", "IR PROBE", build="e1b1c48"))
    ms = 70_000
    if leading_quiet_session:
        quiet = ("quiet", [])
        ms = emit_session(lines, ms, {"KEEP_HEAD_STILL": quiet, "BLINK_FIRMLY": quiet, "MOVE_HEAD": ("sweep", []), "expected": (0, 0, 0)})
    ms = emit_free_run_session(lines, ms)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


class TelemetryStubServer(threading.Thread):
    """[TEST] Loopback stand-in for the firmware telemetry mirror.

    Each entry of `scripts` describes one accepted connection:
      greeting  (default True)  send EVENT,TELEMETRY,CONNECTED,<build> first
      bad_first_line            send a STATUS line instead of the greeting
      chunks                    byte strings sent after the greeting
    Before sending anything the server reads until EOF, recording every recv
    result: a client that honoured SHUT_WR yields exactly [b""] per
    connection, proving zero bytes were ever sent after connect.
    """

    def __init__(self, scripts):
        super().__init__(daemon=True)
        self.scripts = scripts
        self.received = []  # one list of recv() results per connection
        self.errors = []
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]

    def run(self):
        try:
            for script in self.scripts:
                conn, _ = self.listener.accept()
                conn.settimeout(5)
                got = []
                try:
                    while True:
                        data = conn.recv(1024)
                        got.append(data)
                        if not data:
                            break
                except socket.timeout:
                    got.append(b"<recv timeout>")
                self.received.append(got)
                try:
                    if script.get("bad_first_line"):
                        conn.sendall(b"STATUS,1,board=StickS3,mode=MOTION\n")
                    elif script.get("greeting", True):
                        conn.sendall(b"EVENT,TELEMETRY,CONNECTED,e1b1c48\n")
                    for chunk in script.get("chunks", []):
                        conn.sendall(chunk)
                finally:
                    conn.close()
        except Exception as exc:  # pragma: no cover - surfaced via self.errors
            self.errors.append(repr(exc))
        finally:
            self.listener.close()


def as_live_capture(sim_dir: str, dest_root: str, stamp: str = "20260901T000000") -> str:
    """[TEST] Reshape a --simulate output into a live-shaped capture directory.

    Same raw bytes, markers and run mapping; only session.json `mode` becomes
    "live" (with a redacted-identity block instead of simulated_source) and the
    directory takes a live capture name. This is how the tests exercise the
    live-only paths (cue refinement, promotion of clean fixtures, worn
    acceptance) without a device.
    """
    dest = os.path.join(dest_root, f"capture-{stamp}")
    if os.path.exists(dest):
        shutil.rmtree(dest)
    shutil.copytree(sim_dir, dest)
    session_path = os.path.join(dest, "session.json")
    with open(session_path, "r", encoding="utf-8") as handle:
        session = json.load(handle)
    session["mode"] = "live"
    session["simulated_source"] = None
    session["identity"] = {"vid": "0x303a", "pid": "0x1001", "manufacturer": cc.USB_MANUFACTURER, "product": cc.USB_PRODUCT, "serial_last4": "0000", "serial_length": 12, "port_basename": "cu.test"}
    session["capture_dir"] = os.path.basename(dest)
    with open(session_path, "w", encoding="utf-8") as handle:
        handle.write(cc.json_dumps_stable(session))
    return dest


def run_cli(module_main, argv):
    out = io.StringIO()
    err = io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = module_main(argv)
    return code, out.getvalue(), err.getvalue()


# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------


class CaptureToolsBase(unittest.TestCase):
    tmp: str
    replay_bin: str
    synthetic_log: str

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="colibrino-capture-tests-")
        cls.replay_bin = os.path.join(cls.tmp, "replay_imu_capture")
        cc.build_replay_tool(cls.replay_bin, STICKS3_DIR)
        # The date in the name is what a simulated capture must report as its
        # capture_date (the rehearsal day never is).
        cls.synthetic_log = build_synthetic_log(os.path.join(cls.tmp, "synthetic-device-monitor-260901-000000.log"))
        cls.synthetic_log_v2 = build_synthetic_log(os.path.join(cls.tmp, "synthetic-device-monitor-260902-000000-v2.log"), with_v2=True)
        cls.free_run_log = build_free_run_log(os.path.join(cls.tmp, "synthetic-device-monitor-260903-000000-freerun.log"))
        cls.free_run_plan_log = build_free_run_log(
            os.path.join(cls.tmp, "synthetic-device-monitor-260904-000000-freerun-plan.log"),
            leading_quiet_session=True,
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def _simulate(self, log_path: str, plan: bool, name: str, plan_session: str = "A") -> str:
        out_root = os.path.join(self.tmp, name)
        argv = ["--simulate", log_path, "--no-audio", "--no-keys", "--quiet", "--sim-speed", "0", "--out-dir", out_root]
        if plan:
            argv += ["--plan", PLAN_PATH, "--plan-session", plan_session]
        code, out, err = run_cli(capture_session.main, argv)
        self.assertEqual(code, 0, err + out)
        dirs = [d for d in os.listdir(out_root) if d.startswith("capture-sim-")]
        self.assertEqual(len(dirs), 1)
        return os.path.join(out_root, dirs[0])

    def _live(self, log_path: str, name: str, plan_session: str = "A", stamp: str = "20260901T000000") -> str:
        """Simulate, then reshape into a live-shaped capture directory."""
        sim_dir = self._simulate(log_path, plan=True, name=name + "-sim", plan_session=plan_session)
        return as_live_capture(sim_dir, os.path.join(self.tmp, name), stamp)


class TestParsingAndSplitting(CaptureToolsBase):
    def test_retained_logs_split_into_expected_sessions(self):
        checked = 0
        for key, path in RETAINED_LOGS.items():
            if not os.path.isfile(path):
                continue
            sessions = cc.split_sessions(cc.read_lines(path))
            self.assertEqual(len(sessions), EXPECTED_SESSIONS[key], key)
            for session in sessions:
                for stage in cc.CAPTURE_STAGES:
                    self.assertIn(stage, session.stages, f"{key} s{session.index} lacks {stage}")
                    self.assertGreater(len(session.stages[stage].rows), 500)
            checked += 1
        if checked == 0:
            self.skipTest("retained device logs are not present on this machine")

    def test_splitter_matches_replay_tool_session_count(self):
        for key, path in RETAINED_LOGS.items():
            if not os.path.isfile(path):
                continue
            replay = cc.run_replay(self.replay_bin, path)
            self.assertEqual(replay["summary"]["sessions"], EXPECTED_SESSIONS[key], key)  # type: ignore[index]
            splitter = cc.split_sessions(cc.read_lines(path))
            for entry, session in zip(replay["sessions"], splitter):  # type: ignore[arg-type]
                for stage in cc.CAPTURE_STAGES:
                    self.assertEqual(entry["stages"][stage]["samples"], len(session.stages[stage].rows), f"{key} {stage}")  # type: ignore[index]

    def test_line_parsers(self):
        row = cc.parse_imu("IMU,634132,634133103,KEEP_HEAD_STILL,-0.4883,-0.1831,0.2441,-0.05957,0.99292,-0.13013")
        self.assertIsNotNone(row)
        assert row is not None
        self.assertEqual(row.ms, 634132)
        self.assertEqual(row.raw_values[0], "-0.4883")
        self.assertIsNone(cc.parse_imu("IMU,1,2,KEEP_HEAD_STILL,DE:AD:BE:EF:00:01,0,0,0,0,0"))
        status = cc.parse_status("STATUS,564053,board=StickS3,mode=IR PROBE,armed=0,imu=1,calibrated=1,imu_blink=0,ir=0,ota=READY,ir_stage=IDLE,imu_stage=IDLE,gyro_x=2.441,gyro_y=12.024,gyro_z=-0.305")
        assert status is not None
        self.assertEqual(status[0], 564053)
        self.assertEqual(status[1]["mode"], "IR PROBE")
        self.assertEqual(status[1]["ota"], "READY")
        self.assertEqual(cc.parse_event("EVENT,IMU_PROBE_STAGE,BLINK_FIRMLY"), ("IMU_PROBE_STAGE", ["BLINK_FIRMLY"]))
        self.assertEqual(cc.parse_result("RESULT,IMU_BLINK,NOT_PROVEN,still=1,blink=0,head=0"), {"verdict": "NOT_PROVEN", "still": 1, "blink": 0, "head": 0})

    def test_synthetic_log_matches_detector(self):
        # Sanity check of the generator against the real detector build.
        replay = cc.run_replay(self.replay_bin, self.synthetic_log)
        self.assertEqual(replay["summary"]["sessions"], 4)  # type: ignore[index]
        results = [(cc.replay_counts(e), e["result"]) for e in replay["sessions"]]  # type: ignore[index]
        self.assertEqual(results[0], ({"still": 0, "blink": 0, "head": 0}, "NOT_PROVEN"))
        self.assertEqual(results[1], ({"still": 0, "blink": 2, "head": 0}, "PASS"))
        self.assertEqual(results[2], ({"still": 0, "blink": 0, "head": 0}, "NOT_PROVEN"))
        self.assertEqual(results[3], ({"still": 1, "blink": 2, "head": 0}, "NOT_PROVEN"))
        self.assertFalse(replay["summary"]["controls_clean"])  # type: ignore[index]


class TestAlignment(unittest.TestCase):
    def test_lower_envelope_recovers_known_offset(self):
        rng = random.Random(1234)
        base_ns = 5_000_000_000_000
        min_delay_ns = 3_000_000  # 3 ms USB/host floor
        pairs = []
        for i in range(2000):
            device_ms = 100_000 + 5 * i
            jitter_ns = int(rng.uniform(0, 30_000_000))  # up to 30 ms of extra delay
            pairs.append((base_ns + device_ms * 1_000_000 + min_delay_ns + jitter_ns, device_ms))
        estimate = cc.estimate_alignment(pairs)
        assert estimate is not None
        self.assertEqual(estimate["method"], "lower_envelope_p01")
        self.assertEqual(estimate["provenance"], "ESTIMATED")
        recovered = estimate["offset_ns"]
        self.assertLess(abs(recovered - (base_ns + min_delay_ns)), 2_000_000)  # type: ignore[operator]
        self.assertGreaterEqual(estimate["residual_ms"]["p50"], 0.0)  # type: ignore[index]

    def test_empty(self):
        self.assertIsNone(cc.estimate_alignment([]))


class TestDeidGuard(unittest.TestCase):
    def test_red_controls(self):
        cases = {
            "mac_address": "IMU,1,2,BLINK_FIRMLY,DE:AD:BE:EF:00:01,0,0,0,0,0",
            "device_hostname": "provenance host sticks3-ptt.local",
            "ipv4_address": "peer=192.168.4.23 port=3232",
            "home_path": "source=/Users/example/dev/Colibrino/raw.log",
            "time_of_day": "captured at 12:34:56 local",
            "ssid_like_key": "WIFI_SSID=example",
        }
        for rule, text in cases.items():
            self.assertIn(rule, cc.deid_scan(text), rule)
        # The device serial literal rule is exercised from the guard's own pattern
        # so this test file does not repeat the identifier.
        literal = dict(cc.DEID_RULES)["device_mac_literal"].pattern.replace("(?i)", "")
        self.assertIn("device_mac_literal", cc.deid_scan("serial=" + literal.lower()))

    def test_clean_fixture_content_passes(self):
        clean = "\n".join(
            [
                mtf.CSV_HEADER,
                "634132,634133103,-0.4883,-0.1831,0.2441,-0.05957,0.99292,-0.13013",
                '{"capture_date": "2026-08-15", "effective_rate_hz": 165.3, "t_start_ms": 634132}',
                mtf.TSV_HEADER,
                "blink_firmly-whole\tcoded_pattern\t643134\t658132\tCLICK_CANDIDATE\tge\t2\t0",
            ]
        )
        self.assertEqual(cc.deid_scan(clean), [])


class TestSerialGuard(unittest.TestCase):
    def test_open_verified_port_asserts_dtr_rts_and_never_writes(self):
        """[TEST] The firmware's TinyUSB CDC only transmits while the host holds
        DTR, so the port must be opened like `pio device monitor`: 115200,
        DTR/RTS asserted once, nothing written, no toggling after open."""

        events = []

        class StubSerial:
            def __init__(self):
                self.port = None
                self.baudrate = None
                self.timeout = None
                self.is_open = False
                self._dtr = None
                self._rts = None

            @property
            def dtr(self):
                return self._dtr

            @dtr.setter
            def dtr(self, value):
                events.append(("dtr", value, self.is_open))
                self._dtr = value

            @property
            def rts(self):
                return self._rts

            @rts.setter
            def rts(self, value):
                events.append(("rts", value, self.is_open))
                self._rts = value

            def open(self):
                events.append(("open", self.baudrate, self._dtr, self._rts))
                self.is_open = True

            def write(self, data):
                events.append(("write", data))
                raise AssertionError("capture tool must never write to the device")

        saved = capture_session.serial
        capture_session.serial = types.SimpleNamespace(Serial=StubSerial)
        try:
            ser = capture_session.open_verified_port({"device": "stub-port"})
        finally:
            capture_session.serial = saved
        self.assertTrue(ser.is_open)
        self.assertEqual(ser.baudrate, 115200)
        self.assertIs(ser.dtr, True)
        self.assertIs(ser.rts, True)
        self.assertIn(("open", 115200, True, True), events)
        self.assertFalse([e for e in events if e[0] == "write"])
        # No modem-line change after open (that sequence is the ESP32-S3 reset dance).
        after_open = events[events.index(("open", 115200, True, True)) + 1 :]
        self.assertEqual([e for e in after_open if e[0] in ("dtr", "rts")], [])
        self.assertNotIn("1200", str(events))


class TestSimulateAndFixtures(CaptureToolsBase):
    def test_simulate_writes_capture_directory_and_runs(self):
        capture_dir = self._simulate(self.synthetic_log, plan=True, name="sim-plan")
        for fname in ("raw.log", "hosttime.tsv", "markers.jsonl", "session.json"):
            self.assertTrue(os.path.isfile(os.path.join(capture_dir, fname)), fname)
        with open(os.path.join(capture_dir, "raw.log"), "rb") as a, open(self.synthetic_log, "rb") as b:
            self.assertEqual(a.read(), b.read(), "raw.log must be the device bytes verbatim")
        with open(os.path.join(capture_dir, "session.json"), "r", encoding="utf-8") as handle:
            session = json.load(handle)
        self.assertEqual(session["mode"], "simulate")
        self.assertEqual(session["simulated_source"]["basename"], os.path.basename(self.synthetic_log))
        self.assertEqual(session["simulated_source"]["sha256"], cc.sha256_file(self.synthetic_log))
        self.assertEqual(session["probe_sessions"], 4)
        self.assertTrue(session["boot"]["seen"])
        self.assertTrue(session["boot"]["boot_ok"])
        self.assertGreaterEqual(session["boot"]["status_seconds_observed_before_first_probe"], 60)
        by_id = {run["run_id"]: run for run in session["runs"]}
        self.assertEqual(by_id["BOOT"]["status"], "done")
        self.assertEqual([by_id[r]["attempts"][0]["session_index"] for r in ("R0", "V1", "HB1", "U")], [1, 2, 3, 4])
        self.assertEqual(by_id["V1"]["attempts"][0]["result"]["verdict"], "PASS")
        self.assertEqual(by_id["V2"]["status"], "pending")
        self.assertFalse(by_id["V1"]["cleared"])
        self.assertEqual(len(session["alignment"]), 4)
        self.assertEqual(session["alignment"][0]["method"], "lower_envelope_p01")
        with open(os.path.join(capture_dir, "hosttime.tsv"), encoding="utf-8") as handle:
            hosttime = [l for l in handle.read().splitlines() if not l.startswith("#")]
        self.assertEqual(len(hosttime), session["line_count"])
        with open(os.path.join(capture_dir, "markers.jsonl"), encoding="utf-8") as handle:
            cues = [json.loads(l) for l in handle if '"cue"' in l]
        hb_cues = [c for c in cues if c["run_id"] == "HB1" and c["stage"] == "BLINK_FIRMLY"]
        self.assertEqual([c["t_offset_ms"] for c in hb_cues], [750.0 + 1500 * k for k in range(6)])
        u_cues = [c for c in cues if c["run_id"] == "U"]
        self.assertEqual(len(u_cues), 8)

    def test_simulate_five_sessions_lands_v2_after_break_marker(self):
        capture_dir = self._simulate(self.synthetic_log_v2, plan=True, name="sim-plan-v2")
        with open(os.path.join(capture_dir, "session.json"), "r", encoding="utf-8") as handle:
            session = json.load(handle)
        by_id = {run["run_id"]: run for run in session["runs"]}
        self.assertEqual(by_id["BREAK_REMOUNT"]["status"], "done")
        self.assertEqual(by_id["V2"]["attempts"][0]["session_index"], 5)
        self.assertEqual(by_id["V2"]["attempts"][0]["result"]["verdict"], "PASS")

    def test_simulate_without_plan_names_runs_sim_n(self):
        capture_dir = self._simulate(self.synthetic_log, plan=False, name="sim-noplan")
        with open(os.path.join(capture_dir, "session.json"), "r", encoding="utf-8") as handle:
            session = json.load(handle)
        self.assertEqual([r["run_id"] for r in session["runs"]], ["SIM-1", "SIM-2", "SIM-3", "SIM-4"])

    def test_fixture_determinism_and_tsv_derivation(self):
        capture_dir = self._live(self.synthetic_log, name="live-fixture")
        digests = []
        for attempt in ("a", "b"):
            out = os.path.join(self.tmp, f"fixtures-{attempt}")
            code, stdout, stderr = run_cli(mtf.main, [capture_dir, "--out", out, "--replay-bin", self.replay_bin])
            self.assertEqual(code, 0, stderr + stdout)
            names = sorted(os.listdir(out))
            self.assertEqual(len(names), 12 * 3, names)  # 4 sessions x 3 stages x 3 files
            digests.append({n: cc.sha256_file(os.path.join(out, n)) for n in names})
        self.assertEqual(digests[0], digests[1], "same input must give identical bytes")
        out = os.path.join(self.tmp, "fixtures-a")
        for name in os.listdir(out):
            if not name.endswith(".labels.json"):
                continue
            self.assertNotIn("-sim.", name)
            with open(os.path.join(out, name), "r", encoding="utf-8") as handle:
                labels = json.load(handle)
            self.assertEqual(labels["schema"], "colibrino-v2-trace-labels/1")
            self.assertEqual(labels["capture"]["capture_date"], "2026-09-01")  # from the live directory stamp
            self.assertIn(labels["capture"]["run_kind"], cc.RUN_KINDS)
            self.assertIn(labels["capture"]["mount"], cc.MOUNTS)
            self.assertIn(labels["provenance"]["labeler"], cc.LABELERS)
            self.assertTrue(labels["provenance"]["deidentified"])
            self.assertFalse(labels["provenance"]["simulated"])
            self.assertEqual(labels["provenance"]["source_kind"], "capture-session")
            self.assertEqual(labels["provenance"]["capture_mode"], "live")
            self.assertIsNone(labels["provenance"]["cleared_on"])
            self.assertEqual(labels["harness"]["preroll_ms"], 2000)
            for seg in labels["segments"]:
                self.assertIn(seg["kind"], cc.SEGMENT_KINDS)
                self.assertIn(seg["expect_source"], cc.EXPECT_SOURCES)
            tsv_path = os.path.join(out, name.replace(".labels.json", ".labels.tsv"))
            with open(tsv_path, "r", encoding="utf-8") as handle:
                self.assertEqual(handle.read(), mtf.derive_tsv(labels), name)
            csv_path = os.path.join(out, labels["csv"])
            self.assertEqual(cc.sha256_file(csv_path), labels["csv_sha256"])
            with open(csv_path, "r", encoding="utf-8") as handle:
                header = handle.readline().rstrip("\n")
                self.assertEqual(header, mtf.CSV_HEADER)
        # Cue refinement on the HB1 session: 4 ok, 1 missed, 1 ambiguous => review required.
        with open(os.path.join(out, "20260901-s3-blink_firmly-hb_hard_singles.labels.json"), "r", encoding="utf-8") as handle:
            hb = json.load(handle)
        statuses = [r["refine_status"] for r in hb["cue_refinement"]]
        self.assertEqual(statuses[:4], ["ok"] * 4)
        self.assertEqual(statuses[4], "missed")
        self.assertEqual(statuses[5], "ambiguous")
        self.assertTrue(hb["review_required"])
        for r in hb["cue_refinement"][:4]:
            self.assertLessEqual(abs(r["peak_ms"] - (r["cue_ms"] + 250)), 15)
        self.assertEqual(len([seg for seg in hb["segments"] if "cue_ms" in seg]), 6)
        # Six cues each expecting one impulse, but the detector accepted fewer: flagged.
        self.assertTrue(any("per-cue IMPULSE" in reason for reason in hb["review_reasons"]), hb["review_reasons"])
        self.assertIn("REVIEW REQUIRED", stdout)
        # V1 fixture: no cues, coded_pattern ge 2 satisfied by replay (2), parity match, no review needed.
        with open(os.path.join(out, "20260901-s2-blink_firmly-v_standard.labels.json"), "r", encoding="utf-8") as handle:
            v1 = json.load(handle)
        self.assertEqual(v1["capture"]["run_id"], "V1")
        self.assertEqual(v1["replay"]["parity"], "match")
        self.assertEqual(v1["replay"]["stage_sequences"], 2)
        self.assertFalse(v1["review_required"], v1["review_reasons"])
        self.assertEqual(v1["segments"][0]["expect"], {"CLICK_CANDIDATE": {"ge": 2}})
        # U fixture: plan expects CLICK_CANDIDATE eq 0 over the whole stage but the replay
        # found two coded sequences -> whole-stage contradiction must be flagged.
        with open(os.path.join(out, "20260901-s4-blink_firmly-u_uniform_four.labels.json"), "r", encoding="utf-8") as handle:
            u = json.load(handle)
        self.assertTrue(u["review_required"])
        self.assertTrue(any("whole-stage expect CLICK_CANDIDATE eq 0 but replay sequences=2" in r for r in u["review_reasons"]), u["review_reasons"])
        with open(os.path.join(out, "20260901-s4-keep_head_still-u_uniform_four.labels.json"), "r", encoding="utf-8") as handle:
            u_still = json.load(handle)
        self.assertTrue(any("eq 0 but replay sequences=1" in r for r in u_still["review_reasons"]), u_still["review_reasons"])

    def test_whole_stage_expectation_contradicting_replay_is_flagged(self):
        # Direct check of the label-vs-replay rule, including the V-run case
        # (coded_pattern ge 2) over data with zero coded sequences.
        seg_whole = mtf._segment("blink_firmly-whole", "coded_pattern", 1000, 16000, {"CLICK_CANDIDATE": {"ge": 2}}, 0, "instruction-window")
        seg_window = mtf._segment("first-half", "coded_pattern", 1000, 8000, {"CLICK_CANDIDATE": {"eq": 0}}, 0, "instruction-window")
        cue_a = mtf._segment("cue-hb1", "hard_blink", 1500, 1850, {"IMPULSE": {"eq": 1}}, 100, "instruction-window", cue_ms=1400.0, peak_ms=1600, refine_status="ok")
        cue_b = mtf._segment("cue-hb2", "hard_blink", 3000, 3350, {"IMPULSE": {"eq": 1}}, 100, "instruction-window", cue_ms=2900.0, peak_ms=None, refine_status="missed")
        reasons = mtf.replay_consistency_reasons([seg_whole, seg_window, cue_a, cue_b], 1000, 16000, {"samples": 2500, "impulses": 1, "sequences": 0})
        self.assertEqual(len(reasons), 2, reasons)
        self.assertIn("whole-stage expect CLICK_CANDIDATE ge 2 but replay sequences=0", reasons[0])
        self.assertIn("need >= 2 impulses but replay impulses=1", reasons[1])
        # Satisfied expectations produce nothing; sub-stage windows are not judged.
        self.assertEqual(mtf.replay_consistency_reasons([seg_whole, seg_window, cue_a, cue_b], 1000, 16000, {"samples": 2500, "impulses": 2, "sequences": 3}), [])
        self.assertEqual(mtf.replay_consistency_reasons([seg_whole], 1000, 16000, None), [])
        eq_zero = mtf._segment("keep_head_still-whole", "rest", 0, 6000, {"CLICK_CANDIDATE": {"eq": 0}}, 0, "instruction-window")
        self.assertEqual(mtf.replay_consistency_reasons([eq_zero], 0, 6000, {"samples": 1000, "impulses": 4, "sequences": 1}), ["segment keep_head_still-whole: whole-stage expect CLICK_CANDIDATE eq 0 but replay sequences=1"])
        # A window that happens to span the whole stage is judged too.
        span = mtf._segment("all", "confounder", 0, 6000, {"CLICK_CANDIDATE": {"le": 0}}, 250, "instruction-window")
        self.assertEqual(len(mtf.replay_consistency_reasons([span], 0, 6000, {"samples": 1000, "impulses": 0, "sequences": 2})), 1)

    def test_simulated_capture_fixtures_are_marked_and_dated_from_source(self):
        capture_dir = self._simulate(self.synthetic_log, plan=True, name="sim-marked")
        out = os.path.join(self.tmp, "fixtures-sim-marked")
        code, stdout, stderr = run_cli(mtf.main, [capture_dir, "--out", out, "--replay-bin", self.replay_bin])
        self.assertEqual(code, 0, stderr + stdout)
        self.assertIn("SIMULATED capture", stdout)
        names = sorted(os.listdir(out))
        self.assertEqual(len(names), 12 * 3)
        for name in names:
            self.assertTrue(name.startswith("20260901-"), name)  # source log date, not the rehearsal date
            self.assertRegex(name, r"-sim\.(csv|labels\.json|labels\.tsv)$")
        with open(os.path.join(out, "20260901-s3-blink_firmly-hb_hard_singles-sim.labels.json"), "r", encoding="utf-8") as handle:
            hb = json.load(handle)
        self.assertEqual(hb["capture"]["capture_date"], "2026-09-01")
        self.assertTrue(hb["provenance"]["simulated"])
        self.assertEqual(hb["provenance"]["source_kind"], "capture-session-simulated")
        self.assertEqual(hb["provenance"]["capture_mode"], "simulate")
        self.assertEqual(hb["provenance"]["simulated_source_sha256"], cc.sha256_file(self.synthetic_log))
        self.assertEqual(hb["cue_refinement"], [])
        self.assertEqual([seg["name"] for seg in hb["segments"]], ["blink_firmly-whole"])
        self.assertTrue(hb["review_required"])
        self.assertTrue(any(reason.startswith("simulated capture") for reason in hb["review_reasons"]), hb["review_reasons"])
        with open(os.path.join(out, "20260901-s2-blink_firmly-v_standard-sim.labels.json"), "r", encoding="utf-8") as handle:
            v1 = json.load(handle)
        self.assertTrue(v1["review_required"])
        # Promotion is refused for rehearsal fixtures even after a "review".
        v1["review_required"] = False
        v1["review_reasons"] = []
        v1["provenance"]["labeler"] = "owner"
        v1["provenance"]["cleared_on"] = "2026-09-01"
        with open(os.path.join(out, v1["name"] + ".labels.json"), "w", encoding="utf-8") as handle:
            handle.write(cc.json_dumps_stable(v1))
        with open(os.path.join(out, v1["name"] + ".labels.tsv"), "w", encoding="utf-8") as handle:
            handle.write(mtf.derive_tsv(v1))
        dest = os.path.join(self.tmp, "promoted-sim")
        code, stdout, stderr = run_cli(mtf.main, ["--promote", v1["name"], "--out", out, "--dest", dest])
        self.assertNotEqual(code, 0)
        self.assertIn("provenance.simulated is true", stderr)
        self.assertFalse(os.path.exists(dest))
        # A simulated source whose name carries no date must be told explicitly.
        undated = os.path.join(self.tmp, "undated-synthetic.log")
        shutil.copyfile(self.synthetic_log, undated)
        undated_dir = self._simulate(undated, plan=True, name="sim-undated")
        code, stdout, stderr = run_cli(mtf.main, [undated_dir, "--out", os.path.join(self.tmp, "fixtures-undated"), "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertIn("cannot derive capture date", stderr)
        self.assertIn("simulated capture", stderr)
        code, stdout, stderr = run_cli(mtf.main, [undated_dir, "--out", os.path.join(self.tmp, "fixtures-undated"), "--replay-bin", self.replay_bin, "--capture-date", "2026-08-01"])
        self.assertEqual(code, 0, stderr)
        self.assertTrue(all(n.startswith("20260801-") and "-sim." in n for n in os.listdir(os.path.join(self.tmp, "fixtures-undated"))))

    def test_promotion_refuses_review_required(self):
        capture_dir = self._live(self.synthetic_log, name="live-promote")
        out = os.path.join(self.tmp, "fixtures-promote")
        code, stdout, stderr = run_cli(mtf.main, [capture_dir, "--out", out, "--replay-bin", self.replay_bin])
        self.assertEqual(code, 0, stderr)
        hb_name = "20260901-s3-blink_firmly-hb_hard_singles"
        dest = os.path.join(self.tmp, "promoted")
        code, stdout, stderr = run_cli(mtf.main, ["--promote", hb_name, "--out", out, "--dest", dest])
        self.assertNotEqual(code, 0)
        self.assertIn("review_required is true", stderr)
        self.assertFalse(os.path.exists(os.path.join(dest, hb_name + ".csv")))
        # A clean fixture still refuses while the labeler is "agent" and cleared_on is null.
        v1_name = "20260901-s2-blink_firmly-v_standard"
        code, stdout, stderr = run_cli(mtf.main, ["--promote", v1_name, "--out", out, "--dest", dest])
        self.assertNotEqual(code, 0)
        self.assertNotIn("review_required is true", stderr)
        self.assertNotIn("simulated", stderr)
        self.assertIn("labeler is still 'agent'", stderr)
        self.assertIn("cleared_on is null", stderr)

    def test_deid_guard_refuses_cli_output(self):
        # A synthetic live-shaped capture whose cue tag carries the device hostname
        # must be refused before anything is written.
        capture_dir = os.path.join(self.tmp, "capture-20260901T000000-red")
        os.makedirs(capture_dir, exist_ok=True)
        shutil.copyfile(self.synthetic_log, os.path.join(capture_dir, "raw.log"))
        session = {
            "schema": "colibrino-v2-capture-session/1",
            "mode": "live",
            "plan": {"path": os.path.relpath(PLAN_PATH, cc.repo_root()), "session_id": "A"},
            "runs": [
                {"run_id": "R0", "run_kind": "R0_bench", "probe": True, "mount": "bench", "commit_class": "public", "status": "valid", "attempts": [{"attempt": 1, "session_index": 1, "status": "valid"}]},
                {"run_id": "V1", "run_kind": "V_standard", "probe": True, "mount": "glasses_right_temple", "commit_class": "public", "status": "valid", "attempts": [{"attempt": 1, "session_index": 2, "status": "valid"}]},
                {"run_id": "HB1", "run_kind": "HB_hard_singles", "probe": True, "mount": "glasses_right_temple", "commit_class": "public", "status": "valid", "attempts": [{"attempt": 1, "session_index": 3, "status": "valid"}]},
            ],
        }
        with open(os.path.join(capture_dir, "session.json"), "w", encoding="utf-8") as handle:
            json.dump(session, handle)
        with open(os.path.join(capture_dir, "markers.jsonl"), "w", encoding="utf-8") as handle:
            handle.write(json.dumps({"type": "cue", "run_id": "HB1", "stage": "BLINK_FIRMLY", "session_index": 3, "t_offset_ms": 750.0, "slip_ms": 0.0, "sound": "beep", "tag": "sticks3-ptt", "group": None}) + "\n")
        out = os.path.join(self.tmp, "fixtures-red")
        code, stdout, stderr = run_cli(mtf.main, [capture_dir, "--out", out, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertIn("de-identification guard refused", stderr)
        self.assertIn("device_hostname", stderr)
        self.assertNotIn("sticks3-ptt", stdout)
        self.assertFalse(os.path.exists(out) and os.listdir(out), "nothing may be written when the guard fires")

    def test_accept_runs_applies_plan_rule_and_only_counts_designated_runs(self):
        live_a = self._live(self.synthetic_log_v2, name="live-accept-a", stamp="20260902T000000")
        # V1 (s2) and V2 (s5) both PASS with clean controls: the plan's designated pair => ACCEPT.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1,V2", "--session", live_a, "--replay-bin", self.replay_bin])
        self.assertEqual(code, 0, stdout + stderr)
        self.assertIn("verdict=ACCEPT", stdout)
        self.assertIn("plan=round1", stdout)
        self.assertIn('rule="acceptance.designated_runs: one of V1+V2 | V2+V3"', stdout)
        self.assertRegex(stdout, r"V1\t.*\t0/2/0\tPASS\tPASS")
        self.assertRegex(stdout, r"V2\t.*\t5\t.*\t0/2/0\tPASS\tPASS")
        # Order inside the designated set does not matter.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V2,V1", "--session", live_a, "--replay-bin", self.replay_bin])
        self.assertEqual(code, 0, stdout)
        self.assertIn("verdict=ACCEPT", stdout)
        # A single V run is not a designated set, even though it passes.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1", "--session", live_a, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"V1\t.*\tPASS\tPASS")
        self.assertIn("RULE: designated set V1 is not one of the plan's acceptance.designated_runs", stdout)
        self.assertIn("rule_ok=no runs_ok=yes verdict=REJECT", stdout)
        # Designating a run that is not PASS (HB1: no coded sequences) => REJECT even though V1 passes.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1,HB1", "--session", live_a, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertIn("verdict=REJECT", stdout)
        self.assertRegex(stdout, r"HB1\t.*\tFAIL")
        self.assertRegex(stdout, r"V1\t.*\tPASS")
        # Designating a run with a control violation => REJECT (and it is not a designated set either).
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "U", "--session", live_a, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"U\t.*\t1/2/0\tNOT_PROVEN\tFAIL")
        # A designated run unknown to the capture => MISSING => REJECT.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V2,V3", "--session", live_a, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"V3\t.*\tMISSING")
        self.assertIn("rule_ok=yes runs_ok=no verdict=REJECT", stdout)
        # Session A (4 sessions: V2 never ran) => NO_VALID_ATTEMPT => REJECT.
        live_a4 = self._live(self.synthetic_log, name="live-accept-a4", stamp="20260901T000000")
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1,V2", "--session", live_a4, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"V2\t.*\tNO_VALID_ATTEMPT")
        self.assertIn("verdict=REJECT", stdout)
        # Cross-session pair V2 (A) + V3 (B): V3 lands on the still-violation session under plan B => REJECT.
        live_b = self._live(self.synthetic_log, name="live-accept-b", plan_session="B", stamp="20260903T000000")
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V2,V3", "--session", live_a, "--session", live_b, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"V2\tcapture-20260902T000000\t5\t.*\tPASS")
        self.assertRegex(stdout, r"V3\tcapture-20260903T000000\t4\t.*\t1/2/0\tNOT_PROVEN\tFAIL")
        self.assertIn("rule_ok=yes runs_ok=no verdict=REJECT", stdout)
        # The finding's reproduction: under plan B session 2 is SB (a negative control that
        # expects 0/0/0). Its data clicks, the run row says PASS, but SB is not designated.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "SB", "--session", live_b, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"SB\t.*\t0/2/0\tPASS\tPASS")
        self.assertIn("RULE: designated set SB is not one of the plan's acceptance.designated_runs", stdout)
        self.assertIn("verdict=REJECT", stdout)
        # Simulated captures can never produce ACCEPT: rehearsal only.
        sim_a = self._simulate(self.synthetic_log_v2, plan=True, name="sim-accept-plan")
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1,V2", "--session", sim_a, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertIn("SIMULATED capture (rehearsal)", stdout)
        self.assertIn("rule_ok=yes runs_ok=yes verdict=REHEARSAL", stdout)
        self.assertNotIn("verdict=ACCEPT", stdout)
        # Without a plan there is no acceptance rule: REJECT with the reason, table still printed.
        sim_dir = self._simulate(self.synthetic_log, plan=False, name="sim-accept-noplan")
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "SIM-2", "--session", sim_dir, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"SIM-2\t.*\t0/2/0\tPASS\tPASS")
        self.assertIn("RULE: ", stdout)
        self.assertIn("no capture plan", stdout)
        self.assertIn("plan=none", stdout)
        self.assertIn("verdict=REJECT", stdout)


class TestRetainedLogFixtures(CaptureToolsBase):
    def test_retained_five_session_log_controls_clean_and_parity_reported(self):
        """The retained 5-session log was recorded by an OLDER detector build.

        Its firmware RESULT lines therefore do not match the current host
        replay (sessions 1, 3, 4 and 5 differ; session 2 matches). The tool
        must report that mismatch clearly and mark the fixtures for review,
        not silently claim equality.
        """
        path = RETAINED_LOGS["260815-151549"]
        if not os.path.isfile(path):
            self.skipTest("retained device log is not present on this machine")
        out = os.path.join(self.tmp, "fixtures-retained-5")
        code, stdout, stderr = run_cli(mtf.main, [path, "--out", out, "--replay-bin", self.replay_bin, "--mount", "glasses_right_temple", "--run-kind", "V_standard"])
        self.assertEqual(code, 0, stderr)
        summary = next(l for l in stdout.splitlines() if l.startswith("SUMMARY "))
        self.assertIn("sessions=5", summary)
        self.assertIn("controls_clean=yes", summary)
        self.assertIn("parity_matches=1", summary)
        self.assertIn("parity_mismatches=4", summary)
        parity_lines = [l for l in stdout.splitlines() if l.startswith("PARITY ")]
        self.assertEqual(len(parity_lines), 5)
        self.assertEqual(sum(1 for l in parity_lines if l.endswith("MISMATCH")), 4)
        self.assertTrue(any(l.startswith("PARITY s2 ") and l.endswith(" MATCH") for l in parity_lines))
        self.assertIn("REVIEW REQUIRED", stdout)
        self.assertEqual(len(os.listdir(out)), 45)
        with open(os.path.join(out, "20260815-s1-keep_head_still-v_standard.labels.json"), "r", encoding="utf-8") as handle:
            labels = json.load(handle)
        self.assertEqual(labels["replay"]["parity"], "mismatch")
        self.assertTrue(labels["review_required"])
        self.assertTrue(labels["replay"]["session_controls_clean"])
        # CSV values are the device strings verbatim.
        with open(os.path.join(out, labels["csv"]), "r", encoding="utf-8") as handle:
            handle.readline()
            first = handle.readline().rstrip("\n")
        self.assertEqual(first, "634132,634133103,-0.4883,-0.1831,0.2441,-0.05957,0.99292,-0.13013")
        for name in os.listdir(out):
            with open(os.path.join(out, name), "r", encoding="utf-8") as handle:
                self.assertEqual(cc.deid_scan(handle.read()), [], name)

    def test_retained_single_session_logs(self):
        for key in ("260814-232730", "260814-234754"):
            path = RETAINED_LOGS[key]
            if not os.path.isfile(path):
                continue
            out = os.path.join(self.tmp, f"fixtures-retained-{key}")
            code, stdout, stderr = run_cli(mtf.main, [path, "--out", out, "--replay-bin", self.replay_bin])
            self.assertEqual(code, 0, stderr)
            summary = next(l for l in stdout.splitlines() if l.startswith("SUMMARY "))
            self.assertIn("sessions=1", summary)
            self.assertIn("controls_clean=yes", summary)
            self.assertEqual(len(os.listdir(out)), 9)
            if key == "260814-232730":
                self.assertIn("parity_unavailable=1", summary)  # that log has no RESULT line
            else:
                self.assertIn("parity_mismatches=1", summary)  # older detector build


class TestStatusAndTelemetryCompat(CaptureToolsBase):
    def test_status_new_fields_and_result_free_suffix(self):
        # New STATUS tail: ,build=<sha>,batt=<pct>,tele=<0|1> (batt may be -1).
        parsed = cc.parse_status(_status(5000, build="e1b1c48", batt=-1, tele=1))
        assert parsed is not None
        self.assertEqual(parsed[0], 5000)
        self.assertEqual(parsed[1]["build"], "e1b1c48")
        self.assertEqual(parsed[1]["batt"], "-1")
        self.assertEqual(parsed[1]["tele"], "1")
        self.assertEqual(parsed[1]["ota"], "READY")
        # Old STATUS lines keep parsing exactly as before, without the keys.
        old = cc.parse_status(_status(5000))
        assert old is not None
        self.assertNotIn("build", old[1])
        self.assertNotIn("batt", old[1])
        self.assertNotIn("tele", old[1])
        self.assertEqual(old[1]["imu_stage"], "IDLE")
        # RESULT free suffix is exposed only when present.
        self.assertEqual(
            cc.parse_result("RESULT,IMU_BLINK,NOT_PROVEN,still=0,blink=0,head=0,free=2"),
            {"verdict": "NOT_PROVEN", "still": 0, "blink": 0, "head": 0, "free": 2},
        )
        self.assertEqual(
            cc.parse_result("RESULT,IMU_BLINK,PASS,still=0,blink=2,head=0"),
            {"verdict": "PASS", "still": 0, "blink": 2, "head": 0},
        )

    def test_telemetry_dropped_event_increments_alarm_counter(self):
        log = os.path.join(self.tmp, "telemetry-events.log")
        lines = [_status(1000 + 1000 * k, build="e1b1c48") for k in range(3)]
        lines.append("EVENT,TELEMETRY,CONNECTED,e1b1c48")
        lines.append("EVENT,TELEMETRY,DROPPED,3")
        lines.append(_status(5000, build="e1b1c48"))
        lines.append("EVENT,TELEMETRY,DROPPED,3")  # unchanged total: no new alarm
        lines.append("EVENT,TELEMETRY,DROPPED,7")  # increase: second alarm
        with open(log, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines) + "\n")
        capture_dir = self._simulate(log, plan=False, name="sim-telemetry")
        with open(os.path.join(capture_dir, "session.json"), "r", encoding="utf-8") as handle:
            session = json.load(handle)
        dropped = [a for a in session["alarms"] if a["key"] == "telemetry_dropped"]
        self.assertEqual(len(dropped), 2, session["alarms"])
        self.assertEqual(session["telemetry"], {"greetings": 1, "connected_build": "e1b1c48", "dropped_total": 7})
        self.assertEqual(session["status_build"], "e1b1c48")


class TestFreeRunTooling(CaptureToolsBase):
    def test_free_run_log_is_one_session_with_free_block(self):
        sessions = cc.split_sessions(cc.read_lines(self.free_run_log))
        self.assertEqual(len(sessions), 1)
        session = sessions[0]
        self.assertTrue(session.explicit_start)
        self.assertIn(cc.FREE_RUN_STAGE, session.stages)
        block = session.stages[cc.FREE_RUN_STAGE]
        self.assertGreater(len(block.rows), 500)
        self.assertIsNotNone(block.event_line)
        for stage in cc.CAPTURE_STAGES:
            guided = session.stages.get(stage)
            self.assertTrue(guided is None or not guided.rows, stage)
        assert session.result is not None
        self.assertEqual(session.result["verdict"], "NOT_PROVEN")
        self.assertEqual(session.result["free"], 2)

    def test_free_run_fixture_review_required_and_replay_parity(self):
        # The compiled replay tool skips the unknown stage: one session,
        # clean controls, NOT_PROVEN.
        replay = cc.run_replay(self.replay_bin, self.free_run_log)
        self.assertEqual(replay["summary"]["sessions"], 1)  # type: ignore[index]
        self.assertTrue(replay["summary"]["controls_clean"])  # type: ignore[index]
        self.assertEqual(replay["sessions"][0]["result"], "NOT_PROVEN")  # type: ignore[index]
        out = os.path.join(self.tmp, "fixtures-freerun")
        code, stdout, stderr = run_cli(mtf.main, [self.free_run_log, "--out", out, "--replay-bin", self.replay_bin])
        self.assertEqual(code, 0, stderr + stdout)
        name = "20260903-s1-capture_free_run-v_standard"
        self.assertEqual(sorted(os.listdir(out)), [name + ".csv", name + ".labels.json", name + ".labels.tsv"])
        with open(os.path.join(out, name + ".labels.json"), "r", encoding="utf-8") as handle:
            labels = json.load(handle)
        self.assertEqual(labels["capture"]["stage"], "CAPTURE_FREE_RUN")
        self.assertEqual(labels["capture"]["run_kind"], "V_standard")  # CLI default; plans map it per run
        self.assertTrue(labels["review_required"])
        self.assertTrue(
            any("labels.schema.json" in reason and "CAPTURE_FREE_RUN" in reason for reason in labels["review_reasons"]),
            labels["review_reasons"],
        )
        # Unguided stage: nothing can be asserted automatically.
        self.assertEqual(labels["segments"], [])
        # STATUS build= is the default firmware_commit provenance.
        self.assertEqual(labels["capture"]["firmware_commit"], "e1b1c48")
        with open(os.path.join(out, name + ".labels.tsv"), "r", encoding="utf-8") as handle:
            self.assertEqual(handle.read(), mtf.TSV_HEADER + "\n")
        self.assertIn("REVIEW REQUIRED", stdout)
        # Promotion stays blocked while review_required is true.
        code, stdout, stderr = run_cli(mtf.main, ["--promote", name, "--out", out, "--dest", os.path.join(self.tmp, "promoted-freerun")])
        self.assertNotEqual(code, 0)
        self.assertIn("review_required is true", stderr)

    def test_accept_runs_rejects_free_run_only_session(self):
        # Plan A maps session 1 -> R0 and session 2 -> V1; session 2 here is
        # free-run-only, so designating V1 must never yield ACCEPT.
        live = self._live(self.free_run_plan_log, name="live-accept-freerun", stamp="20260904T000000")
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1,V2", "--session", live, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"V1\t.*\tFREE_RUN")
        self.assertIn("verdict=REJECT", stdout)
        self.assertNotIn("verdict=ACCEPT", stdout)


class TestTcpTransport(CaptureToolsBase):
    """[TEST] Loopback-only TCP adapter checks; no real device or network."""

    def _tcp_capture(self, name, server, extra_argv=(), give_up="1.5"):
        out_root = os.path.join(self.tmp, name)
        argv = [
            "--tcp", f"127.0.0.1:{server.port}", "--tcp-give-up", give_up,
            "--no-audio", "--no-keys", "--quiet", "--out-dir", out_root,
        ] + list(extra_argv)
        code, out, err = run_cli(capture_session.main, argv)
        return code, out, err, out_root

    @staticmethod
    def _capture_dir(out_root):
        dirs = [d for d in os.listdir(out_root) if d.startswith("capture-")]
        assert len(dirs) == 1, dirs
        return os.path.join(out_root, dirs[0])

    @staticmethod
    def _session(capture_dir):
        with open(os.path.join(capture_dir, "session.json"), "r", encoding="utf-8") as handle:
            return json.load(handle)

    @staticmethod
    def _run_structure(session):
        """Run/session structure only - no host timestamps."""
        return [
            (
                run["run_id"],
                run["run_kind"],
                run["status"],
                [(a.get("session_index"), a.get("result")) for a in run["attempts"]],
            )
            for run in session["runs"]
        ]

    def test_tcp_greeting_stream_parses_like_simulate_and_sends_nothing(self):
        sim = self._session(self._simulate(self.synthetic_log, plan=True, name="tcp-ref-sim"))
        with open(self.synthetic_log, "rb") as handle:
            log_bytes = handle.read()
        server = TelemetryStubServer([{"chunks": [log_bytes]}])
        server.start()
        code, out, err, out_root = self._tcp_capture("tcp-stream", server, extra_argv=["--plan", PLAN_PATH])
        server.join(timeout=10)
        self.assertEqual(code, 0, err + out)
        self.assertEqual(server.errors, [])
        # SHUT_WR proof: the server's recv saw only EOF, never a byte.
        self.assertEqual(server.received, [[b""]])
        tcp_dir = self._capture_dir(out_root)
        tcp = self._session(tcp_dir)
        self.assertEqual(tcp["mode"], "tcp")
        self.assertEqual(
            tcp["identity"],
            {"transport": "tcp", "mac_last4": None, "port": server.port, "host_is_loopback": True},
        )
        self.assertEqual(tcp["telemetry"]["greetings"], 1)
        self.assertEqual(tcp["telemetry"]["connected_build"], "e1b1c48")
        # Identical parsing to --simulate of the same log (structure, not
        # host timestamps; the tcp stream additionally carries the greeting).
        self.assertEqual(tcp["probe_sessions"], sim["probe_sessions"])
        self.assertEqual(tcp["status_count"], sim["status_count"])
        self.assertEqual(tcp["boot"]["boot_ok"], sim["boot"]["boot_ok"])
        self.assertEqual(self._run_structure(tcp), self._run_structure(sim))
        with open(os.path.join(tcp_dir, "raw.log"), "rb") as handle:
            self.assertEqual(handle.read(), b"EVENT,TELEMETRY,CONNECTED,e1b1c48\n" + log_bytes)

    def test_tcp_mid_stream_close_reconnects_and_continues(self):
        sim = self._session(self._simulate(self.synthetic_log, plan=True, name="tcp-ref-sim-b"))
        with open(self.synthetic_log, "rb") as handle:
            log_bytes = handle.read()
        mid = log_bytes.index(b"\n", len(log_bytes) // 2) + 1
        server = TelemetryStubServer([{"chunks": [log_bytes[:mid]]}, {"chunks": [log_bytes[mid:]]}])
        server.start()
        code, out, err, out_root = self._tcp_capture("tcp-reconnect", server, extra_argv=["--plan", PLAN_PATH], give_up="2.0")
        server.join(timeout=15)
        self.assertEqual(code, 0, err + out)
        self.assertEqual(server.errors, [])
        self.assertEqual(server.received, [[b""], [b""]])  # nothing sent on either connection
        tcp_dir = self._capture_dir(out_root)
        tcp = self._session(tcp_dir)
        self.assertEqual(tcp["disconnects"], 2)  # mid-stream close + final close
        self.assertEqual(tcp["telemetry"]["greetings"], 2)
        with open(os.path.join(tcp_dir, "markers.jsonl"), "r", encoding="utf-8") as handle:
            marker_types = [json.loads(l)["type"] for l in handle if l.strip()]
        self.assertIn("disconnect", marker_types)
        self.assertIn("reconnect", marker_types)
        # The capture continued across the reconnect: same parsed structure.
        self.assertEqual(tcp["probe_sessions"], sim["probe_sessions"])
        self.assertEqual(self._run_structure(tcp), self._run_structure(sim))
        greeting = b"EVENT,TELEMETRY,CONNECTED,e1b1c48\n"
        with open(os.path.join(tcp_dir, "raw.log"), "rb") as handle:
            self.assertEqual(handle.read(), greeting + log_bytes[:mid] + greeting + log_bytes[mid:])

    def test_tcp_loopback_shaped_hostname_is_not_exempt(self):
        # [TEST] '127.evil.example' is a DNS name, not a loopback address:
        # it must never take the loopback exemption (which would let DNS
        # resolve it to an arbitrary remote host with no MAC identity check).
        self.assertFalse(capture_session._tcp_is_loopback("127.evil.example"))
        self.assertFalse(capture_session._tcp_is_loopback("127.1.example.com"))
        self.assertTrue(capture_session._tcp_is_loopback("localhost"))
        self.assertTrue(capture_session._tcp_is_loopback("127.0.0.1"))
        self.assertTrue(capture_session._tcp_is_loopback("127.1.2.3"))
        self.assertTrue(capture_session._tcp_is_loopback("::1"))
        # Full identity lane: with no expected MAC in .env the guard refuses
        # BEFORE any subprocess (no ping, no ARP) and no socket is opened.
        env_path = os.path.join(self.tmp, "empty.env")
        with open(env_path, "w", encoding="utf-8"):
            pass
        calls = []
        real_run = capture_session.subprocess.run

        def forbidden_run(*args, **kwargs):
            calls.append(args)
            raise AssertionError("ping/arp must not run in this test")

        capture_session.subprocess.run = forbidden_run
        try:
            with self.assertRaises(capture_session.TcpGuardError) as ctx:
                capture_session.resolve_tcp_host("127.evil.example", env_path)
            code, out, err = run_cli(
                capture_session.main,
                ["--tcp", "127.evil.example:1", "--env", env_path,
                 "--no-audio", "--no-keys", "--quiet",
                 "--out-dir", os.path.join(self.tmp, "tcp-evil")],
            )
        finally:
            capture_session.subprocess.run = real_run
        self.assertEqual(calls, [])
        self.assertIn(cc.ENV_OTA_MAC_KEY, str(ctx.exception))
        self.assertNotEqual(code, 0)
        self.assertIn("refused", err)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "tcp-evil")))

    def test_tcp_aborts_without_greeting(self):
        # A service that talks, but not the greeting, first.
        server = TelemetryStubServer([{"bad_first_line": True}])
        server.start()
        code, out, err, out_root = self._tcp_capture("tcp-no-greeting", server)
        server.join(timeout=10)
        self.assertNotEqual(code, 0)
        self.assertIn("greeting", err)
        self.assertFalse(os.path.exists(out_root), "no capture directory may be created without the greeting")
        self.assertEqual(server.received, [[b""]])
        # A service that closes without saying anything.
        server = TelemetryStubServer([{"greeting": False}])
        server.start()
        code, out, err, out_root = self._tcp_capture("tcp-silent", server)
        server.join(timeout=10)
        self.assertNotEqual(code, 0)
        self.assertIn("greeting", err)
        self.assertFalse(os.path.exists(out_root))


if __name__ == "__main__":
    unittest.main()
