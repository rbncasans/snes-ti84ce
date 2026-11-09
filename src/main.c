#include <graphx.h>
#include <keypadc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "bus.h"
#include "cpu/65c816.h"
#include "demo_rom.h"
#include "emu.h"
#include "mem/mem.h"
#include "mem/rom_ce.h"
#include "ppu/ppu.h"
#include "ppu/ppu_demo.h"
#include "input/joypad.h"

/* Se dibuja siempre directamente en pantalla (un solo búfer, nunca
 * gfx_SetDrawBuffer): la segunda mitad de la RAM del LCD queda libre para
 * la VRAM de la PPU (0xD52C00-0xD62BFF), el cartucho de autotest (antes de
 * arrancar la PPU) y una página de WRAM (0xD62C00, ver mem/rom_ce.c). */
#define LCD_RAM     ((uint8_t *)0xD40000)
#define LCD_BACK    (LCD_RAM + 320 * 240)
#define LCD_PALETTE ((uint16_t *)0xE30200)
/* Imagen SNES de 256x224 centrada en la pantalla de 320x240 */
#define SNES_OUT    (LCD_RAM + 8 * 320 + 32)
#define SNES_VRAM   LCD_BACK


#define MAX_ROMS 8

static cpu_t cpu;

static const char hexdig[] = "0123456789ABCDEF";

static void print_hex(uint32_t v, uint8_t digits)
{
    char buf[8];
    uint8_t i;
    buf[0] = '$';
    for (i = 0; i < digits; i++)
        buf[1 + i] = hexdig[(v >> (4 * (digits - 1 - i))) & 0xF];
    buf[1 + digits] = '\0';
    gfx_PrintString(buf);
}

static uint16_t bus16(uint32_t addr)
{
    return bus_read(addr) | ((uint16_t)bus_read(addr + 1) << 8);
}

static void wait_key(void)
{
    /* Esperar a soltar y volver a pulsar */
    do kb_Scan(); while (kb_AnyKey());
    do kb_Scan(); while (!kb_AnyKey());
}

static void screen(const char *title)
{
    gfx_FillScreen(255);
    gfx_SetTextFGColor(0);
    gfx_PrintStringXY(title, 8, 8);
    gfx_HorizLine(8, 18, 304);
}

/* Vuelve a la paleta de graphx tras usar la de la PPU */
static void restore_screen(void)
{
    gfx_SetDefaultPalette(gfx_8bpp);
    gfx_SetTextTransparentColor(0);
    gfx_SetTextBGColor(255);
    gfx_SetTextFGColor(0);
    gfx_SetColor(255);
}

static bool check(const char *label, uint32_t addr, uint16_t expected, uint8_t y)
{
    uint16_t got = bus16(addr);
    bool ok = got == expected;
    gfx_SetTextXY(8, y);
    gfx_PrintString(label);
    print_hex(got, 4);
    gfx_PrintString(ok ? "  OK" : "  FALLO");
    return ok;
}

/* ------------------------------------------------------------------ */
/* Autotest: CPU + mapa LoROM con un cartucho de 32 KB en la RAM del LCD */
/* ------------------------------------------------------------------ */

static uint8_t test_wram[MEM_PAGE_SIZE];

static void autotest(void)
{
    static mem_chunk_t chunk;
    mem_config_t cfg;
    uint8_t *cart = LCD_BACK;
    uint32_t steps = 0;
    bool ok = true;
    uint8_t i;

    memset(cart, 0xFF, MEM_CHUNK_SIZE);
    memcpy(cart, demo_rom, sizeof demo_rom);       /* $00:8000 */
    cart[0x7FFC] = DEMO_ROM_BASE & 0xFF;           /* Vector de reset $00:FFFC */
    cart[0x7FFD] = DEMO_ROM_BASE >> 8;

    chunk.data = cart;
    chunk.len = MEM_CHUNK_SIZE;
    chunk.fill = 0xFF;
    memset(&cfg, 0, sizeof cfg);
    cfg.map = MAP_LOROM;
    cfg.rom_size = MEM_CHUNK_SIZE;
    cfg.nchunks = 1;
    cfg.chunks = &chunk;
    /* Para el autotest basta con 8 KB de WRAM repetidos en las 16 páginas */
    memset(test_wram, 0, sizeof test_wram);
    for (i = 0; i < MEM_WRAM_PAGES; i++)
        cfg.wram[i] = test_wram;
    mem_init(&cfg);

    cpu_reset(&cpu);
    while (!cpu.stopped && steps < 100000) {
        cpu_step(&cpu);
        steps++;
    }

    screen("snes-ti84ce - autotest CPU + LoROM");
    ok &= check("Suma 1..100:   ", 0x7E0000, 0x13BA, 32);
    ok &= check("ADC decimal:   ", 0x7E0002, 0x5555, 44);
    ok &= check("JSR/RTS:       ", 0x7E0004, 0xBEEF, 56);
    ok &= check("Espejo $80:FFFC ", 0x80FFFC, DEMO_ROM_BASE, 68);
    ok &= cpu.stopped != 0;

    gfx_SetTextXY(8, 88);
    gfx_PrintString("Instrucciones: ");
    gfx_PrintUInt(steps, 1);
    gfx_PrintStringXY(ok ? "Resultado: TODO OK" : "Resultado: HAY FALLOS", 8, 104);
}

