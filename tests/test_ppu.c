/*
 * Tests de la PPU en PC: registros, casos de render con píxeles conocidos y
 * captura de las escenas de demostración en tests/out/modoN.ppm.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu/ppu.h"
#include "ppu/ppu_demo.h"

static uint8_t vram[PPU_VRAM_SIZE];
static uint8_t screen[256 * 240];
static int failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            printf("FALLO %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            failures++;                                   \
        }                                                 \
    } while (0)

static void w16(uint8_t reg, uint16_t v)
{
    ppu_write(reg, (uint8_t)v);
    ppu_write(reg, (uint8_t)(v >> 8));
}

static void vaddr(uint16_t a, uint8_t vmain)
{
    ppu_write(0x15, vmain);
    ppu_write(0x16, (uint8_t)a);
    ppu_write(0x17, (uint8_t)(a >> 8));
}

static void vword(uint16_t d)
{
    ppu_write(0x18, (uint8_t)d);
    ppu_write(0x19, (uint8_t)(d >> 8));
}

static void render_frame(void)
{
    uint16_t v;
    ppu_frame_start();
    for (v = 1; v <= ppu_visible_lines(); v++)
        ppu_render_line(v);
    ppu_vblank_start();
}

static uint8_t px(int x, int y) { return screen[y * 256 + x]; }

static void save_ppm(const char *path, uint16_t lines)
{
    uint16_t pal[256];
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) {
        printf("No se puede escribir %s\n", path);
        failures++;
        return;
    }
    ppu_build_palette(pal);
    fprintf(f, "P6\n256 %u\n255\n", lines);
    for (i = 0; i < 256 * lines; i++) {
        uint16_t c = pal[screen[i]];
        uint8_t rgb[3] = {(uint8_t)(((c >> 10) & 31) << 3),
                          (uint8_t)(((c >> 5) & 31) << 3),
                          (uint8_t)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* ---------------------------------------------------------------- */

static void test_vram_regs(void)
{
    uint8_t lo, hi;
    ppu_reset();
    /* Escritura con incremento tras el byte alto */
    vaddr(0x1234, 0x80);
    vword(0xBEEF);
    vword(0xCAFE);
    CHECK(vram[0x2468] == 0xEF && vram[0x2469] == 0xBE, "VRAM palabra 1");
    CHECK(vram[0x246A] == 0xFE && vram[0x246B] == 0xCA, "VRAM palabra 2");
    /* Lectura con prefetch */
    vaddr(0x1234, 0x80);
    lo = ppu_read(0x39);
    hi = ppu_read(0x3A);
    CHECK(lo == 0xEF && hi == 0xBE, "lectura VRAM %02X%02X", hi, lo);
    /* Como en el hardware, el prefetch se recarga antes de incrementar:
     * la segunda lectura repite la palabra y luego avanza. */
    lo = ppu_read(0x39);
    hi = ppu_read(0x3A);
    CHECK(lo == 0xEF && hi == 0xBE, "lectura VRAM 2 %02X%02X", hi, lo);
    lo = ppu_read(0x39);
    hi = ppu_read(0x3A);
    CHECK(lo == 0xFE && hi == 0xCA, "lectura VRAM 3 %02X%02X", hi, lo);
    /* Incremento de 32 */
    vaddr(0x0000, 0x81);
    vword(0x1111);
    vword(0x2222);
    CHECK(vram[0x40] == 0x22 && vram[0x41] == 0x22, "incremento 32");
    /* Incremento tras el byte bajo (solo $2118) */
    vaddr(0x0100, 0x00);
    ppu_write(0x18, 0xAA);
    ppu_write(0x18, 0xBB);
    CHECK(vram[0x200] == 0xAA && vram[0x202] == 0xBB, "incremento por byte bajo");
    /* Remapeo de dirección (modo 1: 8 bits) */
    vaddr(0x0021, 0x84); /* aaaaaaaaBBBccccc: B=1 c=1 -> c<<3|B = 9 */
    vword(0x7777);
    CHECK(vram[0x09 * 2] == 0x77, "remapeo de VRAM");
}

