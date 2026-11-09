/*
 * Escenas de prueba de la PPU para los modos 0-7. Gráficos generados por
 * código: ladrillos, damas, bolas, rayas y degradados.
 */
#include "ppu_demo.h"
#include "ppu.h"

/* Mapa de VRAM de las escenas (direcciones en palabras) */
#define MAP_BG1 0x0000 /* 64x32 */
#define MAP_BG2 0x0800
#define MAP_BG3 0x0C00
#define MAP_BG4 0x1000
#define CHR_BG1 0x2000 /* nba 2 */
#define CHR_BG2 0x3000 /* nba 3 */
#define CHR_BG3 0x4000 /* nba 4 */
#define CHR_BG4 0x5000 /* nba 5 */
#define CHR_OBJ 0x6000 /* OBSEL base 3 */

#define HORIZON 64 /* Modo 7: línea donde empieza el suelo */
#define SPRITES 8

static const char *const names[DEMO_SCENES] = {
    "Modo 0: 4 BG 2bpp",       "Modo 1: 2x4bpp + 2bpp",
    "Modo 2: offset por tile", "Modo 3: 8bpp 256 colores",
    "Modo 4: 8bpp + OPT",      "Modo 5: hires 512",
    "Modo 6: hires + OPT",     "Modo 7: rotacion/escala",
};

const char *demo_name(uint8_t mode)
{
    return names[mode & 7];
}

/* ---------------------------------------------------------------- */
/* Utilidades: todo pasa por ppu_write() como en un juego            */
/* ---------------------------------------------------------------- */

static void vram_addr(uint16_t w, uint8_t vmain)
{
    ppu_write(0x15, vmain);
    ppu_write(0x16, (uint8_t)w);
    ppu_write(0x17, (uint8_t)(w >> 8));
}

static void vram_word(uint16_t d)
{
    ppu_write(0x18, (uint8_t)d);
    ppu_write(0x19, (uint8_t)(d >> 8));
}

static void reg16(uint8_t reg, uint16_t v)
{
    ppu_write(reg, (uint8_t)v);
    ppu_write(reg, (uint8_t)(v >> 8));
}

static void cg_set(uint8_t idx, uint16_t c)
{
    ppu_write(0x21, idx);
    ppu_write(0x22, (uint8_t)c);
    ppu_write(0x22, (uint8_t)(c >> 8));
}

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(r | ((uint16_t)g << 5) | ((uint16_t)b << 10));
}

/* Seno en 8.8 (256 = 1.0), 256 pasos por vuelta */
static const uint8_t sin_q[65] = {
    0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92, 98,
    104, 109, 115, 121, 126, 132, 137, 142, 147, 152, 157, 162, 167, 172,
    177, 181, 185, 190, 194, 198, 202, 206, 209, 213, 216, 220, 223, 226,
    229, 231, 234, 237, 239, 241, 243, 245, 247, 248, 250, 251, 252, 253,
    254, 255, 255, 256 - 1, 256 - 1, 256 - 1,
};

static int16_t isin(uint8_t a)
{
    uint8_t q = a & 63;
    int16_t v;
    switch (a >> 6) {
    case 0: v = sin_q[q]; break;
    case 1: v = sin_q[64 - q]; break;
    case 2: v = -(int16_t)sin_q[q]; break;
    default: v = -(int16_t)sin_q[64 - q]; break;
    }
    return v;
}

static int16_t icos(uint8_t a)
{
    return isin((uint8_t)(a + 64));
}

/* ---------------------------------------------------------------- */
/* Gráficos                                                           */
/* ---------------------------------------------------------------- */

/* Patrones de tile: 0 vacío, 1 ladrillo, 2 damas, 3 bola, 4 rayas,
 * 5 degradado; 6/7 = mitades izquierda/derecha de un tile de 16 px. */
