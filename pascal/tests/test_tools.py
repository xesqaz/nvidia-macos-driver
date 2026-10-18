# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
import hashlib
import json
import lzma
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from audit_probe import audit, archive_files
from index_trace import index_trace


class ToolTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def archive(self, mutate=None):
        stages = {x: {"status": "ok"} for x in ["pci", "gpu_raw", "gpu_drivers", "trace", "gpu3d"]}
        report = {"stages": stages, "gpus": [{"bdf": "0000:01:00.0", "vendor": "0x10de", "device": "0x1c81"}]}
        files = {"report.json": json.dumps(report).encode(), "gpu/0000:01:00.0/config.bin": bytes(64),
                 "gpu/0000:01:00.0/vbios.rom": b"fixture"}
        seal = {"files": {n: hashlib.sha256(b).hexdigest() for n, b in files.items()}}
        if mutate:
            mutate(files, seal)
        files["seal.json"] = json.dumps(seal).encode()
        path = self.root / "probe.zip"
        with zipfile.ZipFile(path, "w") as z:
            for n, b in files.items():
                z.writestr("fixture/" + n, b)
        return path

    def test_hashes_and_stage_gate(self):
        result = audit(self.archive())
        self.assertTrue(result["integrity"]["hashes_match"])
        self.assertTrue(result["can_begin_offline_analysis"])
        self.assertIsNone(result["complete_rom_verified"])

    def test_tampering_is_not_complete(self):
        def mutate(files, seal):
            files["gpu/0000:01:00.0/config.bin"] += b"changed"
        result = audit(self.archive(mutate))
        self.assertFalse(result["integrity"]["hashes_match"])
        self.assertFalse(result["can_begin_offline_analysis"])
        self.assertFalse(result["capture_complete"])

    def test_uuid_flag_never_prints_value(self):
        token = "GPU-11111111-2222-3333-4444-555555555555"
        def mutate(files, seal):
            files["example.txt"] = token.encode()
            seal["files"]["example.txt"] = hashlib.sha256(files["example.txt"]).hexdigest()
        result = audit(self.archive(mutate))
        self.assertEqual(result["privacy"]["gpu_uuid_present_in_files"], ["example.txt"])
        self.assertNotIn(token, json.dumps(result))

    def test_traversal_rejected(self):
        path = self.root / "bad.zip"
        with zipfile.ZipFile(path, "w") as z:
            z.writestr("fixture/../escape", b"x")
        with self.assertRaises(ValueError):
            archive_files(path)

    def test_successful_stages_do_not_hide_unknown_trace_records(self):
        def mutate(files, seal):
            r = json.loads(files["report.json"])
            r["gpus"][0]["bars"] = [{"index": 0, "start": "0xf6000000", "size": 0x1000000}]
            r["trace"] = {"runs": [{"name": "nvidia-example", "file": "trace/nvidia.xz", "kernel_overrun": "0"}]}
            files["report.json"] = json.dumps(r).encode()
            files["trace/nvidia.xz"] = lzma.compress(b"VERSION 20070824\nUNKNOWN 1.0 7 0xf6000000 ff 0x0 0\n")
            seal["files"] = {n: hashlib.sha256(b).hexdigest() for n, b in files.items()}
        result = audit(self.archive(mutate), with_traces=True)
        self.assertFalse(result["capture_complete"])
        self.assertIn("incomplete_trace_records:nvidia-example", result["gaps"])
        self.assertEqual(result["traces"][0]["index"]["counts"]["unknown_opcodes"], 1)

    def trace(self, data):
        path = self.root / "trace.xz"
        path.write_bytes(lzma.compress(data))
        return path

    def test_trace_range_width_and_overrun(self):
        path = self.trace(b"VERSION 20070824\nR 4 1.0 7 0xf6000000 0x137000a1 0x0 0\n"
                          b"W 4 1.1 7 0xf6000100 0x1 0x0 0\nR 4 1.2 7 0xf7000000 0x0 0x0 0\n")
        result = index_trace(path, 0xf6000000, 0x1000000)
        self.assertEqual(result["counts"]["bar0_R"], 1)
        self.assertEqual(result["counts"]["bar0_W"], 1)
        self.assertEqual(result["identity_reads"][0]["value"], "0x137000a1")
        self.assertTrue(result["complete_by_observed_metadata"])
        self.assertFalse(index_trace(path, 0xf6000000, 0x1000000, 5)["complete_by_observed_metadata"])
        capped = index_trace(path, 0xf6000000, 0x1000000, max_registers=1)
        self.assertFalse(capped["offset_statistics_complete"])
        self.assertEqual(capped["counts"]["bar0_W"], 1)

    def test_trace_damage_and_limits(self):
        path = self.trace(b"VERSION 20070824\nR 99 1.0 7 0xf6000000 0x1 0x0 0\n")
        self.assertFalse(index_trace(path, 0xf6000000, 0x1000000)["complete_by_observed_metadata"])
        with self.assertRaises(ValueError):
            index_trace(path, 0xf6000000, 0x1000000, max_bytes=5)
        path.write_bytes(path.read_bytes()[:-3])
        with self.assertRaises((ValueError, lzma.LZMAError)):
            index_trace(path, 0xf6000000, 0x1000000)

    def test_concatenated_xz(self):
        path = self.root / "concat.xz"
        path.write_bytes(lzma.compress(b"VERSION 20070824\n") + lzma.compress(b"MARK 1.0 end\n"))
        self.assertEqual(index_trace(path, 0xf6000000, 0x1000000)["counts"]["lines"], 2)


if __name__ == "__main__":
    unittest.main()
