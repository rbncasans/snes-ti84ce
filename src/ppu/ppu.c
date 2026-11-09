/*
 * PPU de la SNES: registros $2100-$213F, VRAM/CGRAM/OAM y renderizado por
 * líneas de los modos 0-7 con sprites, ventanas y mosaico.
 *
 * Nota para la calculadora: en el eZ80 `int` es de 24 bits, así que aquí
 * se usan tipos de tamaño fijo y `int32_t` donde los valores pueden pasar
 * de +-8 millones (modo 7).
 */
#include <string.h>

#include "ppu.h"

ppu_t ppu;
uint8_t ppu_palette_dirty;
uint8_t ppu_open_bus;

static uint8_t *out_base;
static uint16_t out_pitch;

/* Búferes de una línea */
static uint8_t line_col[256]; /* Índice CGRAM ganador */
static uint8_t line_z[256];   /* Prioridad del ganador (0 = fondo) */
static uint8_t lay_col[256];  /* Capa en curso */
static uint8_t lay_z[256];    /* 0 = transparente */
static uint8_t win_mask[256]; /* 1 = píxel recortado por la ventana */

/* ------------------------------------------------------------------ */
/* Tablas de modos                                                     */
/* ------------------------------------------------------------------ */

/* Bits por píxel de BG1..BG4 en cada modo (0 = la capa no existe). */
static const uint8_t mode_bpp[8][4] = {
    {2, 2, 2, 2}, {4, 4, 2, 0}, {4, 4, 0, 0}, {8, 4, 0, 0},
    {8, 2, 0, 0}, {4, 2, 0, 0}, {4, 0, 0, 0}, {8, 0, 0, 0},
};

/*
 * Prioridades (mayor = más delante). Índices: [capa][bit de prioridad].
 * Orden de delante a atrás según la documentación del hardware.
 */
static const uint8_t z_mode0[4][2] = {{8, 11}, {7, 10}, {2, 5}, {1, 4}};
static const uint8_t z_mode0_obj[4] = {3, 6, 9, 12};
static const uint8_t z_mode1[4][2] = {{6, 9}, {5, 8}, {1, 3}, {0, 0}};
static const uint8_t z_mode1_obj[4] = {2, 4, 7, 10};
static const uint8_t z_mode2[4][2] = {{3, 7}, {1, 5}, {0, 0}, {0, 0}};
static const uint8_t z_mode2_obj[4] = {2, 4, 6, 8};
/* Modo 7: BG1 = 3; con EXTBG, BG2 = 1 (prioridad 0) / 5 (prioridad 1). */
static const uint8_t z_mode7_obj[4] = {2, 4, 6, 7};

/* Tamaños de sprite (ancho, alto) según OBSEL bits 5-7: [tamaño][pequeño/grande] */
static const uint8_t obj_w[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32},
};
static const uint8_t obj_h[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {32, 64}, {32, 32},
};

static const uint8_t vram_inc[4] = {1, 32, 128, 128};

/* Brillo: tabla de 5 bits -> 5 bits para el nivel actual */
static uint8_t bright_lut[32];

/* ------------------------------------------------------------------ */
/* Inicialización                                                      */
/* ------------------------------------------------------------------ */

void ppu_init(uint8_t *vram, uint8_t *out, uint16_t pitch)
{
    ppu.vram = vram;
    out_base = out;
    out_pitch = pitch;
    ppu_reset();
}

void ppu_reset(void)
{
    uint8_t *vram = ppu.vram;
    memset(&ppu, 0, sizeof(ppu));
    ppu.vram = vram;
    if (vram)
        memset(vram, 0, PPU_VRAM_SIZE);
    ppu.inidisp = 0x80; /* Forced blank al arrancar */
    ppu.stat77 = 0x01;
    ppu_palette_dirty = 1;
}

uint16_t ppu_visible_lines(void)
{
    return (ppu.setini & 0x04) ? 239 : 224;
}

/* ------------------------------------------------------------------ */
/* VRAM                                                                */
/* ------------------------------------------------------------------ */

static uint16_t vram_remap(uint16_t a)
{
    switch ((ppu.vmain >> 2) & 3) {
    case 1: return (a & 0xFF00) | ((a & 0x001F) << 3) | ((a >> 5) & 7);
    case 2: return (a & 0xFE00) | ((a & 0x003F) << 3) | ((a >> 6) & 7);
    case 3: return (a & 0xFC00) | ((a & 0x007F) << 3) | ((a >> 7) & 7);
    default: return a;
    }
}