static void test_cgram_oam(void)
{
    uint8_t a, b;
    ppu_reset();
    ppu_write(0x21, 5);
    ppu_write(0x22, 0x34);
    ppu_write(0x22, 0xF2); /* bit 7 se ignora */
    CHECK(ppu.cgram[5] == 0x7234, "CGRAM %04X", ppu.cgram[5]);
    ppu_write(0x21, 5);
    a = ppu_read(0x3B);
    b = ppu_read(0x3B);
    CHECK(a == 0x34 && (b & 0x7F) == 0x72, "lectura CGRAM");

    ppu_write(0x02, 0x01);
    ppu_write(0x03, 0x00);
    ppu_write(0x04, 0x11);
    CHECK(ppu.oam[2] == 0x00, "OAM espera al segundo byte");
    ppu_write(0x04, 0x22);
    CHECK(ppu.oam[2] == 0x11 && ppu.oam[3] == 0x22, "OAM par");
    ppu_write(0x02, 0x00);
    ppu_write(0x03, 0x01); /* tabla alta */
    ppu_write(0x04, 0x5A);
    CHECK(ppu.oam[0x200] == 0x5A, "OAM tabla alta");
    ppu_write(0x02, 0x01);
    ppu_write(0x03, 0x00);
    CHECK(ppu_read(0x38) == 0x11 && ppu_read(0x38) == 0x22, "lectura OAM");
}

static void test_mpy_scroll(void)
{
    int32_t r;
    ppu_reset();
    w16(0x1B, (uint16_t)-300); /* M7A */
    ppu_write(0x1C, 0x00);
    ppu_write(0x1C, 0x05);     /* M7B: el último byte es el multiplicador */
    r = ppu_read(0x34) | (ppu_read(0x35) << 8) | (ppu_read(0x36) << 16);
    if (r & 0x800000) r -= 0x1000000;
    CHECK(r == -1500, "MPY = %d", r);

    /* Latch de scroll: dos escrituras -> 10 bits */
    ppu_write(0x0F, 0x23);
    ppu_write(0x0F, 0x01);
    CHECK(ppu.bg[1].hofs == 0x123, "BG2HOFS %03X", ppu.bg[1].hofs);
    ppu_write(0x10, 0xFF);
    ppu_write(0x10, 0x03);
    CHECK(ppu.bg[1].vofs == 0x3FF, "BG2VOFS %03X", ppu.bg[1].vofs);
}

/* Tile 4bpp de un color en la base de caracteres `chr_w` (palabras). */
static void solid_tile(uint16_t chr_w, uint16_t tile, uint8_t bpp, uint8_t color)
{
    uint8_t p, y;
    for (y = 0; y < 8; y++) {
        for (p = 0; p < bpp; p += 2) {
            uint8_t lo = (color >> p) & 1 ? 0xFF : 0x00;
            uint8_t hi = (color >> (p + 1)) & 1 ? 0xFF : 0x00;
            vaddr((uint16_t)(chr_w + tile * 4 * bpp + p * 4 + y), 0x80);
            vword((uint16_t)(lo | (hi << 8)));
        }
    }
}

static void test_render_mode1(void)
{
    uint16_t i;
    ppu_reset();
    ppu_write(0x00, 0x0F);
    ppu_write(0x05, 0x01);
    ppu_write(0x07, 0x00);  /* BG1 mapa en 0 */
    ppu_write(0x08, 0x04);  /* BG2 mapa en $0400 */
    ppu_write(0x0B, 0x22);  /* chr BG1/BG2 en $2000 */
    solid_tile(0x2000, 1, 4, 5);
    solid_tile(0x2000, 2, 4, 9);
    /* BG1: tile 1 paleta 2 en (1,0); BG2: tile 2 paleta 3 cubre todo */
    vaddr(0x0001, 0x80);
    vword(1 | (2 << 10));
    vaddr(0x0400, 0x80);
    for (i = 0; i < 1024; i++)
        vword(2 | (3 << 10));
    ppu_write(0x2C, 0x03);
    render_frame();
    CHECK(px(8, 0) == 2 * 16 + 5, "BG1 encima de BG2: %u", px(8, 0));
    CHECK(px(0, 0) == 3 * 16 + 9, "BG2 visible: %u", px(0, 0));

    /* Scroll horizontal de 3: el tile de BG1 empieza en x=5 */
    w16(0x0D, 3);
    render_frame();
    CHECK(px(4, 0) == 3 * 16 + 9 && px(5, 0) == 2 * 16 + 5, "scroll fino");

    /* Prioridad: BG2 con bit de prioridad pasa delante de BG1 sin él */
    vaddr(0x0400, 0x80);
    for (i = 0; i < 1024; i++)
        vword(2 | (3 << 10) | 0x2000);
    render_frame();
    CHECK(px(5, 0) == 3 * 16 + 9, "prioridad BG2 alta > BG1 baja");

    /* Ventana: recorta BG2 entre x=0 y x=3 en la pantalla principal */
    ppu_write(0x23, 0x20);  /* BG2: ventana 1 activa */
    ppu_write(0x26, 0);
    ppu_write(0x27, 3);
    ppu_write(0x2E, 0x02);
    render_frame();
    CHECK(px(0, 0) == 0 && px(4, 0) == 3 * 16 + 9, "ventana");
    ppu_write(0x2E, 0x00);

    /* Mosaico 4x4 en BG1 */
    vaddr(0x0400, 0x80);
    for (i = 0; i < 1024; i++)
        vword(0);
    w16(0x0D, 2); /* el tile de BG1 empieza en x=6 */
    ppu_write(0x06, 0x31);
    render_frame();
    CHECK(px(6, 0) == 0 && px(7, 0) == 0 && px(8, 0) == 2 * 16 + 5,
          "mosaico: %u %u %u", px(6, 0), px(7, 0), px(8, 0));
    ppu_write(0x06, 0x00);

    /* Forced blank */
    ppu_write(0x00, 0x80);
    render_frame();
    CHECK(px(8, 0) == 0, "forced blank");
}