/* ------------------------------------------------------------------ */
/* Demo de la PPU: [<] [>] cambian de modo, [clear] sale                */
/* ------------------------------------------------------------------ */

static void ppu_demo(void)
{
    uint8_t mode = 1;
    uint16_t frame = 0;
    uint8_t fps10 = 0;
    clock_t t0 = clock();
    uint8_t frames = 0;
    bool held = false;

    memset(LCD_RAM, 0, 320 * 240);
    ppu_init(SNES_VRAM, SNES_OUT, 320);
    demo_setup(mode);

    for (;;) {
        uint16_t v;

        kb_Scan();
        if (kb_IsDown(kb_KeyClear))
            break;
        if (kb_IsDown(kb_KeyLeft) || kb_IsDown(kb_KeyRight)) {
            if (!held) {
                mode = (uint8_t)((mode + (kb_IsDown(kb_KeyRight) ? 1 : 7)) & 7);
                memset(LCD_RAM, 0, 320 * 240);
                demo_setup(mode);
                frame = 0;
            }
            held = true;
        } else {
            held = false;
        }

        demo_frame(mode, frame);
        ppu_frame_start();
        for (v = 1; v <= ppu_visible_lines(); v++) {
            demo_line(mode, v, frame);
            ppu_render_line(v);
        }
        ppu_vblank_start();
        if (ppu_palette_dirty) {
            ppu_build_palette(LCD_PALETTE);
            ppu_palette_dirty = 0;
        }

        if (++frames == 8) {
            clock_t t1 = clock();
            uint32_t dt = (uint32_t)(t1 - t0);
            fps10 = dt ? (uint8_t)((80u * CLOCKS_PER_SEC) / dt) : 0;
            t0 = t1;
            frames = 0;
        }
        /* Texto en el borde superior, con el color 255 (blanco en la demo) */
        gfx_SetColor(0);
        gfx_FillRectangle_NoClip(0, 0, 320, 8);
        gfx_SetTextFGColor(255);
        gfx_SetTextTransparentColor(254);
        gfx_SetTextBGColor(254);
        gfx_PrintStringXY(demo_name(mode), 2, 0);
        gfx_SetTextXY(250, 0);
        gfx_PrintUInt(fps10 / 10, 1);
        gfx_PrintChar('.');
        gfx_PrintUInt(fps10 % 10, 1);
        gfx_PrintString(" fps");
        frame++;
    }
    restore_screen();
}

/* ------------------------------------------------------------------ */
/* Prueba del mando: muestra los botones SNES pulsados                  */
/* ------------------------------------------------------------------ */

/* Nombres de los botones en el orden de los bits 15..4 del mando. */
static const char *const button_names[12] = {
    "B", "Y", "Select", "Start", "Arriba", "Abajo",
    "Izquierda", "Derecha", "A", "X", "L", "R",
};

static void draw_pad(uint16_t pad)
{
    uint8_t i;

    gfx_FillRectangle(0, 40, 320, 140);
    for (i = 0; i < 12; i++) {
        uint16_t bit = (uint16_t)(0x8000u >> i);
        gfx_SetTextFGColor((pad & bit) ? 224 : 181); /* rojo / gris */
        gfx_PrintStringXY(button_names[i], 16 + (i / 6) * 150, 48 + (i % 6) * 16);
    }
    gfx_SetTextFGColor(0);
    gfx_PrintStringXY("$4218/$4219 = ", 8, 160);
    print_hex(joypad_read(0x4218), 2);
    gfx_PrintString(" ");
    print_hex(joypad_read(0x4219), 2);
}