static void vram_prefetch(void)
{
    uint16_t a = (uint16_t)((vram_remap(ppu.vaddr) & 0x7FFF) << 1);
    ppu.vprefetch = ppu.vram[a] | ((uint16_t)ppu.vram[a + 1] << 8);
}

/* ------------------------------------------------------------------ */
/* Escritura de registros                                              */
/* ------------------------------------------------------------------ */

static void write_hofs(uint8_t n, uint8_t v)
{
    ppu.bg[n].hofs = (uint16_t)(((uint16_t)v << 8) | (ppu.ofs_latch & ~7u) |
                                (ppu.hofs_latch & 7u)) & 0x3FF;
    ppu.ofs_latch = v;
    ppu.hofs_latch = v;
}

static void write_vofs(uint8_t n, uint8_t v)
{
    ppu.bg[n].vofs = (uint16_t)(((uint16_t)v << 8) | ppu.ofs_latch) & 0x3FF;
    ppu.ofs_latch = v;
}

/* Registros de 16 bits del modo 7 (dos escrituras, latch compartido) */
static int16_t m7_word(uint8_t v)
{
    int16_t w = (int16_t)(((uint16_t)v << 8) | ppu.m7_latch);
    ppu.m7_latch = v;
    return w;
}

/* Signo de 13 bits */
static int16_t sext13(int16_t v)
{
    return (int16_t)(v & 0x1000 ? (v | (int16_t)0xE000) : (v & 0x1FFF));
}

void ppu_write(uint8_t reg, uint8_t v)
{
    switch (reg) {
    case 0x00: /* INIDISP */
        if (v != ppu.inidisp)
            ppu_palette_dirty = 1;
        ppu.inidisp = v;
        break;
    case 0x01: ppu.obsel = v; break;
    case 0x02: /* OAMADDL */
        ppu.oam_reload = (ppu.oam_reload & 0x100) | v;
        ppu.oam_addr = (uint16_t)(ppu.oam_reload << 1);
        break;
    case 0x03: /* OAMADDH */
        ppu.oam_reload = (uint16_t)((v & 1) << 8) | (ppu.oam_reload & 0xFF);
        ppu.oam_prio_rot = v & 0x80;
        ppu.oam_addr = (uint16_t)(ppu.oam_reload << 1);
        break;
    case 0x04: { /* OAMDATA */
        uint16_t a = ppu.oam_addr;
        if (a < 0x200) {
            if (!(a & 1)) {
                ppu.oam_latch = v;
            } else {
                ppu.oam[a - 1] = ppu.oam_latch;
                ppu.oam[a] = v;
            }
        } else {
            ppu.oam[0x200 + (a & 0x1F)] = v;
        }
        ppu.oam_addr = (a + 1) & 0x3FF;
        break;
    }
    case 0x05: ppu.bgmode = v; break;
    case 0x06: ppu.mosaic = v; break;
    case 0x07: case 0x08: case 0x09: case 0x0A:
        ppu.bg[reg - 0x07].sc = v;
        break;
    case 0x0B: ppu.bg[0].nba = v & 0x0F; ppu.bg[1].nba = v >> 4; break;
    case 0x0C: ppu.bg[2].nba = v & 0x0F; ppu.bg[3].nba = v >> 4; break;
    case 0x0D:
        ppu.m7hofs = sext13(m7_word(v));
        write_hofs(0, v);
        break;
    case 0x0E:
        ppu.m7vofs = sext13(m7_word(v));
        write_vofs(0, v);
        break;
    case 0x0F: write_hofs(1, v); break;
    case 0x10: write_vofs(1, v); break;
    case 0x11: write_hofs(2, v); break;
    case 0x12: write_vofs(2, v); break;
    case 0x13: write_hofs(3, v); break;
    case 0x14: write_vofs(3, v); break;
    case 0x15: ppu.vmain = v; break;
    case 0x16:
        ppu.vaddr = (ppu.vaddr & 0xFF00) | v;
        vram_prefetch();
        break;
    case 0x17:
        ppu.vaddr = (uint16_t)((uint16_t)v << 8) | (ppu.vaddr & 0xFF);
        vram_prefetch();
        break;
    case 0x18:
        ppu.vram[(uint16_t)((vram_remap(ppu.vaddr) & 0x7FFF) << 1)] = v;
        if (!(ppu.vmain & 0x80))
            ppu.vaddr += vram_inc[ppu.vmain & 3];
        break;
    case 0x19:
        ppu.vram[(uint16_t)(((vram_remap(ppu.vaddr) & 0x7FFF) << 1) | 1)] = v;
        if (ppu.vmain & 0x80)
            ppu.vaddr += vram_inc[ppu.vmain & 3];
        break;
    case 0x1A: ppu.m7sel = v; break;
    case 0x1B: ppu.m7a = m7_word(v); break;
    case 0x1C: ppu.m7b = m7_word(v); break;
    case 0x1D: ppu.m7c = m7_word(v); break;
    case 0x1E: ppu.m7d = m7_word(v); break;
    case 0x1F: ppu.m7x = sext13(m7_word(v)); break;
    case 0x20: ppu.m7y = sext13(m7_word(v)); break;
    case 0x21: ppu.cg_addr = (uint16_t)v << 1; break;
    case 0x22:
        if (!(ppu.cg_addr & 1)) {
            ppu.cg_latch = v;
        } else {
            ppu.cgram[ppu.cg_addr >> 1] =
                (uint16_t)(((uint16_t)(v & 0x7F) << 8) | ppu.cg_latch);
            ppu_palette_dirty = 1;
        }
        ppu.cg_addr = (ppu.cg_addr + 1) & 0x1FF;
        break;
    case 0x23: case 0x24: case 0x25: ppu.wsel[reg - 0x23] = v; break;
    case 0x26: ppu.wl[0] = v; break;
    case 0x27: ppu.wr[0] = v; break;
    case 0x28: ppu.wl[1] = v; break;
    case 0x29: ppu.wr[1] = v; break;
    case 0x2A: ppu.wbglog = v; break;
    case 0x2B: ppu.wobjlog = v; break;
    case 0x2C: ppu.tm = v; break;
    case 0x2D: ppu.ts = v; break;
    case 0x2E: ppu.tmw = v; break;
    case 0x2F: ppu.tsw = v; break;
    case 0x30: ppu.cgwsel = v; break;
    case 0x31: ppu.cgadsub = v; break;
    case 0x32: { /* COLDATA */
        uint16_t c = v & 0x1F;
        if (v & 0x20) ppu.fixed_color = (ppu.fixed_color & ~0x001Fu) | c;
        if (v & 0x40) ppu.fixed_color = (ppu.fixed_color & ~0x03E0u) | (c << 5);
        if (v & 0x80) ppu.fixed_color = (ppu.fixed_color & ~0x7C00u) | (c << 10);
        break;
    }
    case 0x33: ppu.setini = v; break;
    default: break; /* $2134-$213F son de solo lectura */
    }
}

