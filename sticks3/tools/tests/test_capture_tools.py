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
import sys
import tempfile
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

STAGE_MS = {"KEEP_HEAD_STILL": 6000, "BLINK_FIRMLY": 15000, "MOVE_HEAD": 12000}
PREPARE_MS = 3000
IMPULSE_MS = 60
BASE = (0.3, -0.2, 0.1)


# --------------------------------------------------------------------------
# Synthetic device log generator
# --------------------------------------------------------------------------


def coded_pattern(start_ms: int):
    """Impulse start times for double, pause, double (500 / 1100 / 500 ms)."""
    return [start_ms, start_ms + 500, start_ms + 1600, start_ms + 2100]


def _status(ms: int, imu_stage: str = "IDLE", mode: str = "MOTION") -> str:
    return (
        f"STATUS,{ms},board=StickS3,mode={mode},armed=0,imu=1,calibrated=1,imu_blink=0,ir=0,ota=READY,"
        f"ir_stage=IDLE,imu_stage={imu_stage},gyro_x=0.061,gyro_y=-0.061,gyro_z=0.000"
    )


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


def build_synthetic_log(path: str):
    """Four probe sessions:

    s1 quiet everywhere                     (R0 slot under plan A)
    s2 two coded patterns in BLINK -> PASS  (V1 slot)
    s3 hard singles at cue+250 ms, cue 5 missing, cue 6 doubled (HB1 slot)
    s4 coded pattern in STILL + two in BLINK -> control violation (U slot)
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
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


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
        cls.synthetic_log = build_synthetic_log(os.path.join(cls.tmp, "synthetic-device-monitor.log"))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)


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


class TestSimulateAndFixtures(CaptureToolsBase):
    def _simulate(self, log_path: str, plan: bool, name: str) -> str:
        out_root = os.path.join(self.tmp, name)
        argv = ["--simulate", log_path, "--no-audio", "--no-keys", "--quiet", "--sim-speed", "0", "--out-dir", out_root]
        if plan:
            argv += ["--plan", PLAN_PATH, "--plan-session", "A"]
        code, out, err = run_cli(capture_session.main, argv)
        self.assertEqual(code, 0, err + out)
        dirs = [d for d in os.listdir(out_root) if d.startswith("capture-sim-")]
        self.assertEqual(len(dirs), 1)
        return os.path.join(out_root, dirs[0])

    def test_simulate_writes_capture_directory_and_runs(self):
        capture_dir = self._simulate(self.synthetic_log, plan=True, name="sim-plan")
        for fname in ("raw.log", "hosttime.tsv", "markers.jsonl", "session.json"):
            self.assertTrue(os.path.isfile(os.path.join(capture_dir, fname)), fname)
        with open(os.path.join(capture_dir, "raw.log"), "rb") as a, open(self.synthetic_log, "rb") as b:
            self.assertEqual(a.read(), b.read(), "raw.log must be the device bytes verbatim")
        with open(os.path.join(capture_dir, "session.json"), "r", encoding="utf-8") as handle:
            session = json.load(handle)
        self.assertEqual(session["mode"], "simulate")
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

    def test_simulate_without_plan_names_runs_sim_n(self):
        capture_dir = self._simulate(self.synthetic_log, plan=False, name="sim-noplan")
        with open(os.path.join(capture_dir, "session.json"), "r", encoding="utf-8") as handle:
            session = json.load(handle)
        self.assertEqual([r["run_id"] for r in session["runs"]], ["SIM-1", "SIM-2", "SIM-3", "SIM-4"])

    def test_fixture_determinism_and_tsv_derivation(self):
        capture_dir = self._simulate(self.synthetic_log, plan=True, name="sim-fixture")
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
            with open(os.path.join(out, name), "r", encoding="utf-8") as handle:
                labels = json.load(handle)
            self.assertEqual(labels["schema"], "colibrino-v2-trace-labels/1")
            self.assertIn(labels["capture"]["run_kind"], cc.RUN_KINDS)
            self.assertIn(labels["capture"]["mount"], cc.MOUNTS)
            self.assertIn(labels["provenance"]["labeler"], cc.LABELERS)
            self.assertTrue(labels["provenance"]["deidentified"])
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
        with open(os.path.join(out, "20260817-s3-blink_firmly-hb_hard_singles.labels.json".replace("20260817", labels["capture"]["capture_date"].replace("-", ""))), "r", encoding="utf-8") as handle:
            hb = json.load(handle)
        statuses = [r["refine_status"] for r in hb["cue_refinement"]]
        self.assertEqual(statuses[:4], ["ok"] * 4)
        self.assertEqual(statuses[4], "missed")
        self.assertEqual(statuses[5], "ambiguous")
        self.assertTrue(hb["review_required"])
        for r in hb["cue_refinement"][:4]:
            self.assertLessEqual(abs(r["peak_ms"] - (r["cue_ms"] + 250)), 15)
        self.assertIn("REVIEW REQUIRED", stdout)
        # V1 fixture: no cues, coded_pattern ge 2, replay parity match, no review needed.
        with open(os.path.join(out, hb["name"].replace("s3-blink_firmly-hb_hard_singles", "s2-blink_firmly-v_standard") + ".labels.json"), "r", encoding="utf-8") as handle:
            v1 = json.load(handle)
        self.assertEqual(v1["capture"]["run_id"], "V1")
        self.assertEqual(v1["replay"]["parity"], "match")
        self.assertFalse(v1["review_required"], v1["review_reasons"])
        self.assertEqual(v1["segments"][0]["expect"], {"CLICK_CANDIDATE": {"ge": 2}})

    def test_promotion_refuses_review_required(self):
        capture_dir = self._simulate(self.synthetic_log, plan=True, name="sim-promote")
        out = os.path.join(self.tmp, "fixtures-promote")
        code, stdout, stderr = run_cli(mtf.main, [capture_dir, "--out", out, "--replay-bin", self.replay_bin])
        self.assertEqual(code, 0, stderr)
        hb_name = next(n[:-len(".labels.json")] for n in os.listdir(out) if n.endswith("hb_hard_singles.labels.json") and "blink_firmly" in n)
        dest = os.path.join(self.tmp, "promoted")
        code, stdout, stderr = run_cli(mtf.main, ["--promote", hb_name, "--out", out, "--dest", dest])
        self.assertNotEqual(code, 0)
        self.assertIn("review_required is true", stderr)
        self.assertFalse(os.path.exists(os.path.join(dest, hb_name + ".csv")))
        # A clean fixture still refuses while the labeler is "agent" and cleared_on is null.
        v1_name = hb_name.replace("s3-blink_firmly-hb_hard_singles", "s2-blink_firmly-v_standard")
        code, stdout, stderr = run_cli(mtf.main, ["--promote", v1_name, "--out", out, "--dest", dest])
        self.assertNotEqual(code, 0)
        self.assertIn("labeler is still 'agent'", stderr)
        self.assertIn("cleared_on is null", stderr)

    def test_deid_guard_refuses_cli_output(self):
        # A synthetic capture whose cue tag carries the device hostname must be refused
        # before anything is written.
        capture_dir = os.path.join(self.tmp, "capture-sim-20260817T000000-red")
        os.makedirs(capture_dir, exist_ok=True)
        shutil.copyfile(self.synthetic_log, os.path.join(capture_dir, "raw.log"))
        session = {
            "schema": "colibrino-v2-capture-session/1",
            "mode": "simulate",
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

    def test_accept_runs_only_counts_designated_runs(self):
        plan_dir = self._simulate(self.synthetic_log, plan=True, name="sim-accept-plan")
        # V1 (session 2) passes; U (session 4) has a still-control violation but is not designated.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1", "--session", plan_dir, "--replay-bin", self.replay_bin])
        self.assertEqual(code, 0, stdout + stderr)
        self.assertIn("verdict=ACCEPT", stdout)
        # Designating a run that is not PASS (HB1: no coded sequences) => REJECT even though V1 passes.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1,HB1", "--session", plan_dir, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertIn("verdict=REJECT", stdout)
        self.assertRegex(stdout, r"HB1\t.*\tFAIL")
        self.assertRegex(stdout, r"V1\t.*\tPASS")
        # Designating a run with a control violation => REJECT.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "U", "--session", plan_dir, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"U\t.*\t1/2/0\tNOT_PROVEN\tFAIL")
        # A designated run without a valid attempt, or unknown to the capture => REJECT.
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "V1,V2,V9", "--session", plan_dir, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
        self.assertRegex(stdout, r"V2\t.*\tNO_VALID_ATTEMPT")
        self.assertRegex(stdout, r"V9\t.*\tMISSING")
        self.assertIn("verdict=REJECT", stdout)
        # Simulated capture without a plan: SIM-1..N naming.
        sim_dir = self._simulate(self.synthetic_log, plan=False, name="sim-accept-noplan")
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "SIM-2", "--session", sim_dir, "--replay-bin", self.replay_bin])
        self.assertEqual(code, 0, stdout)
        self.assertIn("verdict=ACCEPT", stdout)
        code, stdout, stderr = run_cli(mtf.main, ["--accept-runs", "SIM-2,SIM-4", "--session", sim_dir, "--replay-bin", self.replay_bin])
        self.assertNotEqual(code, 0)
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


if __name__ == "__main__":
    unittest.main()