static uint8_t pattern(uint8_t t, uint8_t x, uint8_t y, uint8_t max)
{
    switch (t) {
    case 1:
        if (x == 0 || y == 0) return 1;
        if (x == 7 || y == 7) return (uint8_t)(max > 3 ? 2 : 1);
        return max;
    case 2:
        return (uint8_t)((((x >> 1) ^ (y >> 1)) & 1) ? 2 : 3 > max ? max : 3);
    case 3: {
        int16_t dx = 2 * x - 7, dy = 2 * y - 7;
        int16_t d2 = dx * dx + dy * dy;
        if (d2 > 50) return 0;
        return (uint8_t)(max - (d2 * (max - 1)) / 51);
    }
    case 4:
        return (uint8_t)(((x + y) & 4) ? 0 : (max > 3 ? 1 + ((x + y) & 3) : 1));
    case 5:
        if (max == 255) return (uint8_t)(1 + y * 8 + x);
        return (uint8_t)(1 + ((x + y) * (max - 1)) / 14);
    case 6: case 7: {
        /* Rombo de 16x8 en un tile de 16 de ancho (modo hires) */
        int16_t hx = (int16_t)(t == 7 ? x + 8 : x);
        int16_t d = (hx > 7 ? hx - 8 : 7 - hx) * 4 + (y > 3 ? y - 4 : 3 - y) * 8;
        if (d > 30) return 0;
        return (uint8_t)(1 + ((hx & 1) ? (max - 1) : (max - 1) / 2));
    }
    default:
        return 0;
    }
}