/* ------------------------------------------------------------------ */
/* Lectura de registros                                                */
/* ------------------------------------------------------------------ */

void ppu_set_counters(uint16_t h, uint16_t v)
{
    ppu.hcount = h;
    ppu.vcount = v;
}

uint8_t ppu_read(uint8_t reg)
{
    uint8_t r;
    switch (reg) {
    case 0x34: case 0x35: case 0x36: { /* MPYL/M/H: M7A * (M7B >> 8) con signo */
        int32_t m = (int32_t)ppu.m7a * (int8_t)((uint16_t)ppu.m7b >> 8);
        r = (uint8_t)(m >> ((reg - 0x34) * 8));
        ppu.ppu1_open_bus = r;
        return r;
    }
    case 0x37: /* SLHV: captura los contadores */
        ppu.hlatch = ppu.hcount;
        ppu.vlatch = ppu.vcount;
        ppu.counters_latched = 1;
        return ppu_open_bus;
    case 0x38: { /* RDOAM */
        uint16_t a = ppu.oam_addr;
        r = a < 0x200 ? ppu.oam[a] : ppu.oam[0x200 + (a & 0x1F)];
        ppu.oam_addr = (a + 1) & 0x3FF;
        ppu.ppu1_open_bus = r;
        return r;
    }
    case 0x39: /* RDVRAML */
        r = (uint8_t)ppu.vprefetch;
        if (!(ppu.vmain & 0x80)) {
            vram_prefetch();
            ppu.vaddr += vram_inc[ppu.vmain & 3];
        }
        ppu.ppu1_open_bus = r;
        return r;
    case 0x3A: /* RDVRAMH */
        r = (uint8_t)(ppu.vprefetch >> 8);
        if (ppu.vmain & 0x80) {
            vram_prefetch();
            ppu.vaddr += vram_inc[ppu.vmain & 3];
        }
        ppu.ppu1_open_bus = r;
        return r;
    case 0x3B: { /* RDCGRAM */
        uint16_t c = ppu.cgram[ppu.cg_addr >> 1];
        if (!(ppu.cg_addr & 1))
            r = (uint8_t)c;
        else
            r = (uint8_t)((c >> 8) & 0x7F) | (ppu.ppu2_open_bus & 0x80);
        ppu.cg_addr = (ppu.cg_addr + 1) & 0x1FF;
        ppu.ppu2_open_bus = r;
        return r;
    }
    case 0x3C: /* OPHCT */
        if (!ppu.hlatch_hi)
            r = (uint8_t)ppu.hlatch;
        else
            r = (uint8_t)((ppu.hlatch >> 8) & 1) | (ppu.ppu2_open_bus & 0xFE);
        ppu.hlatch_hi ^= 1;
        ppu.ppu2_open_bus = r;
        return r;
    case 0x3D: /* OPVCT */
        if (!ppu.vlatch_hi)
            r = (uint8_t)ppu.vlatch;
        else
            r = (uint8_t)((ppu.vlatch >> 8) & 1) | (ppu.ppu2_open_bus & 0xFE);
        ppu.vlatch_hi ^= 1;
        ppu.ppu2_open_bus = r;
        return r;
    case 0x3E: /* STAT77 */
        r = (ppu.stat77 & 0xC0) | (ppu.ppu1_open_bus & 0x10) | 0x01;
        ppu.ppu1_open_bus = r;
        return r;
    case 0x3F: /* STAT78 (NTSC, versión 3) */
        r = (uint8_t)(ppu.field << 7) | (uint8_t)(ppu.counters_latched << 6) |
            (ppu.ppu2_open_bus & 0x20) | 0x03;
        ppu.counters_latched = 0;
        ppu.hlatch_hi = 0;
        ppu.vlatch_hi = 0;
        ppu.ppu2_open_bus = r;
        return r;
    default:
        /* Registros de solo escritura: bus abierto de la CPU */
        return ppu_open_bus;
    }
}

