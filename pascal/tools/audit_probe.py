#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Audit a private Probe archive without trusting the scanner's 'all ok' summary.

Never uploads, mounts a drive, executes an archive member or prints identifier values.
The optional inspector is our locally built gp107-inspect executable, not an archive file.
"""
import argparse
import hashlib
import json
import lzma
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tempfile
import zipfile

MAX_FILES = 4096
MAX_BYTES = 256 << 20
GPU_UUID = re.compile(rb"GPU-[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}")


def archive_files(path):
    with zipfile.ZipFile(path) as z:
        infos = z.infolist()
        if len(infos) > MAX_FILES or sum(i.file_size for i in infos) > MAX_BYTES:
            raise ValueError("archive exceeds resource limits")
        names, roots = set(), set()
        for i in infos:
            p = PurePosixPath(i.filename)
            if (p.is_absolute() or ".." in p.parts or "\\" in i.filename or
                    not p.parts or stat.S_ISLNK(i.external_attr >> 16)):
                raise ValueError("unsafe archive member")
            if i.filename in names:
                raise ValueError("duplicate archive member")
            names.add(i.filename)
            if not i.is_dir():
                if len(p.parts) < 2:
                    raise ValueError("expected a single report directory")
                roots.add(p.parts[0])
        if len(roots) != 1:
            raise ValueError("expected exactly one report root")
        prefix = next(iter(roots)) + "/"
        # read() verifies each member's ZIP CRC; no extraction is needed.
        return {i.filename[len(prefix):]: z.read(i) for i in infos if not i.is_dir()}


def inspect_roms(files, gpu, inspector):
    base = "gpu/" + gpu["bdf"] + "/"
    config = files.get(base + "config.bin")
    raw_boot0 = int(gpu.get("nv", {}).get("pmc_boot_0", "0"), 0)
    results = []
    with tempfile.TemporaryDirectory(prefix="gp107-audit-") as d:
        cfg = Path(d) / "config.bin"
        cfg.write_bytes(config or b"")
        for name, data in files.items():
            if not name.startswith("gpu/") or not name.endswith(".rom"):
                continue
            item = {"file": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            if inspector and config:
                rom = Path(d) / "vbios.rom"
                rom.write_bytes(data)
                p = subprocess.run([str(inspector), str(cfg), str(rom), hex(raw_boot0)],
                                   capture_output=True, text=True, timeout=15)
                if p.returncode not in (0, 2):
                    raise ValueError("local inspector failed")
                item["inspection"] = json.loads(p.stdout)
            results.append(item)
    return results


def audit(path, inspector=None, with_traces=False):
    files = archive_files(path)
    report = json.loads(files["report.json"])
    seal = json.loads(files["seal.json"])
    expected = seal["files"]
    if not isinstance(expected, dict):
        raise ValueError("invalid seal file table")
    bad = sorted(n for n, want in expected.items()
                 if n not in files or hashlib.sha256(files[n]).hexdigest() != want)
    extras = sorted(set(files) - set(expected) - {"seal.json"})
    integrity_ok = not bad and not extras
    failed = {k: v for k, v in report.get("stages", {}).items() if v.get("status") != "ok"}
    gpu = next((g for g in report.get("gpus", [])
                if int(g.get("vendor", "0"), 0) == 0x10de and int(g.get("device", "0"), 0) == 0x1c81), None)
    gaps, traces = [], []
    if not report.get("trace", {}).get("runs"):
        gaps.append("trace_runs_absent")
    if not report.get("gpu3d", {}).get("runs"):
        gaps.append("gpu_tests_absent")
    for t in report.get("trace", {}).get("runs", []):
        dropped = int(t.get("kernel_overrun", "0") or 0)
        item = {k: t.get(k) for k in ("name", "file", "lines", "raw_bytes", "compressed_bytes")}
        item["reported_overrun"] = dropped
        item["complete_by_metadata"] = not (dropped or t.get("cut_for_budget") or t.get("kept_raw_cap"))
        if not item["complete_by_metadata"]:
            gaps.append("incomplete_trace:" + t["name"])
        if with_traces and gpu and t["name"].startswith(("nouveau", "nvidia")):
            from index_trace import index_trace
            bar0 = next(b for b in gpu["bars"] if b.get("index") == 0)
            with tempfile.TemporaryDirectory(prefix="gp107-trace-") as d:
                trace_path = Path(d) / "capture.xz"
                trace_path.write_bytes(files[t["file"]])
                item["index"] = index_trace(trace_path, int(bar0["start"], 0), int(bar0["size"]), dropped)
            if not item["index"]["complete_by_observed_metadata"]:
                gaps.append("incomplete_trace_records:" + t["name"])
        traces.append(item)
    if b"ERROR" in files.get("gpu3d/vulkaninfo-summary.txt", b""):
        gaps.append("vulkaninfo_failed")
    for bdf, run in report.get("gpu3d", {}).get("runs", {}).items():
        if run.get("rc") != 0 or run.get("result", {}).get("pass") is not True:
            gaps.append("gpu_test_failed:" + bdf)
    if any(c.get("status") == "connected" and not c.get("modes")
           for cs in report.get("trace", {}).get("nvidia_display", {}).values() for c in cs):
        gaps.append("nvidia_display_modes_not_captured")
    uuid_files = sorted(n for n, b in files.items()
                        if n.endswith((".txt", ".json", ".log")) and GPU_UUID.search(b))
    firmware = inspect_roms(files, gpu, inspector) if gpu else []
    full_rom = any(f.get("inspection", {}).get("rom_status") == "ok" for f in firmware)
    essential = {"pci", "gpu_raw", "gpu_drivers", "trace", "gpu3d"}
    missing_stages = sorted(essential - set(report.get("stages", {})))
    if missing_stages:
        gaps.append("missing_essential_stages")
    can_begin = bool(integrity_ok and gpu and not failed and not missing_stages and
                     files.get("gpu/" + gpu["bdf"] + "/config.bin") and firmware)
    return {
        "schema": "pascal-probe-audit/1",
        "archive_sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest(),
        "integrity": {"zip_crc": "verified", "seal_hashes": len(expected), "hashes_match": integrity_ok,
                      "mismatches": bad, "extra_files": extras,
                      "hmac_authenticity": "not-verified-maintainer-key-unavailable"},
        "stage_count": len(report.get("stages", {})), "failed_stages": failed,
        "missing_essential_stages": missing_stages, "capture_complete": integrity_ok and not gaps and not failed,
        "gaps": gaps, "can_begin_offline_analysis": can_begin,
        "complete_rom_verified": full_rom if inspector else None,
        "target": ({k: gpu.get(k) for k in ("bdf", "vendor", "device", "subsystem_vendor", "subsystem_device", "revision", "nv", "bars")} if gpu else None),
        "gpu_tests": report.get("gpu3d", {}).get("runs", {}),
        "traces": traces, "firmware": firmware,
        "privacy": {"gpu_uuid_present_in_files": uuid_files, "publish_raw_report": False},
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive", type=Path)
    p.add_argument("--inspector", type=Path)
    p.add_argument("--index-traces", action="store_true", help="also inspect NVIDIA/Nouveau trace records")
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    try:
        result = audit(a.archive, a.inspector.resolve() if a.inspector else None, a.index_traces)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile, lzma.LZMAError, subprocess.SubprocessError) as e:
        p.exit(2, "audit failed: " + str(e) + "\n")
    text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if a.output:
        a.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0 if result["can_begin_offline_analysis"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