/* Escribe el tile `t` (con el patrón `pat`) en formato planar. */
static void put_tile(uint16_t chr, uint8_t bpp, uint16_t t, uint8_t pat)
{
    uint8_t max = (uint8_t)((1u << bpp) - 1);
    uint16_t base = (uint16_t)(chr + t * 4 * bpp);
    uint8_t x, y, b;

    for (y = 0; y < 8; y++) {
        uint8_t p[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (x = 0; x < 8; x++) {
            uint8_t c = pattern(pat, x, y, max);
            for (b = 0; b < bpp; b++)
                if (c & (1 << b))
                    p[b] |= (uint8_t)(0x80 >> x);
        }
        for (b = 0; b < bpp; b += 2) {
            vram_addr((uint16_t)(base + b * 4 + y), 0x80);
            vram_word((uint16_t)(p[b] | (p[b + 1] << 8)));
        }
    }
}

static void put_tiles(uint16_t chr, uint8_t bpp)
{
    uint8_t t;
    for (t = 0; t < 8; t++)
        put_tile(chr, bpp, t, t);
}

/* Bola de 16x16 para sprites (tiles 0, 1, 16, 17 de la tabla de OBJ) */
static void put_sprite_ball(void)
{
    uint8_t tx, ty, x, y, b;
    for (ty = 0; ty < 2; ty++)
        for (tx = 0; tx < 2; tx++) {
            uint16_t base = (uint16_t)(CHR_OBJ + (ty * 16 + tx) * 16);
            for (y = 0; y < 8; y++) {
                uint8_t p[4] = {0, 0, 0, 0};
                for (x = 0; x < 8; x++) {
                    int16_t dx = 2 * (tx * 8 + x) - 15, dy = 2 * (ty * 8 + y) - 15;
                    int16_t d2 = dx * dx + dy * dy;
                    /* Brillo descentrado para que parezca una esfera */
                    int16_t ex = dx + 5, ey = dy + 5;
                    int16_t e2 = ex * ex + ey * ey;
                    uint8_t c;
                    if (d2 > 225)
                        c = 0;
                    else if (d2 > 180)
                        c = 1; /* Borde oscuro */
                    else
                        c = (uint8_t)(15 - (e2 > 390 ? 13 : e2 / 30));
                    for (b = 0; b < 4; b++)
                        if (c & (1 << b))
                            p[b] |= (uint8_t)(0x80 >> x);
                }
                vram_addr((uint16_t)(base + y), 0x80);
                vram_word((uint16_t)(p[0] | (p[1] << 8)));
                vram_addr((uint16_t)(base + 8 + y), 0x80);
                vram_word((uint16_t)(p[2] | (p[3] << 8)));
            }
        }
}

/* Paleta: 8 tonos x 16 brillos, repetida en BG (0-127) y OBJ (128-255) */
static void put_palette(void)
{
    static const uint8_t hue[8][3] = {
        {31, 6, 6},  {31, 18, 4}, {30, 28, 6}, {8, 28, 8},
        {6, 26, 28}, {8, 12, 31}, {20, 8, 30}, {31, 10, 22},
    };
    uint16_t i;
    for (i = 1; i < 256; i++) {
        const uint8_t *h = hue[(i >> 4) & 7];
        uint8_t k = (uint8_t)(i & 15);
        cg_set((uint8_t)i, rgb((uint8_t)(h[0] * (k + 3) / 18),
                               (uint8_t)(h[1] * (k + 3) / 18),
                               (uint8_t)(h[2] * (k + 3) / 18)));
    }
    cg_set(0, rgb(2, 3, 9));        /* Fondo */
    cg_set(255, rgb(31, 31, 31));   /* Texto de la demo */
}

/* ---------------------------------------------------------------- */
/* Mapas                                                              */
/* ---------------------------------------------------------------- */

static void fill_map(uint16_t map, uint8_t screens, uint8_t layer, uint8_t wide8)
{
    uint16_t tx, ty;
    for (ty = 0; ty < 32; ty++) {
        vram_addr((uint16_t)(map + ty * 32), 0x80);
        for (tx = 0; tx < 32; tx++) {
            uint16_t e = 0;
            switch (layer) {
            case 0: /* BG1: ladrillos y damas (degradado en 8bpp), con huecos */
                if ((((tx >> 1) + (ty >> 1)) % 3) == 0)
                    break;
                e = (uint16_t)(((tx ^ ty) & 1) ? 1 : (wide8 ? 5 : 2)) |
                    (uint16_t)((((tx >> 2) + (ty >> 2)) & 7) << 10);
                if (ty >= 20) e |= 0x2000;
                break;
            case 1: /* BG2: rayas diagonales semitransparentes */
                if (((tx >> 2) + (ty >> 2)) & 1)
                    e = 4 | (2 << 10) | (((tx + ty) & 8) ? 0x4000 : 0);
                break;
            case 2: /* BG3: barra superior (prioridad alta) y bolas */
                if (ty < 3)
                    e = 1 | (1 << 10) | 0x2000;
                else if ((tx % 6) == 3 && (ty % 5) == 2)
                    e = 3 | (3 << 10);
                break;
            default: /* BG4: degradados dispersos */
                if (((tx + ty * 3) % 7) == 0)
                    e = 5 | (5 << 10);
                break;
            }
            vram_word(e);
        }
    }
    if (screens == 2) { /* Segunda pantalla de BG1 (64x32): otra paleta */
        for (ty = 0; ty < 32; ty++) {
            vram_addr((uint16_t)(map + 0x400 + ty * 32), 0x80);
            for (tx = 0; tx < 32; tx++)
                vram_word((uint16_t)(((tx + ty) & 1 ? 2 : 1) | (6 << 10)));
        }
    }
}

/* Mapa de BG1 en modo hires: rombos de 16 px (tiles 6/7 consecutivos) */
static void fill_map_hires(uint16_t map)
{
    uint16_t tx, ty;
    for (ty = 0; ty < 32; ty++) {
        vram_addr((uint16_t)(map + ty * 32), 0x80);
        for (tx = 0; tx < 32; tx++) {
            uint16_t e = ((tx + ty) & 1) ? (uint16_t)(6 | ((ty & 7) << 10))
                                         : (uint16_t)(1 | (4 << 10));
            vram_word(e);
        }
    }
    /* Segunda pantalla igual */
    for (ty = 0; ty < 32; ty++) {
        vram_addr((uint16_t)(map + 0x400 + ty * 32), 0x80);
        for (tx = 0; tx < 32; tx++)
            vram_word((uint16_t)(6 | (((tx + ty) & 7) << 10)));
    }
}

/* Modo 7: mapa de 128x128 en bytes bajos y tiles 8bpp en bytes altos */
static void setup_mode7(void)
{
    uint16_t i, tx, ty;
    uint8_t t, x, y;

    vram_addr(0, 0x00); /* Incremento tras $2118 (byte bajo) */
    for (ty = 0; ty < 128; ty++)
        for (tx = 0; tx < 128; tx++) {
            uint8_t tile;
            if ((tx & 15) == 0 || (ty & 15) == 0)
                tile = 3;
            else
                tile = (uint8_t)((((tx >> 2) ^ (ty >> 2)) & 1) ? 1 : 2);
            ppu_write(0x18, tile);
        }

    vram_addr(0, 0x80); /* Incremento tras $2119 (byte alto) */
    for (t = 0; t < 4; t++)
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++) {
                uint8_t c;
                switch (t) {
                case 1: c = (uint8_t)(0x30 + 4 + ((x + y) >> 1)); break; /* verde */
                case 2: c = (uint8_t)(0x20 + 6 + ((x ^ y) & 3)); break;  /* amarillo */
                case 3: c = (uint8_t)(0x50 + 12 + (x & 1)); break;       /* azul */
                default: c = 0; break;
                }
                ppu_write(0x19, c);
            }
    (void)i;
    ppu_write(0x1A, 0x80); /* Fuera del mapa: transparente */
}