/* ------------------------------------------------------------------ */
/* Paleta                                                              */
/* ------------------------------------------------------------------ */

void ppu_build_palette(uint16_t *dst)
{
    uint16_t i;
    uint8_t b = ppu.inidisp & 0x0F;

    if (ppu.inidisp & 0x80) {
        memset(dst, 0, 256 * sizeof(uint16_t));
        return;
    }
    for (i = 0; i < 32; i++)
        bright_lut[i] = (uint8_t)((i * (uint16_t)(b + 1)) >> 4);
    for (i = 0; i < 256; i++) {
        uint16_t c = ppu.cgram[i];
        uint16_t r = bright_lut[c & 0x1F];
        uint16_t g = bright_lut[(c >> 5) & 0x1F];
        uint16_t bl = bright_lut[(c >> 10) & 0x1F];
        dst[i] = (uint16_t)((r << 10) | (g << 5) | bl);
    }
}

/* ------------------------------------------------------------------ */
/* Ventanas y mosaico                                                  */
/* ------------------------------------------------------------------ */

/*
 * Calcula win_mask para una capa. sel = nibble de WxxSEL, logic = 2 bits.
 * Devuelve 0 si ninguna ventana está activa (no hay que recortar).
 */
static uint8_t build_window(uint8_t sel, uint8_t logic)
{
    uint8_t e1 = sel & 0x02, e2 = sel & 0x08;
    uint8_t i1 = sel & 0x01, i2 = (sel >> 2) & 0x01;
    uint16_t x;

    if (!e1 && !e2)
        return 0;
    for (x = 0; x < 256; x++) {
        uint8_t a = (uint8_t)((x >= ppu.wl[0] && x <= ppu.wr[0]) ^ i1);
        uint8_t b = (uint8_t)((x >= ppu.wl[1] && x <= ppu.wr[1]) ^ i2);
        uint8_t m;
        if (!e2)
            m = a;
        else if (!e1)
            m = b;
        else {
            switch (logic & 3) {
            case 0: m = a | b; break;
            case 1: m = a & b; break;
            case 2: m = a ^ b; break;
            default: m = !(a ^ b); break;
            }
        }
        win_mask[x] = m;
    }
    return 1;
}

static void apply_mosaic_h(void)
{
    uint8_t size = (ppu.mosaic >> 4) + 1;
    uint16_t x = 0;
    while (x < 256) {
        uint8_t c = lay_col[x], z = lay_z[x];
        uint8_t k;
        for (k = 0; k < size && x < 256; k++, x++) {
            lay_col[x] = c;
            lay_z[x] = z;
        }
    }
}

static uint16_t mosaic_line(uint16_t v)
{
    uint8_t size = (ppu.mosaic >> 4) + 1;
    return (uint16_t)(v - (uint16_t)((v - 1) % size));
}

