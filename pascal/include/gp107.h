/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef PASCAL_GP107_H
#define PASCAL_GP107_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define GP107_VENDOR 0x10deu
#define GP107_DEVICE 0x1c81u
#define GP107_CHIPSET 0x137u
#define GP107_ROM_MAX_IMAGES 8u

enum gp107_status {
    GP107_OK, GP107_ARGUMENT, GP107_SHORT_INPUT, GP107_NOT_TARGET,
    GP107_BAD_HEADER, GP107_BAD_PCIR, GP107_TRUNCATED_ROM,
    GP107_BAD_CHECKSUM, GP107_IMAGE_LIMIT, GP107_UNAVAILABLE
};

struct gp107_bar {
    uint64_t address;
    uint8_t present, io, wide, prefetch, upper;
};

struct gp107_pci {
    uint16_t vendor, device, subsystem_vendor, subsystem_device, command;
    uint8_t revision, base_class, subclass, power_state, power_state_known;
    struct gp107_bar bars[6];
};

struct gp107_chip {
    uint32_t boot0;
    uint16_t chipset;
    uint8_t revision;
};

struct gp107_rom_image {
    size_t offset, length;
    uint16_t vendor, device, efi_machine, efi_compression;
    uint8_t code_type, last, checksum;
};

struct gp107_rom {
    size_t image_count, chain_bytes, trailing_bytes;
    uint8_t complete, has_efi_image;
    struct gp107_rom_image images[GP107_ROM_MAX_IMAGES];
};

/* Pure decoders: no allocation, IO, callbacks or device writes. */
enum gp107_status gp107_decode_pci(const uint8_t *, size_t, struct gp107_pci *);
enum gp107_status gp107_decode_boot0(uint32_t, struct gp107_chip *);
enum gp107_status gp107_decode_rom(const uint8_t *, size_t, struct gp107_rom *);
const char *gp107_status_name(enum gp107_status);

#ifdef __cplusplus
}
#endif
#endif