static void joypad_test(void)
{
    uint16_t last = 0xFFFF;

    screen("snes-ti84ce - prueba del mando");
    gfx_PrintStringXY("Pulsa [clear] para salir", 8, 24);
    joypad_reset();
    do kb_Scan(); while (kb_AnyKey());
    do {
        uint16_t pad = joypad_poll();
        joypad_auto_read(); /* lo que hace el bus en cada VBlank */
        if (pad != last) {
            draw_pad(pad);
            last = pad;
        }
    } while (!joypad_quit_requested());
}

/* ------------------------------------------------------------------ */
/* Selección y prueba de una ROM convertida                            */
/* ------------------------------------------------------------------ */

static int8_t choose_rom(char names[][9], uint8_t n)
{
    uint8_t sel = 0, i;
    bool redraw = true;

    for (;;) {
        if (redraw) {
            gfx_FillRectangle(0, 140, 320, 100);
            gfx_SetColor(255);
            gfx_PrintStringXY("ROMs encontradas ([2nd] carga, [clear] sale):", 8, 140);
            for (i = 0; i < n; i++) {
                gfx_PrintStringXY(i == sel ? "> " : "  ", 8, 154 + i * 10);
                gfx_PrintString(names[i]);
            }
            redraw = false;
        }
        do kb_Scan(); while (!kb_AnyKey());
        if (kb_IsDown(kb_KeyClear))
            return -1;
        if (kb_IsDown(kb_Key2nd) || kb_IsDown(kb_KeyEnter))
            return (int8_t)sel;
        if (kb_IsDown(kb_KeyDown) && sel + 1 < n) {
            sel++;
            redraw = true;
        }
        if (kb_IsDown(kb_KeyUp) && sel > 0) {
            sel--;
            redraw = true;
        }
        do kb_Scan(); while (kb_AnyKey());
    }
}

/* Bucle de la consola (src/emu.c) con la barra de estado de arriba.
 * Se dibuja un fotograma de cada FRAMESKIP + 1. */
#define FRAMESKIP 0

static void run_console(void)
{
    uint32_t frames = 0;
    uint8_t fps10 = 0, skip = 0;
    clock_t t0;

    memset(LCD_RAM, 0, 320 * 240);
    emu_init(SNES_VRAM, SNES_OUT, 320);

    t0 = clock();
    for (;;) {
        bool render = skip == 0;

        emu_frame(render);
        if (++skip > FRAMESKIP)
            skip = 0;
        if (ppu_palette_dirty) {
            ppu_build_palette(LCD_PALETTE);
            ppu_palette_dirty = 0;
        }
        frames++;

        if (emu_quit_requested())
            break;

        if ((frames & 15) == 0) {
            clock_t t1 = clock();
            uint32_t dt = (uint32_t)(t1 - t0);
            fps10 = dt ? (uint8_t)((160u * CLOCKS_PER_SEC) / dt) : 0;
            t0 = t1;
            emu_instr = 0;
        }
        if (!render)
            continue;
        /* Barra superior: PC y fotogramas emulados por segundo */
        gfx_SetColor(0);
        gfx_FillRectangle_NoClip(0, 0, 320, 8);
        gfx_SetTextFGColor(255);
        gfx_SetTextTransparentColor(254);
        gfx_SetTextBGColor(254);
        gfx_SetTextXY(2, 0);
        gfx_PrintString("PC ");
        print_hex(((uint32_t)emu_cpu.pb << 16) | emu_cpu.pc, 6);
        gfx_PrintString(emu_cpu.stopped ? " STP" : emu_cpu.waiting ? " WAI" : "");
        gfx_SetTextXY(250, 0);
        gfx_PrintUInt(fps10 / 10, 1);
        gfx_PrintChar('.');
        gfx_PrintUInt(fps10 % 10, 1);
        gfx_PrintString(" fps");
    }
    restore_screen();
}

