/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "gp107.h"

extern "C" {
kern_return_t _start(kmod_info_t *, void *) { return KERN_SUCCESS; }
kern_return_t _stop(kmod_info_t *, void *) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(io.github.xesqaz.PascalProbe, "0.0.1", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = nullptr;
__private_extern__ kmod_stop_func_t *_antimain = nullptr;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
}

class PascalProbe : public IOService {
    OSDeclareDefaultStructors(PascalProbe)
    using super = IOService;
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
private:
    IOPCIDevice *pci_ = nullptr;
    IOMemoryMap *map_ = nullptr;
    bool opened_ = false;
    void cleanup();
    bool readIdentity();
    bool readPtimer(uint64_t *ticks);
};
OSDefineMetaClassAndStructors(PascalProbe, IOService)

IOService *PascalProbe::probe(IOService *provider, SInt32 *score)
{
    int enabled = 0;
    if (!PE_parse_boot_argn("gp107probe", &enabled, sizeof enabled) || enabled != 1)
        return nullptr;
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci || pci->configRead16(0) != GP107_VENDOR || pci->configRead16(2) != GP107_DEVICE)
        return nullptr;
    // The first hardware target is the board for which we have a private baseline.
    if (pci->configRead16(0x2c) != 0x1458 || pci->configRead16(0x2e) != 0x3765)
        return nullptr;
    return super::probe(provider, score);
}

bool PascalProbe::readIdentity()
{
    uint8_t cfg[256];
    for (unsigned i = 0; i < sizeof cfg; ++i) cfg[i] = pci_->configRead8((UInt8)i);
    struct gp107_pci identity;
    enum gp107_status status = gp107_decode_pci(cfg, sizeof cfg, &identity);
    if (status != GP107_OK || !(identity.command & 2) ||
        !identity.power_state_known || identity.power_state != 0 ||
        !identity.bars[0].present || identity.bars[0].io || !identity.bars[0].address) {
        IOLog("PascalProbe: refusing MMIO (identity=%s, memory=%u, power-known=%u, power=%u)\n",
              gp107_status_name(status), (identity.command >> 1) & 1,
              identity.power_state_known, identity.power_state);
        return false;
    }
    IODeviceMemory *memory = pci_->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!memory || memory->getLength() < sizeof(uint32_t)) return false;
    map_ = pci_->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0, kIOMapInhibitCache);
    if (!map_ || !map_->getVirtualAddress() || map_->getLength() < sizeof(uint32_t) ||
        map_->getPhysicalAddress() != identity.bars[0].address) return false;

    // Read-only identity and timer sampling only. Never replay Linux trace writes.
    uint32_t boot0 = OSReadLittleInt32(reinterpret_cast<void *>(map_->getVirtualAddress()), 0);
    struct gp107_chip chip;
    status = gp107_decode_boot0(boot0, &chip);
    if (status != GP107_OK) {
        IOLog("PascalProbe: BOOT_0=%08x (%s)\n", boot0, gp107_status_name(status));
        return false;
    }

    uint64_t ptimerFirst = 0, ptimerSecond = 0;
    bool ptimerFirstValid = readPtimer(&ptimerFirst);
    bool ptimerSecondValid = false;
    if (ptimerFirstValid) {
        IOSleep(1);
        ptimerSecondValid = readPtimer(&ptimerSecond);
    }
    bool ptimerTicking = ptimerSecondValid && ptimerSecond > ptimerFirst;
    setProperty("PascalProbe-stage", "identity-and-read-only-timer");
    setProperty("PascalProbe-pci-id", (unsigned long long)GP107_DEVICE << 16 | GP107_VENDOR, 32);
    setProperty("PascalProbe-boot0", (unsigned long long)boot0, 32);
    setProperty("PascalProbe-chipset", (unsigned long long)chip.chipset, 32);
    setProperty("PascalProbe-bar0-bytes", (unsigned long long)map_->getLength(), 64);
    setProperty("PascalProbe-acceleration", false);
    setProperty("PascalProbe-ptimer-first-valid", ptimerFirstValid);
    setProperty("PascalProbe-ptimer-second-valid", ptimerSecondValid);
    setProperty("PascalProbe-ptimer-ticking", ptimerTicking);
    if (ptimerFirstValid)
        setProperty("PascalProbe-ptimer-first", (unsigned long long)ptimerFirst, 64);
    if (ptimerSecondValid)
        setProperty("PascalProbe-ptimer-second", (unsigned long long)ptimerSecond, 64);
    if (ptimerTicking)
        setProperty("PascalProbe-ptimer-delta", (unsigned long long)(ptimerSecond - ptimerFirst), 64);
    IOLog("PascalProbe: GP107 revision %02x identified; PTIMER samples=%u ticking=%u; no writes, firmware, reset, modeset or acceleration\n",
          chip.revision, ptimerFirstValid && ptimerSecondValid, ptimerTicking);
    return true;
}

bool PascalProbe::readPtimer(uint64_t *ticks)
{
    if (!map_ || !ticks || map_->getLength() < 0x9414) return false;
    void *bar0 = reinterpret_cast<void *>(map_->getVirtualAddress());
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        uint32_t highBefore = OSReadLittleInt32(bar0, 0x9410);
        uint32_t low = OSReadLittleInt32(bar0, 0x9400);
        uint32_t highAfter = OSReadLittleInt32(bar0, 0x9410);
        enum gp107_status status = gp107_decode_ptimer(highBefore, low, highAfter, ticks);
        if (status == GP107_OK) return true;
        if (status != GP107_UNSTABLE_SNAPSHOT) return false;
    }
    return false;
}

void PascalProbe::cleanup()
{
    if (map_) { map_->release(); map_ = nullptr; }
    if (opened_ && pci_) { pci_->close(this); opened_ = false; }
    pci_ = nullptr;
}

bool PascalProbe::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    pci_ = OSDynamicCast(IOPCIDevice, provider);
    if (!pci_ || !pci_->open(this)) {
        IOLog("PascalProbe: PCI provider unavailable or already open\n");
        cleanup(); super::stop(provider); return false;
    }
    opened_ = true;
    bool ok = readIdentity();
    // Retain neither the PCI open nor the MMIO mapping after the one-shot observation.
    cleanup();
    if (!ok) { super::stop(provider); return false; }
    registerService();
    return true;
}

void PascalProbe::stop(IOService *provider)
{
    cleanup();
    super::stop(provider);
}
