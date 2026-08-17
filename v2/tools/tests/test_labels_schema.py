#!/usr/bin/env python3
"""Pins the contract between the Python fixture tooling (sticks3/tools) and
the C golden runner (v2/test/support/golden_runner.c):

* every closed enum in v2/traces/labels.schema.json equals the tuple the
  tooling enforces in sticks3/tools/capture_common.py;
* the TSV header the tooling emits (make_trace_fixture.TSV_HEADER) is the
  header the golden runner requires (kTsvHeader);
* a fixture name produced by the tooling matches the schema's name pattern.

Run from anywhere: python3 v2/tools/tests/test_labels_schema.py
(registered as ctest `labels_schema_consistency`; skipped when python3 or the
sticks3 tooling is absent).
"""
import importlib
import json
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
V2 = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(V2, ".."))
TOOLS = os.path.join(REPO, "sticks3", "tools")
SCHEMA = os.path.join(V2, "traces", "labels.schema.json")
RUNNER = os.path.join(V2, "test", "support", "golden_runner.c")


def load_module(name):
    """Imports sticks3/tools/<name>.py as a normal module (dataclasses need
    the module registered in sys.modules before execution)."""
    path = os.path.join(TOOLS, name + ".py")
    if not os.path.isfile(path):
        return None
    if TOOLS not in sys.path:
        sys.path.insert(0, TOOLS)
    return importlib.import_module(name)


def c_string_literal(source, ident):
    """Concatenated C string literal assigned to `ident` (adjacent literals)."""
    m = re.search(ident + r"\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;", source)
    if not m:
        return None
    parts = re.findall(r"\"((?:[^\"\\]|\\.)*)\"", m.group(1))
    return "".join(parts).encode("utf-8").decode("unicode_escape")


class LabelsSchemaConsistency(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = load_module("capture_common")
        cls.mtf = load_module("make_trace_fixture")
        if cls.cc is None or cls.mtf is None:
            raise unittest.SkipTest("sticks3/tools not present in this checkout")
        with open(SCHEMA, encoding="utf-8") as fh:
            cls.schema = json.load(fh)
        with open(RUNNER, encoding="utf-8") as fh:
            cls.runner_src = fh.read()

    def enum(self, *path):
        node = self.schema
        for key in path:
            node = node[key]
        return tuple(node["enum"])

    def test_enums_match_tooling(self):
        props = ("properties",)
        cap = props + ("capture", "properties")
        self.assertEqual(self.enum(*cap, "run_kind"), tuple(self.cc.RUN_KINDS))
        self.assertEqual(self.enum(*cap, "mount"), tuple(self.cc.MOUNTS))
        self.assertEqual(self.enum(*cap, "stage"), tuple(self.cc.CAPTURE_STAGES))
        self.assertEqual(self.enum(*cap, "commit_class"), tuple(self.cc.COMMIT_CLASSES))
        self.assertEqual(
            self.enum(*props, "provenance", "properties", "labeler"), tuple(self.cc.LABELERS)
        )
        seg = props + ("segments", "items", "properties")
        self.assertEqual(self.enum(*seg, "kind"), tuple(self.cc.SEGMENT_KINDS))
        self.assertEqual(self.enum(*seg, "expect_source"), tuple(self.cc.EXPECT_SOURCES))
        expect_props = self.schema["properties"]["segments"]["items"]["properties"]["expect"][
            "properties"
        ]
        self.assertEqual(tuple(expect_props.keys()), tuple(self.cc.EXPECT_KINDS))
        ops = tuple(self.schema["$defs"]["count_expect"]["properties"].keys())
        self.assertEqual(ops, tuple(self.cc.EXPECT_OPS))

    def test_tsv_header_matches_golden_runner(self):
        header = c_string_literal(self.runner_src, r"kTsvHeader")
        self.assertIsNotNone(header, "kTsvHeader not found in golden_runner.c")
        self.assertEqual(header, self.mtf.TSV_HEADER)
        derived = self.mtf.derive_tsv(
            {
                "segments": [
                    {
                        "name": "seg-1",
                        "kind": "rest",
                        "t_start_ms": 10,
                        "t_end_ms": 20,
                        "expect": {"CLICK_CANDIDATE": {"eq": 0}, "IMPULSE": {"ge": 1}},
                        "tolerance_ms": 0,
                    }
                ]
            }
        )
        lines = derived.splitlines()
        self.assertEqual(lines[0], header)
        for row in lines[1:]:
            fields = row.split("\t")
            self.assertEqual(len(fields), 8, row)
            self.assertIn(fields[4], self.cc.EXPECT_KINDS)
            self.assertIn(fields[5], self.cc.EXPECT_OPS)
            self.assertTrue(fields[6].isdigit() and fields[7].isdigit(), row)

    def test_fixture_name_matches_schema_pattern(self):
        pattern = re.compile(self.schema["properties"]["name"]["pattern"])
        name = self.mtf.stage_fixture_name("20260815", 3, "BLINK_FIRMLY", "HB_hard_singles")
        self.assertRegex(name, pattern)
        name_sim = self.mtf.stage_fixture_name(
            "20260815", 3, "BLINK_FIRMLY", "HB_hard_singles", simulated=True
        )
        self.assertRegex(name_sim, pattern)

    def test_schema_id_matches_tooling(self):
        self.assertEqual(self.schema["properties"]["schema"]["const"], self.mtf.LABELS_SCHEMA)


if __name__ == "__main__":
    unittest.main(verbosity=2)