/* ---------------------------------------------------------------- */
/* Escenas                                                            */
/* ---------------------------------------------------------------- */

void demo_setup(uint8_t mode)
{
    uint16_t i;
    mode &= 7;

    ppu_reset();
    ppu_write(0x00, 0x80); /* Forced blank mientras se carga */
    put_palette();

    /* OAM: todos los sprites fuera de pantalla, los 8 primeros grandes */
    ppu_write(0x02, 0);
    ppu_write(0x03, 0);
    for (i = 0; i < 128; i++) {
        ppu_write(0x04, 0);
        ppu_write(0x04, 0xF0);
        ppu_write(0x04, 0);
        ppu_write(0x04, 0);
    }
    for (i = 0; i < 32; i++)
        ppu_write(0x04, i < 2 ? 0xAA : 0x00);
    ppu_write(0x01, 0x03); /* 8x8/16x16, base $6000 */
    put_sprite_ball();

    if (mode == 7) {
        setup_mode7();
        ppu_write(0x05, 0x07);
        ppu_write(0x2C, 0x11);
    } else {
        static const uint8_t tm[7] = {0x1F, 0x17, 0x13, 0x13, 0x13, 0x13, 0x11};
        static const uint8_t bpp[7][4] = {
            {2, 2, 2, 2}, {4, 4, 2, 0}, {4, 4, 0, 0}, {8, 4, 0, 0},
            {8, 2, 0, 0}, {4, 2, 0, 0}, {4, 0, 0, 0},
        };
        static const uint16_t chr[4] = {CHR_BG1, CHR_BG2, CHR_BG3, CHR_BG4};
        uint8_t n;

        ppu_write(0x05, (uint8_t)(mode | (mode == 1 ? 0x08 : 0)));
        ppu_write(0x07, (MAP_BG1 >> 8) | 0x01); /* 64x32 */
        ppu_write(0x08, MAP_BG2 >> 8);
        ppu_write(0x09, MAP_BG3 >> 8);
        ppu_write(0x0A, MAP_BG4 >> 8);
        ppu_write(0x0B, 0x32);
        ppu_write(0x0C, 0x54);
        for (n = 0; n < 4; n++)
            if (bpp[mode][n])
                put_tiles(chr[n], bpp[mode][n]);
        if (mode == 5 || mode == 6)
            fill_map_hires(MAP_BG1);
        else
            fill_map(MAP_BG1, 2, 0, bpp[mode][0] == 8);
        fill_map(MAP_BG2, 1, 1, 0);
        if (mode < 2) {
            fill_map(MAP_BG3, 1, 2, 0);
            fill_map(MAP_BG4, 1, 3, 0);
        }
        ppu_write(0x2C, tm[mode]);
    }

    ppu_write(0x00, 0x0F); /* Pantalla encendida, brillo máximo */
}

