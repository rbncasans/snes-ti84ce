#include <graphx.h>
#include <keypadc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "bus.h"
#include "cpu/65c816.h"
#include "demo_rom.h"
#include "mem/mem.h"
#include "mem/rom_ce.h"

/* Se dibuja siempre directamente en pantalla (un solo búfer): la segunda
 * mitad de la RAM del LCD queda libre para la VRAM de la PPU, el cartucho
 * de autotest y una página de WRAM. */
#define LCD_RAM   ((uint8_t *)0xD40000)
#define LCD_BACK  (LCD_RAM + 320 * 240)

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

static void run_rom(const char *name)
{
    static const char *map_name[3] = { "LoROM", "HiROM", "ExHiROM" };
    uint32_t hdr;
    uint16_t reset;
    uint32_t frames = 0, instr = 0;
    clock_t t0;
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

    gfx_PrintStringXY("Ejecutando la CPU (sin PPU todavia).", 8, 116);
    gfx_PrintStringXY("[clear] para salir", 8, 220);

    cpu_reset(&cpu);
    t0 = clock();
    for (;;) {
        /* ~1 fotograma: 262 líneas, ~1364/8 ciclos de bus por línea */
        uint32_t target = cpu.cycles + 262u * 170u;
        while (cpu.cycles < target && !cpu.stopped) {
            cpu_step(&cpu);
            cpu.cycles += mem_take_dma_cycles();
            instr++;
            if (mem_take_nmi_request())
                cpu_nmi(&cpu);
        }
        mem_vblank_end();
        if (mem_vblank_start())
            cpu_nmi(&cpu);
        frames++;

        if ((frames & 31) == 0) {
            uint32_t dt = (uint32_t)(clock() - t0);
            kb_Scan();
            if (kb_IsDown(kb_KeyClear))
                break;
            gfx_FillRectangle(0, 128, 320, 60);
            gfx_SetColor(255);
            gfx_PrintStringXY("PC = ", 8, 130);
            print_hex(((uint32_t)cpu.pb << 16) | cpu.pc, 6);
            gfx_PrintString(cpu.stopped ? "  (STP)" : cpu.waiting ? "  (WAI)" : "");
            gfx_PrintStringXY("Fotogramas: ", 8, 142);
            gfx_PrintUInt(frames, 1);
            gfx_PrintStringXY("Instrucciones/s: ", 8, 154);
            gfx_PrintUInt(dt ? (uint32_t)((float)instr * CLOCKS_PER_SEC / dt) : 0, 1);
            gfx_PrintStringXY("Fotogramas/s x10: ", 8, 166);
            gfx_PrintUInt(dt ? (uint32_t)(320u * CLOCKS_PER_SEC / dt) : 0, 1);
            t0 = clock();
            instr = 0;
        }
    }
    rom_unload();
}

int main(void)
{
    static char names[MAX_ROMS][9];
    uint8_t n;
    int8_t sel;

    gfx_Begin();
    gfx_SetDrawScreen();
    gfx_SetColor(255);

    autotest();
    n = rom_list(names, MAX_ROMS);
    if (n == 0) {
        gfx_PrintStringXY("No hay ROMs convertidas (AppVars SNCE).", 8, 140);
        gfx_PrintStringXY("Usa tools/rom-converter.html", 8, 152);
        gfx_PrintStringXY("Pulsa una tecla para salir", 8, 220);
        wait_key();
    } else {
        sel = choose_rom(names, n);
        if (sel >= 0)
            run_rom(names[sel]);
    }

    gfx_End();
    return 0;
}