/* Mezcla lay_* sobre line_* respetando prioridades y la ventana. */
static void compose(uint8_t windowed)
{
    uint16_t x;
    if (windowed) {
        for (x = 0; x < 256; x++) {
            if (lay_z[x] > line_z[x] && !win_mask[x]) {
                line_z[x] = lay_z[x];
                line_col[x] = lay_col[x];
            }
        }
    } else {
        for (x = 0; x < 256; x++) {
            if (lay_z[x] > line_z[x]) {
                line_z[x] = lay_z[x];
                line_col[x] = lay_col[x];
            }
        }
    }
}

/* Recorte por ventana de la pantalla principal (TMW) para la capa `bit`. */
static uint8_t layer_window(uint8_t layer)
{
    uint8_t sel, logic;
    if (!(ppu.tmw & (1 << layer)))
        return 0;
    if (layer < 4) {
        sel = (uint8_t)(ppu.wsel[layer >> 1] >> ((layer & 1) * 4)) & 0x0F;
        logic = (uint8_t)(ppu.wbglog >> (layer * 2)) & 3;
    } else {
        sel = ppu.wsel[2] & 0x0F;
        logic = ppu.wobjlog & 3;
    }
    return build_window(sel, logic);
}

/* ------------------------------------------------------------------ */
/* Fondos por tiles (modos 0-6)                                        */
/* ------------------------------------------------------------------ */

/* Decodifica una fila de 8 píxeles planar -> chunky. addr = byte de la fila. */
static void decode_row(uint16_t addr, uint8_t bpp, uint8_t hflip, uint8_t *px)
{
    const uint8_t *vr = ppu.vram;
    uint8_t p0 = vr[addr], p1 = vr[(uint16_t)(addr + 1)];
    uint8_t p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0;
    uint8_t i;

    if (bpp >= 4) {
        p2 = vr[(uint16_t)(addr + 16)];
        p3 = vr[(uint16_t)(addr + 17)];
    }
    if (bpp == 8) {
        p4 = vr[(uint16_t)(addr + 32)];
        p5 = vr[(uint16_t)(addr + 33)];
        p6 = vr[(uint16_t)(addr + 48)];
        p7 = vr[(uint16_t)(addr + 49)];
    }
    for (i = 0; i < 8; i++) {
        uint8_t c = (uint8_t)((p0 >> 7) | ((p1 >> 6) & 2));
        p0 <<= 1;
        p1 <<= 1;
        if (bpp >= 4) {
            c |= (uint8_t)(((p2 >> 5) & 4) | ((p3 >> 4) & 8));
            p2 <<= 1;
            p3 <<= 1;
            if (bpp == 8) {
                c |= (uint8_t)(((p4 >> 3) & 16) | ((p5 >> 2) & 32) |
                               ((p6 >> 1) & 64) | (p7 & 128));
                p4 <<= 1;
                p5 <<= 1;
                p6 <<= 1;
                p7 <<= 1;
            }
        }
        px[hflip ? 7 - i : i] = c;
    }
}

/* Entrada del mapa de tiles de la capa n en la celda (tx, ty) de 8 o 16 px. */
static uint16_t map_entry(const ppu_bg_t *bg, uint16_t tx, uint16_t ty)
{
    uint16_t base = (uint16_t)((bg->sc & 0xFC) << 9); /* bytes */
    uint16_t off = 0;
    uint16_t a;

    if (bg->sc & 1) { /* 64 de ancho */
        if (tx & 32) off = 0x800;
        if ((bg->sc & 2) && (ty & 32)) off += 0x1000;
    } else if ((bg->sc & 2) && (ty & 32)) { /* 64 de alto */
        off = 0x800;
    }
    a = (uint16_t)(base + off + ((((ty & 31) << 5) | (tx & 31)) << 1));
    return ppu.vram[a] | ((uint16_t)ppu.vram[(uint16_t)(a + 1)] << 8);
}

/* Lectura del mapa de BG3 para offset-per-tile (coordenadas en píxeles). */
static uint16_t opt_entry(uint16_t x, uint16_t y)
{
    const ppu_bg_t *bg = &ppu.bg[2];
    uint8_t big = (ppu.bgmode >> 6) & 1;
    uint8_t sh = big ? 4 : 3;
    return map_entry(bg, (x & 0x3FF) >> sh, (y & 0x3FF) >> sh);
}