void demo_frame(uint8_t mode, uint16_t frame)
{
    uint8_t i;
    mode &= 7;

    /* Sprites: bolas girando */
    ppu_write(0x02, 0);
    ppu_write(0x03, 0);
    for (i = 0; i < SPRITES; i++) {
        uint8_t a = (uint8_t)(frame * 2 + i * 32);
        int16_t x = 120 + (isin(a) * 90) / 256;
        int16_t y = 104 + (icos(a) * 70) / 256;
        ppu_write(0x04, (uint8_t)x);
        ppu_write(0x04, (uint8_t)y);
        ppu_write(0x04, 0x00);                               /* tile 0 */
        ppu_write(0x04, (uint8_t)(((i & 7) << 1) | ((i & 3) << 4)));
    }

    if (mode == 7)
        return;

    /* Scroll: BG1 en diagonal, BG2 en sentido contrario, BG3 fijo */
    reg16(0x0D, (uint16_t)(frame & 0x3FF));
    reg16(0x0E, (uint16_t)((frame >> 1) & 0x3FF));
    reg16(0x0F, (uint16_t)(-(int16_t)frame & 0x3FF));
    reg16(0x10, 0);
    reg16(0x13, (uint16_t)((frame >> 2) & 0x3FF));

    if (mode == 2 || mode == 4 || mode == 6) {
        /* Offset-per-tile: ondulación vertical por columnas en BG3 */
        uint8_t c;
        vram_addr((uint16_t)(MAP_BG3 + (mode == 4 ? 0 : 32)), 0x80);
        for (c = 0; c < 32; c++) {
            int16_t off = isin((uint8_t)(c * 16 + frame * 4)) / 16;
            uint16_t e = (uint16_t)((uint16_t)(off + ((frame >> 1) & 0x3FF)) & 0x3FF) |
                         0x2000;
            if (mode == 4)
                e |= 0x8000; /* Modo 4: bit 15 = desplazamiento vertical */
            vram_word(e);
        }
    }
}

void demo_line(uint8_t mode, uint16_t v, uint16_t frame)
{
    /* Equivalente a un HDMA: matriz del modo 7 distinta en cada línea */
    int16_t s, ca, sa;
    int16_t cx, cy;
    uint8_t ang;

    if ((mode & 7) != 7)
        return;
    if (v < HORIZON) {
        ppu_write(0x2C, 0x10); /* Cielo: solo sprites */
        return;
    }
    ppu_write(0x2C, 0x11);

    ang = (uint8_t)(frame);
    s = (int16_t)(46 * 256 / (int16_t)(v - HORIZON + 8)); /* escala 8.8 */
    ca = (int16_t)((int32_t)icos(ang) * s / 256);
    sa = (int16_t)((int32_t)isin(ang) * s / 256);
    cx = (int16_t)(512 + isin((uint8_t)(frame >> 1)) / 2);
    cy = (int16_t)(512 + icos((uint8_t)(frame >> 1)) / 2);

    reg16(0x1B, (uint16_t)ca);   /* A */
    reg16(0x1C, (uint16_t)sa);   /* B */
    reg16(0x1D, (uint16_t)-sa);  /* C */
    reg16(0x1E, (uint16_t)ca);   /* D */
    reg16(0x1F, (uint16_t)cx);   /* centro X */
    reg16(0x20, (uint16_t)cy);   /* centro Y */
    reg16(0x0D, (uint16_t)(cx - 128));
    reg16(0x0E, (uint16_t)(cy - 64 - (int16_t)v));
}
