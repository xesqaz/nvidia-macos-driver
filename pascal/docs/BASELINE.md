# GP107 baseline — 2026-10-18

Sanitised findings from a private 1401 Probe 2.2.0 run on a desktop GTX 1050. The archive, raw firmware and hardware identifiers remain outside this repository.

## Confirmed target

| Property | Observation |
|---|---|
| GPU | Gigabyte GeForce GTX 1050, GP107, 2 GB |
| PCI / subsystem | `10de:1c81` / `1458:3765` |
| `BOOT_0` | `0x137000a1`, chipset `0x137`, revision `a1` |
| VBIOS version | `86.07.39.40.97` |
| PCI power | D0 in the captured config |
| Captured BAR0/BAR1/BAR3 sizes | 16 MiB / 256 MiB / 32 MiB |
| Linux GPU test | Mesa NVK GP107, Vulkan API 1.3.354, test pack Mesa 26.1.6 |
| Compute result | 1,048,576 results checked; 0 wrong |
| Render result | 65,536 results checked; 0 wrong |

These are results of one Linux run, not proof of macOS Metal, broad Vulkan conformance or application performance. The successful NVK test gives us a usable Linux reference for both compute and drawing on this actual GP107.

## Integrity and completeness

- ZIP CRC checks pass. All 98 file hashes in `seal.json` match; no extra payload files were found. HMAC authenticity cannot be independently checked without the maintainer's private key.
- All 18 scanner stages completed, with an empty pending-step marker.
- Nouveau trace: 2,169,781 lines; 545,008 reported overruns and 1,536,272 `UNKNOWN` opcode records. Useful for comparison, incomplete as an instruction trace.
- Closed NVIDIA 580.178.04 trace: 1,294,933 lines; no reported ring overrun, but 54 `UNKNOWN` records. Therefore this trace is not an exhaustive replay recipe either.
- The standalone `vulkaninfo` display query returned `ERROR_OUT_OF_HOST_MEMORY`. The separately executed compute/render tests returned success with independently checked results.
- Closed-driver display snapshots have connected outputs without captured mode lists. Nouveau snapshots provide active DVI/HDMI modes and EDID.
- One text file still contains a GPU UUID despite the scanner's redaction. No identifier value is reproduced here. Raw reports are kept private.

This run is sufficient for stage-0 implementation and offline analysis. There is no need to repeat it just to start coding. A targeted later capture may be needed for an exact missing sequence.

## ROM correction

The raw sysfs ROM is 58,880 bytes: a checksum-valid legacy image whose PCIR last-image bit is clear. The next image is absent. Its reported `has_gop=false` is not evidence that the card lacks an EFI image.

The Nouveau debugfs ROM is 236,032 bytes. Our parser finds a complete chain:

| Offset | Length | Code type | Checksum |
|---|---|---|---|
| 0 | 61,440 bytes | Legacy (0) | Valid |
| 61,440 | 67,584 bytes | EFI (3), machine `0x8664` | Valid |

There are 107,008 trailing bytes after the last declared PCIR image. They are preserved as part of the private dump, not interpreted as another executable image. An EFI image exists; GOP protocol support inside it has not yet been separately decoded. The diagnostic parser never executes either image.

## Next hardware acceptance result

Build and test `PascalProbe` on macOS, confirm this exact PCI identity and `BOOT_0`, then proceed to a separately designed Pascal memory/firmware backend. Host parser tests and the source-only diagnostic kext do not yet establish successful macOS hardware initialisation.
