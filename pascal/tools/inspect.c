/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "gp107.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

static uint8_t *read_file(const char *path, size_t cap, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long end = ftell(f);
    if (end <= 0 || (unsigned long)end > cap || fseek(f, 0, SEEK_SET)) {
        fprintf(stderr, "invalid input size: %s\n", path); fclose(f); return NULL;
    }
    *size = (size_t)end;
    uint8_t *p = malloc(*size);
    if (!p || fread(p, 1, *size, f) != *size) { free(p); fclose(f); return NULL; }
    fclose(f); return p;
}

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s CONFIG.bin VBIOS.rom BOOT0\n", argv[0]); return 1; }
    char *end; errno = 0;
    unsigned long raw = strtoul(argv[3], &end, 0);
    if (errno || end == argv[3] || *end || raw > UINT32_MAX) return 1;
    size_t cn = 0, rn = 0;
    uint8_t *cfg = read_file(argv[1], 4096, &cn);
    uint8_t *rom = read_file(argv[2], 4u << 20, &rn);
    if (!cfg || !rom) { free(cfg); free(rom); return 1; }
    struct gp107_pci pci; struct gp107_chip chip; struct gp107_rom vbios;
    enum gp107_status ps = gp107_decode_pci(cfg, cn, &pci);
    enum gp107_status bs = gp107_decode_boot0((uint32_t)raw, &chip);
    enum gp107_status rs = gp107_decode_rom(rom, rn, &vbios);
    printf("{\n  \"pci_status\":\"%s\",\"boot0_status\":\"%s\",\"rom_status\":\"%s\",\n",
           gp107_status_name(ps), gp107_status_name(bs), gp107_status_name(rs));
    printf("  \"pci\":\"%04x:%04x\",\"subsystem\":\"%04x:%04x\",\"chipset\":\"0x%03x\",\"revision\":%u,\n",
           pci.vendor, pci.device, pci.subsystem_vendor, pci.subsystem_device, chip.chipset, chip.revision);
    printf("  \"memory_decode\":%s,\"power_state\":%u,\"power_state_known\":%s,\n  \"bars\":[",
           (pci.command & 2) ? "true" : "false", pci.power_state, pci.power_state_known ? "true" : "false");
    int comma = 0;
    for (unsigned i = 0; i < 6; ++i) if (pci.bars[i].present) {
        const struct gp107_bar *b = &pci.bars[i];
        printf("%s{\"index\":%u,\"address\":\"0x%" PRIx64 "\",\"io\":%s,\"wide\":%s}",
               comma ? "," : "", i, b->address, b->io ? "true" : "false", b->wide ? "true" : "false");
        comma = 1;
    }
    printf("],\n  \"rom\":{\"complete\":%s,\"has_efi_image\":%s,\"chain_bytes\":%zu,\"trailing_bytes\":%zu,\"images\":[",
           vbios.complete ? "true" : "false", vbios.has_efi_image ? "true" : "false", vbios.chain_bytes, vbios.trailing_bytes);
    for (size_t i = 0; i < vbios.image_count; ++i) {
        const struct gp107_rom_image *im = &vbios.images[i];
        printf("%s{\"offset\":%zu,\"length\":%zu,\"code_type\":%u,\"checksum\":%u,\"efi_machine\":%u}",
               i ? "," : "", im->offset, im->length, im->code_type, im->checksum, im->efi_machine);
    }
    puts("]}\n}"); free(cfg); free(rom);
    return ps == GP107_OK && bs == GP107_OK && rs == GP107_OK ? 0 : 2;
}