static void test_tile16_and_bigmap(void)
{
    ppu_reset();
    ppu_write(0x00, 0x0F);
    ppu_write(0x05, 0x11);  /* modo 1, BG1 con tiles de 16x16 */
    ppu_write(0x07, 0x03);  /* 64x64 */
    ppu_write(0x0B, 0x02);
    solid_tile(0x2000, 0x10, 4, 1);
    solid_tile(0x2000, 0x11, 4, 2);
    solid_tile(0x2000, 0x20, 4, 3);
    solid_tile(0x2000, 0x21, 4, 4);
    /* Celda (0,0) tile $10: sub-tiles $10, $11, $20, $21 */
    vaddr(0x0000, 0x80);
    vword(0x10);
    /* Pantalla 3 (abajo-derecha) en la celda (32,32) */
    vaddr(0x0C00, 0x80);
    vword(0x10 | 0x4000); /* volteo horizontal */
    ppu_write(0x2C, 0x01);
    render_frame();
    CHECK(px(0, 0) == 1 && px(8, 0) == 2 && px(0, 8) == 3 && px(8, 8) == 4,
          "tile 16x16: %u %u %u %u", px(0, 0), px(8, 0), px(0, 8), px(8, 8));
    w16(0x0D, 512);
    w16(0x0E, 512);
    render_frame();
    CHECK(px(0, 0) == 2 && px(8, 0) == 1, "mapa 64x64 + volteo: %u %u",
          px(0, 0), px(8, 0));
}

static void test_sprites(void)
{
    ppu_reset();
    ppu_write(0x00, 0x0F);
    ppu_write(0x05, 0x01);
    ppu_write(0x01, 0x03);  /* base OBJ $6000, 8/16 */
    solid_tile(0x6000, 0, 4, 7);
    solid_tile(0x6000, 1, 4, 8);
    /* Sprite 0: x=10, y=20, tile 0, paleta 1 */
    ppu_write(0x02, 0);
    ppu_write(0x03, 0);
    ppu_write(0x04, 10);
    ppu_write(0x04, 20);
    ppu_write(0x04, 0);
    ppu_write(0x04, 0x02);
    /* Sprite 1 solapado (detrás del 0 por índice): tile 1, paleta 2 */
    ppu_write(0x04, 14);
    ppu_write(0x04, 20);
    ppu_write(0x04, 1);
    ppu_write(0x04, 0x34);
    ppu_write(0x2C, 0x10);
    render_frame();
    CHECK(px(10, 20) == 128 + 16 + 7, "sprite: %u", px(10, 20));
    CHECK(px(9, 20) == 0 && px(10, 19) == 0, "borde del sprite");
    CHECK(px(14, 20) == 128 + 16 + 7, "índice menor delante: %u", px(14, 20));
    CHECK(px(19, 27) == 128 + 32 + 8, "sprite 1: %u", px(19, 27));

    /* x negativo (bit 9 en la tabla alta) */
    ppu_write(0x02, 0);
    ppu_write(0x03, 0);
    ppu_write(0x04, 0xFC); /* x = -4 (con el bit 9 de la tabla alta) */
    ppu_write(0x04, 20);
    ppu_write(0x02, 0);
    ppu_write(0x03, 1);
    ppu_write(0x04, 0x01);
    render_frame();
    CHECK(px(0, 20) == 128 + 16 + 7 && px(3, 20) == 128 + 16 + 7 && px(4, 20) == 0,
          "x negativo");
}

