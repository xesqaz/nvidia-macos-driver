#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Index recorded mmiotrace R/W addresses without opening hardware or replaying writes."""
import argparse
from collections import Counter
import json
import lzma
from pathlib import Path


def xz_lines(path, max_bytes):
    """Bound both the codec's dictionary and decompressed text, including concatenated XZ streams."""
    decoder = lzma.LZMADecompressor(memlimit=128 << 20)
    pending, total = bytearray(), 0
    with open(path, "rb") as f:
        chunk = f.read(65536)
        while True:
            data = decoder.decompress(chunk, max_length=65536)
            total += len(data)
            if total > max_bytes:
                raise ValueError("trace exceeds resource limits")
            pending.extend(data)
            while True:
                nl = pending.find(b"\n")
                if nl < 0:
                    break
                if nl + 1 > 16384:
                    raise ValueError("trace line exceeds resource limits")
                yield bytes(pending[:nl + 1])
                del pending[:nl + 1]
            if len(pending) > 16384:
                raise ValueError("trace line exceeds resource limits")
            if decoder.eof:
                chunk = decoder.unused_data or f.read(65536)
                if not chunk:
                    if pending:
                        yield bytes(pending)
                    return
                decoder = lzma.LZMADecompressor(memlimit=128 << 20)
            elif decoder.needs_input:
                chunk = f.read(65536)
                if not chunk:
                    raise ValueError("truncated XZ stream")
            else:
                chunk = b""


def index_trace(path, base, size, reported_overrun=0, max_bytes=256 << 20, max_registers=100000):
    if base < 0 or size <= 0 or base + size > 1 << 64 or reported_overrun < 0:
        raise ValueError("invalid BAR range")
    counts, registers, regions, identity = Counter(), Counter(), Counter(), Counter()
    raw_bytes = 0
    for line in xz_lines(path, max_bytes):
        raw_bytes += len(line)
        if len(line) > 16384 or raw_bytes > max_bytes:
            raise ValueError("trace exceeds resource limits")
        fields = line.split()
        if not fields:
            continue
        kind = fields[0].decode("ascii", "replace")
        counts["lines"] += 1
        if b"LOST" in line.upper():
            counts["lost_event_lines"] += 1
        if kind == "VERSION":
            if fields != [b"VERSION", b"20070824"]:
                raise ValueError("unsupported mmiotrace format")
            counts["version_headers"] += 1
        elif kind == "UNKNOWN":
            counts["unknown_opcodes"] += 1
        elif kind in ("R", "W"):
            try:
                if len(fields) != 8:
                    raise ValueError()
                width, address, value = int(fields[1]), int(fields[4], 0), int(fields[5], 0)
                if width not in (1, 2, 4, 8) or not 0 <= value < (1 << (width * 8)):
                    raise ValueError()
            except ValueError:
                counts["malformed_accesses"] += 1
                continue
            counts[kind] += 1
            if base <= address and address + width <= base + size:
                offset = address - base
                counts["bar0_" + kind] += 1
                key = (kind, offset, width)
                if key in registers or len(registers) < max_registers:
                    registers[key] += 1
                else:
                    counts["untracked_offset_accesses"] += 1
                regions[(kind, offset & ~0xfff)] += 1
                if kind == "R" and width == 4 and offset in (0, 0xa00):
                    identity[(offset, value)] += 1
        elif kind not in ("PCIDEV", "LSPCI", "MAP", "UNMAP", "MARK") and not line.startswith(b"#"):
            counts["unrecognised_records"] += 1
    if counts["version_headers"] != 1:
        raise ValueError("expected exactly one mmiotrace version header")
    complete = not (reported_overrun or counts["lost_event_lines"] or counts["unknown_opcodes"] or
                    counts["malformed_accesses"] or counts["unrecognised_records"])
    return {
        "schema": "pascal-mmio-index/1", "raw_bytes": raw_bytes,
        "bar0_base": hex(base), "bar0_bytes": size, "counts": dict(counts),
        "reported_overrun": reported_overrun, "complete_by_observed_metadata": complete,
        "offset_statistics_complete": not counts["untracked_offset_accesses"],
        "tracked_offset_keys": len(registers),
        "identity_reads": [{"offset": hex(o), "value": hex(v), "reads": n} for (o, v), n in sorted(identity.items())],
        "top_tracked_offsets": [{"operation": k, "offset": hex(o), "width": w, "count": n}
                          for (k, o, w), n in registers.most_common(40)],
        "top_4k_regions": [{"operation": k, "offset": hex(o), "count": n}
                           for (k, o), n in regions.most_common(24)],
        "replay_supported": False,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("trace", type=Path)
    p.add_argument("--base", required=True, type=lambda x: int(x, 0))
    p.add_argument("--size", required=True, type=lambda x: int(x, 0))
    p.add_argument("--reported-overrun", type=int, default=0)
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    try:
        result = index_trace(a.trace, a.base, a.size, a.reported_overrun)
    except (ValueError, OSError, lzma.LZMAError) as e:
        p.exit(2, "index failed: " + str(e) + "\n")
    text = json.dumps(result, indent=2) + "\n"
    if a.output:
        a.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
