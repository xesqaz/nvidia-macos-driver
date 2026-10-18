# GP107 stage 0

This directory is the start of the Pascal backend in the xesqaz fork. It does not enable Pascal acceleration in the existing NVRM/GSP stack.

## Components

- `src/gp107.c`: allocation-free C decoders shared by host tools and the diagnostic kext. Checks `10de:1c81`, endpoint/class identity, 32/64-bit BARs, PCI PM capabilities, GP107 `BOOT_0`, coherent high/low/high PTIMER samples, and bounded PCIR option-ROM image chains with checksums.
- `tools/inspect.c`: offline JSON inspection of saved PCI config, a saved ROM, and a supplied `BOOT_0`. It never opens a PCI device.
- `tools/audit_probe.py`: checks ZIP CRCs and seal-file hashes, examines actual stage/test/trace status, optionally uses the host inspector and trace indexer, and flags remaining GPU UUID text without printing its value. It does not verify the maintainer's private HMAC key.
- `tools/index_trace.py`: bounded, streaming XZ/mmiotrace analysis. Counts recorded accesses by BAR-relative offset, checks for lost/unknown records and cross-checks chip identity. It never replays writes. Firmware upload apertures can exhaust the detailed offset table; then exact region/event counts continue and `offset_statistics_complete` becomes false.
- `macos/`: optional one-shot IOKit diagnostic source. Requires `gp107probe=1`, exact PCI/subsystem IDs, memory decoding already enabled, and known PCI D0 before mapping BAR0. Reads `BOOT_0` and takes two bounded, stable PTIMER samples from BAR0, releases its mapping/open, and publishes diagnostic properties. These observations are read-only; there is no user client, interrupt handling, firmware upload, GPU reset, DMA, modesetting or acceleration.

The macOS source has **not** been built or loaded on hardware. The current host runs Linux and has no Apple kernel SDK. No kext binary or installation package is shipped. Do not treat successful host tests as successful macOS validation.

## Host checks

From the repository root:

```sh
cmake -S pascal -B pascal/.build -DCMAKE_C_COMPILER=clang -DPASCAL_SANITIZE=ON
cmake --build pascal/.build
ctest --test-dir pascal/.build --output-on-failure
```

The test suite covers capability-list cycles, 64-bit BARs, wrong/unavailable chip IDs, stable and rollover PTIMER snapshots, truncated/malformed ROMs, checksum failure, structured mutations and 20,000 bounded malformed buffers. Python tests cover archive tampering/traversal, identifier-value suppression, trace range/width checks, dropped-event flags, decompression limits and concatenated XZ streams.

Validation performed on 2026-10-18: Clang 22.1.6 with ASan/UBSan and GCC 16.1.1 Release both passed the core suite and eight Python tests. Clang static analysis reported no findings. On the actual private capture, the full ROM returns success and the short ROM returns `incomplete-rom-chain`; both identify GP107 and PCI D0. The real NVIDIA/Nouveau traces were indexed and their unknown/lost records are recorded in [BASELINE.md](docs/BASELINE.md). The macOS build helper's Linux refusal was checked; it is not a successful kernel build.

## Private capture inspection

```sh
pascal/.build/gp107-inspect /private/path/config.bin /private/path/vbios.rom 0x137000a1

python3 pascal/tools/audit_probe.py /private/path/report.zip \
  --inspector pascal/.build/gp107-inspect --index-traces \
  --output /private/path/audit.json

python3 pascal/tools/index_trace.py /private/path/nvidia.mmio.xz \
  --base 0xf6000000 --size 0x1000000 --output /private/path/index.json
```

The BAR addresses above are from one captured boot. Discover them from each new report; they are not hardware constants. Pass any `kernel_overrun` count from `trace.json` to the standalone indexer with `--reported-overrun`; the integrated audit does this automatically.

`gp107-inspect` returns 0 only for valid target PCI identity, a GP107 `BOOT_0` and a complete valid ROM chain; 2 means a rejected or incomplete observation. A partial ROM containing only a legacy image does **not** establish that EFI/GOP is absent. EFI code type 3 establishes an EFI image, not which protocols it implements.

`audit_probe.py` returning 0 means offline analysis can begin. Inspect `capture_complete` and `gaps` independently: a usable report can contain incomplete traces. HMAC authenticity is reported as unverified, even when every listed SHA-256 matches. No tool uploads reports or firmware.

## Optional macOS build

On a Mac with compatible kernel headers and link libraries:

```sh
bash pascal/macos/build.sh
```

`SDKROOT` can point to a compatible SDK. The helper stops when macOS/Xcode/kernel headers are unavailable. The output is not installed automatically. Stage 0 accepts only the tested baseline board `1458:3765`; it should not be broadened before another board is independently checked.

The first macOS acceptance check will be a real boot with this module, the expected IORegistry properties, matching `BOOT_0` and a PTIMER delta greater than zero. A static or unreadable PTIMER is reported as a diagnostic result; it does not turn the probe into an accelerator. No existing NVRM support gate has been broadened.

## Reference boundaries

- Linux Nouveau [device/chip decoding and GP107 subsystem map](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/drivers/gpu/drm/nouveau/nvkm/engine/device/base.c).
- Linux [mmiotrace format and missing-event limitations](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/Documentation/trace/mmiotrace.rst). MMIO captures do not describe every host-RAM DMA buffer or every action performed inside signed GPU firmware.
- EDK II [PCI option-ROM/PCIR/EFI header definitions](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/IndustryStandard/Pci22.h).
- Apple [IOPCIDevice API](https://github.com/apple-oss-distributions/IOPCIFamily/blob/main/IOKit/pci/IOPCIDevice.h).

Firmware blobs and private machine captures are not included. Existing repository licensing remains unchanged. Public sources here serve as references; incorporating third-party implementation code will require checking its own licence and dependencies.
