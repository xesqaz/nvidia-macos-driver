/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "gp107.h"

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16);
}

enum gp107_status gp107_decode_pci(const uint8_t *p, size_t n, struct gp107_pci *o)
{
    if (!o) return GP107_ARGUMENT;
    *o = (struct gp107_pci){0};
    if (!p) return GP107_ARGUMENT;
    if (n < 64) return GP107_SHORT_INPUT;
    o->vendor = le16(p); o->device = le16(p + 2);
    o->command = le16(p + 4); o->revision = p[8];
    o->base_class = p[11]; o->subclass = p[10];
    o->subsystem_vendor = le16(p + 0x2c);
    o->subsystem_device = le16(p + 0x2e);
    o->power_state = 0xff;
    if (o->vendor != GP107_VENDOR || o->device != GP107_DEVICE ||
        o->base_class != 3 || (o->subclass != 0 && o->subclass != 2))
        return GP107_NOT_TARGET;
    if ((p[0x0e] & 0x7f) != 0) return GP107_BAD_HEADER;
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t low = le32(p + 0x10 + 4 * i);
        struct gp107_bar *b = &o->bars[i];
        if (low == 0 || low == UINT32_MAX) continue;
        b->present = 1; b->io = (uint8_t)(low & 1);
        if (b->io) { b->address = low & ~UINT32_C(3); continue; }
        b->prefetch = (uint8_t)((low >> 3) & 1);
        unsigned type = (low >> 1) & 3;
        if (type != 0 && type != 2) return GP107_BAD_HEADER;
        b->address = low & ~UINT32_C(15);
        if (type == 2) {
            if (i == 5) return GP107_BAD_HEADER;
            b->wide = 1;
            b->address |= (uint64_t)le32(p + 0x10 + 4 * (i + 1)) << 32;
            o->bars[++i].upper = 1;
        }
    }
    /* Capabilities can be absent from a short saved config snapshot. */
    if (!(le16(p + 6) & 0x10)) return GP107_OK;
    uint64_t seen = 0;
    unsigned off = p[0x34] & 0xfc;
    while (off) {
        if (off < 0x40) return GP107_BAD_HEADER;
        if (off + 2 > n) return GP107_SHORT_INPUT;
        uint64_t bit = UINT64_C(1) << (off / 4);
        if (seen & bit) return GP107_BAD_HEADER;
        seen |= bit;
        if (p[off] == 1) {
            if (off + 6 > n) return GP107_SHORT_INPUT;
            o->power_state = (uint8_t)(le16(p + off + 4) & 3);
            o->power_state_known = 1;
        }
        off = p[off + 1] & 0xfc;
    }
    return GP107_OK;
}

enum gp107_status gp107_decode_boot0(uint32_t raw, struct gp107_chip *o)
{
    if (!o) return GP107_ARGUMENT;
    *o = (struct gp107_chip){0};
    o->boot0 = raw;
    if (raw == 0 || raw == UINT32_MAX) return GP107_UNAVAILABLE;
    o->chipset = (uint16_t)((raw >> 20) & 0x1ff);
    o->revision = (uint8_t)(raw & 0xff);
    return o->chipset == GP107_CHIPSET ? GP107_OK : GP107_NOT_TARGET;
}

enum gp107_status gp107_decode_rom(const uint8_t *p, size_t n, struct gp107_rom *o)
{
    if (!o) return GP107_ARGUMENT;
    *o = (struct gp107_rom){0};
    if (!p) return GP107_ARGUMENT;
    size_t off = 0;
    for (;;) {
        if (o->image_count == GP107_ROM_MAX_IMAGES) return GP107_IMAGE_LIMIT;
        if (n - off < 0x1a) return GP107_TRUNCATED_ROM;
        const uint8_t *h = p + off;
        if (le16(h) != 0xaa55) return GP107_BAD_HEADER;
        size_t pcir_off = le16(h + 0x18);
        if (pcir_off < 0x1a || pcir_off > n - off || n - off - pcir_off < 24)
            return GP107_BAD_PCIR;
        const uint8_t *d = h + pcir_off;
        if (le32(d) != UINT32_C(0x52494350)) return GP107_BAD_PCIR;
        size_t length = (size_t)le16(d + 0x10) * 512;
        size_t data_length = le16(d + 0x0a);
        if (data_length < 24 || !length || pcir_off > length || data_length > length - pcir_off)
            return GP107_BAD_PCIR;
        if (length > n - off) return GP107_TRUNCATED_ROM;
        struct gp107_rom_image *im = &o->images[o->image_count++];
        im->offset = off; im->length = length;
        im->vendor = le16(d + 4); im->device = le16(d + 6);
        im->code_type = d[0x14]; im->last = (uint8_t)((d[0x15] >> 7) & 1);
        for (size_t j = 0; j < length; ++j) im->checksum = (uint8_t)(im->checksum + h[j]);
        if (im->vendor != GP107_VENDOR || im->device != GP107_DEVICE) return GP107_NOT_TARGET;
        if (im->checksum) return GP107_BAD_CHECKSUM;
        if (im->code_type == 3) {
            if (le32(h + 4) != 0x0ef1 || le16(h + 0x16) >= length)
                return GP107_BAD_HEADER;
            im->efi_machine = le16(h + 0x0a);
            im->efi_compression = le16(h + 0x0c);
            if (im->efi_compression > 1) return GP107_BAD_HEADER;
            /* EFI code type proves an EFI image, not the protocols implemented by that image. */
            o->has_efi_image = 1;
        }
        off += length; o->chain_bytes = off;
        if (im->last) {
            o->complete = 1; o->trailing_bytes = n - off;
            return GP107_OK;
        }
    }
}

const char *gp107_status_name(enum gp107_status s)
{
    switch (s) {
    case GP107_OK: return "ok";
    case GP107_ARGUMENT: return "invalid-argument";
    case GP107_SHORT_INPUT: return "short-input";
    case GP107_NOT_TARGET: return "not-target-gp107";
    case GP107_BAD_HEADER: return "bad-header";
    case GP107_BAD_PCIR: return "bad-pcir";
    case GP107_TRUNCATED_ROM: return "incomplete-rom-chain";
    case GP107_BAD_CHECKSUM: return "bad-checksum";
    case GP107_IMAGE_LIMIT: return "image-limit";
    case GP107_UNAVAILABLE: return "device-unavailable";
    }
    return "unknown-error";
}
