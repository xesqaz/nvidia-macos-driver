#!/bin/bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
set -euo pipefail
if [[ $(uname -s) != Darwin ]] || ! command -v xcrun >/dev/null; then
    echo "PascalProbe needs macOS, Xcode command-line tools and kernel SDK headers." >&2
    exit 2
fi
pascal_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
pascal_out=${1:-"$pascal_root/out"}
pascal_sdk=${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}
pascal_headers="$pascal_sdk/System/Library/Frameworks/Kernel.framework/Headers"
if [[ ! -f "$pascal_headers/IOKit/IOService.h" || ! -f "$pascal_headers/IOKit/pci/IOPCIDevice.h" ]]; then
    echo "Kernel headers missing. Set SDKROOT to a compatible kernel SDK/KDK." >&2
    exit 2
fi
plutil -lint "$pascal_root/macos/Info.plist"
mkdir -p "$pascal_out/PascalProbe.kext/Contents/MacOS"
pascal_flags=(-arch x86_64 -isysroot "$pascal_sdk" -mmacosx-version-min=15.0
    -fapple-kext -mkernel -nostdinc -I"$pascal_headers" -I"$pascal_root/include"
    -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
    -fno-builtin -fno-common -fno-stack-protector -mno-red-zone -O2 -Wall -Wextra)
xcrun clang "${pascal_flags[@]}" -std=c11 -c "$pascal_root/src/gp107.c" -o "$pascal_out/gp107.o"
xcrun clang++ "${pascal_flags[@]}" -std=c++17 -fno-rtti -fno-exceptions \
    -c "$pascal_root/macos/PascalProbe.cpp" -o "$pascal_out/PascalProbe.o"
xcrun clang++ -arch x86_64 -isysroot "$pascal_sdk" -fapple-kext -nostdlib -Wl,-kext \
    "$pascal_out/PascalProbe.o" "$pascal_out/gp107.o" -lkmodc++ -lkmod -lcc_kext \
    -o "$pascal_out/PascalProbe.kext/Contents/MacOS/PascalProbe"
cp "$pascal_root/macos/Info.plist" "$pascal_out/PascalProbe.kext/Contents/Info.plist"
echo "Built $pascal_out/PascalProbe.kext. It is not installed or hardware-tested."