static void render_bg(uint8_t n, uint16_t v, uint8_t bpp, uint8_t zlo,
                      uint8_t zhi, uint8_t paloff)
{
    const ppu_bg_t *bg = &ppu.bg[n];
    uint8_t mode = ppu.bgmode & 7;
    uint8_t hires = (mode == 5 || mode == 6);
    uint8_t opt = (mode == 2 || mode == 4 || mode == 6);
    uint8_t big = (ppu.bgmode >> (4 + n)) & 1;
    uint8_t wide = big || hires;          /* Tiles de 16 de ancho */
    uint16_t chr = (uint16_t)(bg->nba << 13);
    uint8_t tile_bytes = (uint8_t)(bpp * 8);
    uint16_t y = (ppu.mosaic & (1 << n)) ? mosaic_line(v) : v;
    uint8_t fine = bg->hofs & 7;
    uint8_t col;
    int16_t x0; /* x de pantalla donde empieza la columna (puede ser < 0) */
    uint8_t px[16], pz[16];

    for (col = 0, x0 = -(int16_t)fine; x0 < 256; col++, x0 += 8) {
        uint16_t hofs = bg->hofs, vofs = bg->vofs;
        uint16_t yy, xx, entry, tile, tx, ty, ry, addr;
        uint8_t pal, z, hf, vf, sub_x, i, nb, k;

        /* Offset-per-tile: BG3 aporta desplazamientos por columna */
        if (opt && col > 0) {
            uint16_t valid = (uint16_t)(0x2000u << n);
            uint16_t ox = (uint16_t)((col - 1) * 8 + (ppu.bg[2].hofs & ~7u));
            uint16_t h = opt_entry(ox, ppu.bg[2].vofs);
            if (mode == 4) {
                if (h & valid) {
                    if (h & 0x8000)
                        vofs = h;
                    else
                        hofs = (h & ~7u) | (hofs & 7u);
                }
            } else {
                uint16_t vv = opt_entry(ox, ppu.bg[2].vofs + 8);
                if (h & valid) hofs = (h & ~7u) | (hofs & 7u);
                if (vv & valid) vofs = vv;
            }
        }

        yy = (uint16_t)(y + vofs);
        /* Coordenada x del primer píxel de la columna, en píxeles normales */
        xx = (uint16_t)((col * 8) + (hofs & ~7u));

        if (big) {
            ty = (yy & 0x3FF) >> 4;
            ry = yy & 15;
        } else {
            ty = (yy & 0x3FF) >> 3;
            ry = yy & 7;
        }

        /* En modos hires cada columna de 8 px de pantalla son 16 px hires;
         * se decodifican las dos mitades y se toman los píxeles impares
         * (los de la pantalla principal). */
        nb = hires ? 2 : 1;
        for (k = 0; k < nb; k++) {
            uint16_t hx = hires ? (uint16_t)(xx * 2 + k * 8) : xx;
            uint16_t c8 = (hx & (hires ? 0x7FF : 0x3FF)) >> 3; /* columna de 8 px */
            if (wide) {
                tx = c8 >> 1;
                sub_x = c8 & 1;
            } else {
                tx = c8;
                sub_x = 0;
            }
            entry = map_entry(bg, tx, ty);
            tile = entry & 0x3FF;
            pal = (entry >> 10) & 7;
            z = (entry & 0x2000) ? zhi : zlo;
            hf = (entry >> 14) & 1;
            vf = (entry >> 15) & 1;
            {
                uint8_t r = vf ? (uint8_t)((big ? 15 : 7) - ry) : (uint8_t)ry;
                if (r & 8) tile += 16;
                if (wide && (sub_x ^ hf)) tile += 1;
                addr = (uint16_t)(chr + (tile & 0x3FF) * tile_bytes + (r & 7) * 2);
            }
            decode_row(addr, bpp, hf, px + k * 8);
            for (i = k * 8; i < k * 8 + 8; i++) {
                uint8_t c = px[i];
                if (c) {
                    if (bpp == 2) c = (uint8_t)(paloff + pal * 4 + c);
                    else if (bpp == 4) c = (uint8_t)(pal * 16 + c);
                    px[i] = c;
                }
                pz[i] = c ? z : 0;
            }
        }

        /* Copia de la columna a la línea de la capa */
        if (hires) {
            /* 16 píxeles hires -> 8 de pantalla: se toman los impares,
             * que son los de la pantalla principal. */
            for (i = 0; i < 8; i++) {
                int16_t sx = x0 + i;
                if (sx >= 0 && sx < 256) {
                    lay_col[sx] = px[i * 2 + 1];
                    lay_z[sx] = pz[i * 2 + 1];
                }
            }
        } else {
            for (i = 0; i < 8; i++) {
                int16_t sx = x0 + i;
                if (sx >= 0 && sx < 256) {
                    lay_col[sx] = px[i];
                    lay_z[sx] = pz[i];
                }
            }
        }
    }

    if (ppu.mosaic & (1 << n) && (ppu.mosaic >> 4))
        apply_mosaic_h();
}

/* ------------------------------------------------------------------ */
/* Modo 7                                                              */
/* ------------------------------------------------------------------ */

