"""Actual CLI completion contracts, using generated PCM only.

Run: python3 tests/test_outcomes.py build/chirpmotion-native [BASELINE_BINARY]
The optional baseline additionally requires byte-identical CSV/JSON outputs.
"""
import collections
import csv
import hashlib
import math
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest

BINARY = str(Path(sys.argv[1]).resolve())
BASELINE = str(Path(sys.argv[2]).resolve()) if len(sys.argv) == 3 else None
sys.argv = [sys.argv[0], BINARY]
fixture = runpy.run_path(str(Path(__file__).with_name("test_quality.py")))["fixture"]


def down(kind):
    def chirp(j, beat):
        t = j / 48000 - beat / 300000
        return math.cos(2 * math.pi * (23000 * t - 150000 * t * t)) * (
            0.5 - 0.5 * math.cos(2 * math.pi * j / 960)
        )
    values = {
        "single": lambda j: 12000 * chirp(j, 300),
        "two": lambda j: 6000 * (chirp(j, 300) + chirp(j, 850)),
        "silence": lambda j: 0,
    }
    return b"".join(struct.pack("<h", round(values[kind](j))) for j in range(961))


class Outcomes(unittest.TestCase):
    def run_case(self, root, name, raw, *, mode="up", binary=BINARY, extra=(), start=True):
        source = root / (name + ".pcm")
        source.write_bytes(raw)
        before = hashlib.sha256(raw).digest()
        destination = root / name
        args = [binary, "--input", str(source), "--output", str(destination),
                "--mode", mode, "--spectra-csv"]
        if start:
            args += ["--start-sample", "1"]
        result = subprocess.run(args + list(extra), capture_output=True, text=True)
        self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)
        return result, destination

    def check_complete(self, result, out, cycles, expected=None):
        self.assertEqual(result.returncode, 0, result.stderr)
        with (out / "cycles.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        counts = collections.Counter(row["quality"] for row in rows)
        if expected is not None:
            self.assertEqual(counts, expected)
        self.assertIn(f"Complete: {cycles} cycle{'' if cycles == 1 else 's'}, {len(rows)} sweep{'' if len(rows) == 1 else 's'}.\n", result.stdout)
        self.assertIn(f"Quality: {counts['usable']} usable, {counts['ambiguous']} ambiguous, "
                      f"{counts['no_signal']} no_signal.\n", result.stdout)
        withheld = counts["ambiguous"] + counts["no_signal"]
        if not counts["usable"]:
            self.assertIn("All derived delays and paths withheld; output files are complete.\n", result.stdout)
        else:
            self.assertIn(f"Derived delays and paths: {counts['usable']} available, {withheld} withheld.\n", result.stdout)
        for row in rows:
            if row["quality"] != "usable":
                self.assertEqual((row["relative_delay_seconds"], row["relative_path_meters"]), ("null", "null"))
        self.assertTrue((out / "summary.json").exists())
        return rows

    def test_single_and_all_withheld_completed_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for kind, quality in (("single", "usable"), ("two", "ambiguous"), ("silence", "no_signal")):
                result, out = self.run_case(root, kind, fixture(kind))
                self.check_complete(result, out, 1, {quality: 1})

    def test_mixed_multiple_cycles_and_sweep_directions(self):
        kinds = ("single", "two", "silence") * 2
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result, out = self.run_case(root, "up", b"".join(fixture(k) for k in kinds))
            rows = self.check_complete(result, out, 6, {"usable": 2, "ambiguous": 2, "no_signal": 2})
            self.assertEqual([(int(r["cycle"]), int(r["sweep"])) for r in rows], [(c, 0) for c in range(6)])
            raw = b"".join(fixture(k) + down(k) for k in kinds)
            result, out = self.run_case(root, "alternating", raw, mode="alternating")
            rows = self.check_complete(result, out, 6, {"usable": 4, "ambiguous": 4, "no_signal": 4})
            self.assertEqual([(int(r["cycle"]), int(r["sweep"])) for r in rows], [(c, s) for c in range(6) for s in range(2)])

    def test_failures_never_print_completion_and_collision_preserves_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, raw, extra, start in (
                ("alignment", fixture("silence"), (), False),
                ("limit", fixture("single"), ("--max-output-bytes", "1"), True),
                ("short", fixture("single")[:-2], (), True),
            ):
                result, out = self.run_case(root, name, raw, extra=extra, start=start)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertNotIn("Complete:", result.stdout)
                self.assertNotIn("Quality:", result.stdout)
                self.assertFalse((out / "summary.json").exists())
                if name == "limit":
                    self.assertTrue((out / "INCOMPLETE.txt").exists())
            result, out = self.run_case(root, "preserved", fixture("single"))
            self.check_complete(result, out, 1)
            before = {p.name: p.read_bytes() for p in out.iterdir()}
            result, _ = self.run_case(root, "preserved", fixture("two"))
            self.assertEqual(result.returncode, 1)
            self.assertNotIn("Complete:", result.stdout)
            self.assertEqual({p.name: p.read_bytes() for p in out.iterdir()}, before)

    @unittest.skipUnless(BASELINE, "pass an original binary for serialized-output preservation")
    def test_serialized_outputs_match_original_binary_exactly(self):
        inputs = [(k, fixture(k), "up") for k in ("single", "offbin", "two", "noise", "silence", "outband", "low", "high")]
        kinds = ("single", "two", "silence") * 2
        inputs += [("mixed-up", b"".join(fixture(k) for k in kinds), "up"),
                   ("mixed-alternating", b"".join(fixture(k) + down(k) for k in kinds), "alternating")]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, raw, mode in inputs:
                with self.subTest(name=name):
                    current, out = self.run_case(root, name, raw, mode=mode)
                    prior, old = self.run_case(root, name + "-baseline", raw, mode=mode, binary=BASELINE)
                    self.assertEqual((current.returncode, prior.returncode), (0, 0))
                    for filename in ("cycles.csv", "spectra.csv", "summary.json"):
                        self.assertEqual((out / filename).read_bytes(), (old / filename).read_bytes(), filename)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