static void run_rom(const char *name)
{
    static const char *map_name[3] = { "LoROM", "HiROM", "ExHiROM" };
    uint32_t hdr;
    uint16_t reset;
    int err;
    uint8_t y;

    screen("Cargando ROM...");
    err = rom_load(name);
    if (err != ROM_OK) {
        screen("Error al cargar la ROM");
        gfx_PrintStringXY(rom_error(err), 8, 32);
        if (err == ROM_ERR_CHUNK) {
            gfx_PrintStringXY("AppVar: ", 8, 44);
            gfx_PrintString(rom_info.missing);
        }
        gfx_PrintStringXY("Pulsa una tecla", 8, 220);
        wait_key();
        return;
    }

    screen(rom_info.title);
    gfx_PrintStringXY("Mapa: ", 8, 26);
    gfx_PrintString(map_name[rom_info.map]);
    gfx_PrintString(rom_info.fastrom ? " FastROM" : " SlowROM");
    gfx_PrintStringXY("ROM: ", 8, 38);
    gfx_PrintUInt(rom_info.rom_size / 1024, 1);
    gfx_PrintString(" KB   SRAM: ");
    gfx_PrintUInt(rom_info.sram_size / 1024, 1);
    gfx_PrintString(" KB");
    gfx_PrintStringXY("Trozos: ", 8, 50);
    gfx_PrintUInt(rom_info.stored, 1);
    gfx_PrintString(" guardados de ");
    gfx_PrintUInt(rom_info.nchunks, 1);
    gfx_PrintStringXY("WRAM 8K: ", 8, 62);
    gfx_PrintUInt(rom_info.wram_ps, 1);
    gfx_PrintString(" pixelShadow, ");
    gfx_PrintUInt(rom_info.wram_lcd, 1);
    gfx_PrintString(" LCD, ");
    gfx_PrintUInt(rom_info.wram_heap, 1);
    gfx_PrintString(" heap");

    /* Comprobación del mapa: la cabecera vista por la CPU en $00:FFC0 */
    hdr = 0x00FFC0;
    gfx_PrintStringXY("Titulo en $00:FFC0: ", 8, 80);
    for (y = 0; y < 21; y++) {
        char ch = (char)bus_read(hdr + y);
        gfx_PrintChar(ch >= 0x20 && ch < 0x7F ? ch : '.');
    }
    reset = bus16(0x00FFFC);
    gfx_PrintStringXY("Reset $00:FFFC = ", 8, 92);
    print_hex(reset, 4);
    gfx_PrintString("  opcode ");
    print_hex(bus_read(reset), 2);

    gfx_PrintStringXY("Pulsa una tecla para empezar", 8, 116);
    gfx_PrintStringXY("[clear] para salir durante el juego", 8, 128);
    wait_key();
    do kb_Scan(); while (kb_AnyKey());

    run_console();
    rom_unload();
}

static void load_rom_menu(void)
{
    static char names[MAX_ROMS][9];
    uint8_t n;
    int8_t sel;

    screen("snes-ti84ce - cargar ROM");
    n = rom_list(names, MAX_ROMS);
    if (n == 0) {
        gfx_PrintStringXY("No hay ROMs convertidas (AppVars SNCE).", 8, 32);
        gfx_PrintStringXY("Usa tools/rom-converter.html", 8, 44);
        gfx_PrintStringXY("Pulsa una tecla", 8, 220);
        wait_key();
        return;
    }
    gfx_SetColor(255);
    sel = choose_rom(names, n);
    if (sel >= 0)
        run_rom(names[sel]);
}

int main(void)
{
    gfx_Begin();
    gfx_SetDrawScreen();
    restore_screen();

    autotest();
    gfx_PrintStringXY("Pulsa una tecla", 8, 220);
    wait_key();

    for (;;) {
        screen("snes-ti84ce - menu");
        gfx_PrintStringXY("[1] Cargar una ROM convertida", 8, 32);
        gfx_PrintStringXY("[2] Demo de la PPU (modos 0-7)", 8, 44);
        gfx_PrintStringXY("[3] Prueba del mando", 8, 56);
        gfx_PrintStringXY("[clear] Salir", 8, 76);
        do kb_Scan(); while (kb_AnyKey());
        do kb_Scan(); while (!kb_AnyKey());
        if (kb_IsDown(kb_KeyClear))
            break;
        if (kb_IsDown(kb_Key1)) {
            load_rom_menu();
        } else if (kb_IsDown(kb_Key2)) {
            ppu_demo();
        } else if (kb_IsDown(kb_Key3)) {
            joypad_test();
        }
    }

    gfx_End();
    return 0;
}