static int32_t m7_clip(int32_t n)
{
    return (n & 0x2000) ? (n | ~(int32_t)1023) : (n & 1023);
}

/* Si extbg = 1, devuelve la capa BG2 (7 bits + prioridad). */
static void render_mode7(uint16_t v, uint8_t extbg, uint8_t zlo, uint8_t zhi,
                         uint8_t mosaic_bit)
{
    int32_t a = ppu.m7a, b = ppu.m7b, c = ppu.m7c, d = ppu.m7d;
    int32_t cx = ppu.m7x, cy = ppu.m7y;
    int32_t hx = m7_clip((int32_t)ppu.m7hofs - cx);
    int32_t vy = m7_clip((int32_t)ppu.m7vofs - cy);
    uint16_t line = (ppu.mosaic & mosaic_bit) ? mosaic_line(v) : v;
    int32_t y = (ppu.m7sel & 0x02) ? 255 - (int32_t)line : (int32_t)line;
    uint8_t repeat = ppu.m7sel >> 6;
    uint8_t hflip = ppu.m7sel & 0x01;
    int32_t px, py, ox, oy;
    uint16_t x;
    const uint8_t *vr = ppu.vram;

    ox = ((a * hx) & ~(int32_t)63) + ((b * vy) & ~(int32_t)63) +
         ((b * y) & ~(int32_t)63) + (cx << 8);
    oy = ((c * hx) & ~(int32_t)63) + ((d * vy) & ~(int32_t)63) +
         ((d * y) & ~(int32_t)63) + (cy << 8);

    if (hflip) {
        px = ox + a * 255;
        py = oy + c * 255;
        a = -a;
        c = -c;
    } else {
        px = ox;
        py = oy;
    }

    for (x = 0; x < 256; x++, px += a, py += c) {
        int32_t tx = px >> 8, ty = py >> 8;
        uint8_t tile, col;
        if ((tx | ty) & ~(int32_t)1023) {
            if (repeat == 2) { lay_z[x] = 0; continue; }
            if (repeat == 3) {
                col = vr[((((uint16_t)ty & 7) << 3) | ((uint16_t)tx & 7)) * 2 + 1];
                goto have_color;
            }
        }
        tile = vr[(uint16_t)(((((uint16_t)ty & 1023) >> 3) << 7) |
                             (((uint16_t)tx & 1023) >> 3)) << 1];
        col = vr[(uint16_t)((((uint16_t)tile << 6) | (((uint16_t)ty & 7) << 3) |
                             ((uint16_t)tx & 7)) << 1) + 1];
    have_color:
        if (extbg) {
            lay_col[x] = col & 0x7F;
            lay_z[x] = (col & 0x7F) ? ((col & 0x80) ? zhi : zlo) : 0;
        } else {
            lay_col[x] = col;
            lay_z[x] = col ? zlo : 0;
        }
    }

    if ((ppu.mosaic & mosaic_bit) && (ppu.mosaic >> 4))
        apply_mosaic_h();
}

/* ------------------------------------------------------------------ */
/* Sprites                                                             */
/* ------------------------------------------------------------------ */

