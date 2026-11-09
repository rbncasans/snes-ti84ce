/* Detección de la cabecera de un cartucho de SNES (código original). */
#include <stddef.h>

#include "cart.h"
#include "mem.h"

static const uint32_t header_base[3] = { 0x7FC0, 0xFFC0, 0x40FFC0 };
static const uint8_t map_byte[3] = { 0x20, 0x21, 0x25 };
/* Primeras instrucciones típicas de una rutina de reset */
static const uint8_t reset_ops[] = {
    0x78, 0x18, 0x5C, 0x4C, 0xC2, 0xE2, 0x9C, 0xA9, 0x20, 0x22, 0x8D, 0x80, 0x82, 0xFB,
};

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t cart_copier_header(uint32_t file_size)
{
    return (file_size % 1024 == 512) ? 512 : 0;
}

static int16_t score_header(const uint8_t *rom, uint32_t size, uint8_t map)
{
    uint32_t base = header_base[map];
    const uint8_t *h;
    int16_t s = 0;
    uint16_t chk, cmp, reset;
    uint8_t mm, i, printable = 0;

    if (base + 0x40 > size)
        return -1;
    h = rom + base;

    chk = u16(h + 0x1E);
    cmp = u16(h + 0x1C);
    if ((uint16_t)(chk + cmp) == 0xFFFF && chk != 0)
        s += 4;

    mm = h[0x15] & 0xEF;
    if (mm == map_byte[map])
        s += 4;
    else if (map == MAP_LOROM && (mm == 0x22 || mm == 0x23 || mm == 0x32))
        s += 3;
    else if ((mm & 0xE0) == 0x20)
        s += 1;

    if (h[0x17] >= 0x07 && h[0x17] <= 0x0D)
        s += 1;

    for (i = 0; i < 21; i++)
        if (h[i] >= 0x20 && h[i] < 0x7F)
            printable++;
    if (printable >= 19)
        s += 2;

    reset = u16(h + 0x3C);
    if (reset >= 0x8000) {
        uint32_t off;
        s += 2;
        if (map == MAP_LOROM)
            off = reset & 0x7FFF;
        else if (map == MAP_HIROM)
            off = reset;
        else
            off = 0x400000u + reset;
        off %= size;
        for (i = 0; i < sizeof reset_ops; i++)
            if (rom[off] == reset_ops[i]) {
                s += 3;
                break;
            }
    }
    return s;
}

int cart_detect(const uint8_t *rom, uint32_t size, cart_info_t *out)
{
    int16_t best = -1;
    uint8_t map, i;
    const uint8_t *h;

    out->map = MAP_LOROM;
    for (map = 0; map < 3; map++) {
        int16_t s = score_header(rom, size, map);
        if (s > best) {
            best = s;
            out->map = map;
        }
    }
    out->score = best;
    if (best < 0)
        return -1;

    out->header = header_base[out->map];
    h = rom + out->header;
    for (i = 0; i < 21; i++)
        out->title[i] = (h[i] >= 0x20 && h[i] < 0x7F) ? (char)h[i] : ' ';
    out->title[21] = '\0';
    for (i = 21; i > 0 && out->title[i - 1] == ' '; i--)
        out->title[i - 1] = '\0';

    out->map_mode = h[0x15];
    out->cart_type = h[0x16];
    out->region = h[0x19];
    out->fastrom = (uint8_t)((h[0x15] >> 4) & 1);
    out->complement = u16(h + 0x1C);
    out->checksum = u16(h + 0x1E);
    out->reset = u16(h + 0x3C);

    /* SRAM: 1 KB << $FFD8, solo si el cartucho dice tener RAM */
    out->sram_size = 0;
    if (h[0x18] > 0 && h[0x18] <= 0x08) {
        uint8_t chips = h[0x16] & 0x0F;
        if (chips >= 0x01 && chips != 0x03 && chips != 0x06)
            out->sram_size = 1024ul << h[0x18];
    }
    return 0;
}

uint16_t cart_checksum(const uint8_t *rom, uint32_t size)
{
    uint32_t p = 1, i, sum = 0, part = 0, rest;

    if (size == 0)
        return 0;
    while (p * 2 <= size)
        p *= 2;
    for (i = 0; i < p; i++)
        sum += rom[i];
    rest = size - p;
    if (rest) {
        for (i = p; i < size; i++)
            part += rom[i];
        sum += part * ((p + rest / 2) / rest);
    }
    return (uint16_t)sum;
}