static void test_mode7(void)
{
    uint16_t i;
    ppu_reset();
    ppu_write(0x00, 0x0F);
    ppu_write(0x05, 0x07);
    /* Mapa: celda (1,0) = tile 1; tile 1 entero color 0x42 */
    vaddr(0x0001, 0x00);
    ppu_write(0x18, 1);
    vaddr(64, 0x80);
    for (i = 0; i < 64; i++)
        ppu_write(0x19, 0x42);
    w16(0x1B, 0x0100);
    w16(0x1C, 0);
    w16(0x1D, 0);
    w16(0x1E, 0x0100);
    ppu_write(0x2C, 0x01);
    /* Identidad: la pantalla (8..15, y) muestra el tile 1. Línea v -> y = v */
    render_frame();
    CHECK(px(8, 0) == 0x42 && px(7, 0) == 0, "modo 7 identidad: %u %u",
          px(7, 0), px(8, 0));
    /* Escala 2x (A = D = 0.5): el tile 1 aparece en x = 16..31 */
    w16(0x1B, 0x0080);
    w16(0x1E, 0x0080);
    render_frame();
    CHECK(px(16, 0) == 0x42 && px(31, 0) == 0x42 && px(15, 0) == 0 &&
          px(32, 0) == 0, "modo 7 escala");
    /* Fuera del mapa con repeat=2 -> transparente; con repeat=0 se repite */
    w16(0x1B, 0x0100);
    w16(0x1E, 0x0100);
    w16(0x0D, 0x1FF8); /* -8 en 13 bits */
    ppu_write(0x1A, 0x80);
    render_frame();
    CHECK(px(16, 0) == 0x42 && px(0, 0) == 0, "repeat 2");
    ppu_write(0x1A, 0x00);
    render_frame();
    CHECK(px(16, 0) == 0x42, "repeat 0");
}

static void test_hires(void)
{
    uint16_t i;
    ppu_reset();
    ppu_write(0x00, 0x0F);
    ppu_write(0x05, 0x05);
    ppu_write(0x07, 0x00);
    ppu_write(0x0B, 0x02);
    /* Tile 0: columnas pares color 1, impares color 2; tile 1: color 3 */
    for (i = 0; i < 8; i++) {
        vaddr((uint16_t)(0x2000 + i), 0x80);
        vword(0x55AA); /* plano 0 = 10101010, plano 1 = 01010101 */
    }
    solid_tile(0x2000, 1, 4, 3);
    vaddr(0x0000, 0x80);
    for (i = 0; i < 1024; i++)
        vword(0);
    ppu_write(0x2C, 0x01);
    render_frame();
    /* Hires: 16 px por tile; se ven los impares -> color 2, luego tile 1 */
    CHECK(px(0, 0) == 2 && px(3, 0) == 2 && px(4, 0) == 3 && px(7, 0) == 3,
          "hires: %u %u %u %u", px(0, 0), px(3, 0), px(4, 0), px(7, 0));
}

static void test_opt(void)
{
    uint16_t i;
    ppu_reset();
    ppu_write(0x00, 0x0F);
    ppu_write(0x05, 0x02);
    ppu_write(0x07, 0x00);  /* BG1 mapa en 0 */
    ppu_write(0x09, 0x04);  /* BG3 mapa en $0400 */
    ppu_write(0x0B, 0x02);
    solid_tile(0x2000, 1, 4, 6);
    /* BG1: fila 1 de tiles = tile 1 */
    vaddr(32, 0x80);
    for (i = 0; i < 32; i++)
        vword(1);
    /* BG3 fila 1 (V): columna 2 de pantalla -> vofs = 8 para BG1 */
    vaddr(0x0400 + 32 + 1, 0x80);
    vword(8 | 0x2000);
    ppu_write(0x2C, 0x01);
    render_frame();
    CHECK(px(16, 0) == 6 && px(15, 0) == 0 && px(24, 0) == 0,
          "offset por tile: %u %u %u", px(15, 0), px(16, 0), px(24, 0));
}

static void dump_demos(void)
{
    uint8_t m;
    char path[64];
    for (m = 0; m < DEMO_SCENES; m++) {
        uint16_t f, v;
        demo_setup(m);
        for (f = 0; f <= 20; f++) {
            demo_frame(m, f);
            ppu_frame_start();
            for (v = 1; v <= ppu_visible_lines(); v++) {
                demo_line(m, v, f);
                ppu_render_line(v);
            }
            ppu_vblank_start();
        }
        snprintf(path, sizeof(path), "out/modo%u.ppm", m);
        save_ppm(path, 224);
    }
}

int main(int argc, char **argv)
{
    ppu_init(vram, screen, 256);
    test_vram_regs();
    test_cgram_oam();
    test_mpy_scroll();
    test_render_mode1();
    test_tile16_and_bigmap();
    test_sprites();
    test_mode7();
    test_hires();
    test_opt();
    if (argc > 1 && !strcmp(argv[1], "--dump"))
        dump_demos();
    if (failures) {
        printf("PPU: %d fallos\n", failures);
        return 1;
    }
    printf("PPU: todos los tests OK\n");
    return 0;
}