static void render_sprites(uint16_t v, const uint8_t *zobj)
{
    uint8_t size = ppu.obsel >> 5;
    uint16_t base = (uint16_t)((ppu.obsel & 7) << 14);
    uint16_t gap = (uint16_t)((((ppu.obsel >> 3) & 3) + 1) << 13);
    uint8_t first = ppu.oam_prio_rot ? (uint8_t)((ppu.oam_addr >> 2) & 0x7F) : 0;
    uint8_t list[32];
    uint8_t count = 0;
    uint8_t n;
    uint8_t px[8];

    memset(lay_z, 0, 256);

    /* Evaluación: hasta 32 sprites en la línea, en orden de OAM */
    for (n = 0; n < 128; n++) {
        uint8_t i = (uint8_t)((first + n) & 0x7F);
        const uint8_t *o = &ppu.oam[i * 4];
        uint8_t hi = (uint8_t)(ppu.oam[0x200 + (i >> 2)] >> ((i & 3) * 2));
        uint8_t big = (hi >> 1) & 1;
        int16_t x = (int16_t)(o[0] | ((hi & 1) << 8));
        uint8_t w = obj_w[size][big], h = obj_h[size][big];
        uint8_t row = (uint8_t)(v - 1 - o[1]);
        if (x >= 256) x -= 512;
        if (row >= h) continue;
        if (x <= -(int16_t)w && x != -256) continue;
        if (x == -256 && w <= 0) continue;
        if (count == 32) {
            ppu.stat77 |= 0x40; /* Range over */
            break;
        }
        list[count++] = i;
    }

    /* Se dibujan de atrás hacia delante: el de índice menor queda encima */
    while (count--) {
        uint8_t i = list[count];
        const uint8_t *o = &ppu.oam[i * 4];
        uint8_t hi = (uint8_t)(ppu.oam[0x200 + (i >> 2)] >> ((i & 3) * 2));
        uint8_t big = (hi >> 1) & 1;
        int16_t x = (int16_t)(o[0] | ((hi & 1) << 8));
        uint8_t w = obj_w[size][big], h = obj_h[size][big];
        uint8_t attr = o[3];
        uint8_t row = (uint8_t)(v - 1 - o[1]);
        uint8_t pal = (uint8_t)(128 + ((attr >> 1) & 7) * 16);
        uint8_t z = zobj[(attr >> 4) & 3];
        uint8_t hf = (attr >> 6) & 1;
        uint8_t tiles = w >> 3, t;
        uint16_t tbase = (attr & 1) ? (uint16_t)(base + gap) : base;

        if (x >= 256) x -= 512;
        if (attr & 0x80) row = (uint8_t)(h - 1 - row);

        for (t = 0; t < tiles; t++) {
            uint8_t tc = hf ? (uint8_t)(tiles - 1 - t) : t;
            uint8_t lo = (uint8_t)((o[2] + tc) & 0x0F);
            uint8_t hn = (uint8_t)(((o[2] >> 4) + (row >> 3)) & 0x0F);
            uint16_t addr = (uint16_t)(tbase + (((uint16_t)hn << 4) | lo) * 32 +
                                       (row & 7) * 2);
            int16_t sx = x + t * 8;
            uint8_t k;
            if (sx <= -8 || sx >= 256)
                continue;
            decode_row(addr, 4, hf, px);
            for (k = 0; k < 8; k++, sx++) {
                if (sx < 0 || sx >= 256 || !px[k])
                    continue;
                lay_col[sx] = (uint8_t)(pal + px[k]);
                lay_z[sx] = z;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Bucle de líneas                                                     */
/* ------------------------------------------------------------------ */

void ppu_frame_start(void)
{
    ppu.stat77 &= 0x3F;
    ppu.field ^= 1;
}

void ppu_vblank_start(void)
{
    /* Al entrar en V-blank se recarga la dirección de OAM */
    if (!(ppu.inidisp & 0x80))
        ppu.oam_addr = (uint16_t)(ppu.oam_reload << 1);
}

void ppu_render_line(uint16_t v)
{
    uint8_t mode = ppu.bgmode & 7;
    uint8_t *dst;
    uint8_t n;
    const uint8_t (*zbg)[2];
    const uint8_t *zobj;

    if (v == 0 || v > ppu_visible_lines() || !out_base)
        return;
    dst = out_base + (uint16_t)(v - 1) * out_pitch;

    if (ppu.inidisp & 0x80) { /* Forced blank */
        memset(dst, 0, 256);
        return;
    }

    memset(line_col, 0, 256);
    memset(line_z, 0, 256);

    if (mode == 7) {
        zobj = z_mode7_obj;
        if (ppu.tm & 0x01) {
            render_mode7(v, 0, 3, 3, 0x01);
            compose(layer_window(0));
        }
        if ((ppu.setini & 0x40) && (ppu.tm & 0x02)) {
            render_mode7(v, 1, 1, 5, 0x02);
            compose(layer_window(1));
        }
    } else {
        if (mode == 0) {
            zbg = z_mode0;
            zobj = z_mode0_obj;
        } else if (mode == 1) {
            zbg = z_mode1;
            zobj = z_mode1_obj;
        } else {
            zbg = z_mode2;
            zobj = z_mode2_obj;
        }
        for (n = 0; n < 4; n++) {
            uint8_t bpp = mode_bpp[mode][n];
            uint8_t zlo, zhi;
            if (!bpp || !(ppu.tm & (1 << n)))
                continue;
            zlo = zbg[n][0];
            zhi = zbg[n][1];
            /* Modo 1 con BG3 prioritario (BGMODE bit 3): BG3 alta va delante de todo */
            if (mode == 1 && n == 2 && (ppu.bgmode & 0x08))
                zhi = 11;
            render_bg(n, v, bpp, zlo, zhi, mode == 0 ? (uint8_t)(n * 32) : 0);
            compose(layer_window(n));
        }
    }

    if (ppu.tm & 0x10) {
        render_sprites(v, zobj);
        compose(layer_window(4));
    }

    memcpy(dst, line_col, 256);
}
