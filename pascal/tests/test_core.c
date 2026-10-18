/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "gp107.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void w16(uint8_t *p, unsigned n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static void w32(uint8_t *p, uint32_t n) { w16(p,n); w16(p+2,n>>16); }
static void pci_fixture(uint8_t *p)
{
    memset(p, 0, 256); w16(p, GP107_VENDOR); w16(p+2, GP107_DEVICE);
    w16(p+4, 7); w16(p+6, 0x10); p[8] = 0xa1; p[11] = 3;
    w16(p+0x2c,0x1458); w16(p+0x2e,0x3765);
    w32(p+0x10,0xf6000000); w32(p+0x14,0xe000000c); w32(p+0x18,1);
    w32(p+0x1c,0xf000000c); w32(p+0x24,0xe001);
    p[0x34]=0x60; p[0x60]=1;
}
static void rom_fixture(uint8_t *p, unsigned type, unsigned last)
{
    memset(p, 0, 512); w16(p,0xaa55); p[2]=1; w16(p+0x18,0x20);
    memcpy(p+0x20,"PCIR",4); w16(p+0x24,GP107_VENDOR); w16(p+0x26,GP107_DEVICE);
    w16(p+0x2a,24); w16(p+0x30,1); p[0x34]=(uint8_t)type; p[0x35]=(uint8_t)(last<<7);
    if(type==3) { w32(p+4,0x0ef1); w16(p+0x0a,0x8664); w16(p+0x16,0x80); }
    unsigned sum=0; for(unsigned i=0;i<511;++i)sum+=p[i]; p[511]=(uint8_t)(0u-sum);
}
int main(void)
{
    uint8_t p[256],r[1024]; struct gp107_pci pci; struct gp107_chip chip; struct gp107_rom rom;
    pci_fixture(p);
    assert(gp107_decode_pci(p,sizeof p,&pci)==GP107_OK);
    assert(pci.bars[1].address==UINT64_C(0x1e0000000) && pci.bars[2].upper);
    assert(pci.bars[5].io && pci.power_state_known && pci.power_state==0);
    assert(gp107_decode_pci(p,20,&pci)==GP107_SHORT_INPUT);
    assert(gp107_decode_pci(p,64,&pci)==GP107_SHORT_INPUT);
    p[0x61]=0x60; assert(gp107_decode_pci(p,sizeof p,&pci)==GP107_BAD_HEADER);
    pci_fixture(p); p[2]=0x82; assert(gp107_decode_pci(p,sizeof p,&pci)==GP107_NOT_TARGET);
    pci_fixture(p); w32(p+0x24,4); assert(gp107_decode_pci(p,sizeof p,&pci)==GP107_BAD_HEADER);
    assert(gp107_decode_pci(NULL,0,&pci)==GP107_ARGUMENT);
    assert(gp107_decode_boot0(0x137000a1,&chip)==GP107_OK && chip.chipset==0x137 && chip.revision==0xa1);
    assert(gp107_decode_boot0(0xffffffff,&chip)==GP107_UNAVAILABLE);
    assert(gp107_decode_boot0(0,&chip)==GP107_UNAVAILABLE);
    assert(gp107_decode_boot0(0x136000a1,&chip)==GP107_NOT_TARGET);
    rom_fixture(r,0,0); rom_fixture(r+512,3,1);
    assert(gp107_decode_rom(r,sizeof r,&rom)==GP107_OK);
    assert(rom.complete && rom.has_efi_image && rom.image_count==2 && rom.chain_bytes==1024);
    assert(gp107_decode_rom(r,512,&rom)==GP107_TRUNCATED_ROM && !rom.complete);
    assert(gp107_decode_rom(r,700,&rom)==GP107_TRUNCATED_ROM);
    r[511]^=1; assert(gp107_decode_rom(r,sizeof r,&rom)==GP107_BAD_CHECKSUM); r[511]^=1;
    r[0x20]=0; assert(gp107_decode_rom(r,sizeof r,&rom)==GP107_BAD_PCIR); r[0x20]='P';
    w16(r+0x18,0xfff0); assert(gp107_decode_rom(r,sizeof r,&rom)==GP107_BAD_PCIR);
    assert(gp107_decode_rom(NULL,0,&rom)==GP107_ARGUMENT);
    rom_fixture(r,0,0); rom_fixture(r+512,3,1);
    for (size_t n=0;n<=sizeof r;++n) (void)gp107_decode_rom(r,n,&rom);
    for (size_t i=0;i<sizeof r;++i) {
        r[i]^=0x80; (void)gp107_decode_rom(r,sizeof r,&rom); r[i]^=0x80;
    }
    /* Bounded malformed-input stress under ASan/UBSan; these are never executed as firmware. */
    uint32_t seed=0x137; uint8_t fuzz[4096];
    for(unsigned round=0;round<20000;++round) {
        for(unsigned i=0;i<sizeof fuzz;++i) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; fuzz[i]=(uint8_t)seed; }
        size_t len=seed%sizeof fuzz;
        (void)gp107_decode_rom(fuzz,len,&rom); (void)gp107_decode_pci(fuzz,len,&pci);
    }
    puts("core: identity, PCI capabilities, 64-bit BARs, BOOT0, ROM chain and malformed-input tests passed");
    return 0;
}
