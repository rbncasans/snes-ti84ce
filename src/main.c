#include <graphx.h>
#include <keypadc.h>
#include <stdbool.h>
#include <stdint.h>

#include "bus.h"
#include "cpu/65c816.h"
#include "ppu/ppu.h"
#include "ppu/ppu_demo.h"

#include <string.h>
#include <time.h>

/* LCD de la CE en modo de paleta de 8 bpp (graphx) */
#define LCD_RAM     ((uint8_t *)0xD40000)
#define LCD_PALETTE ((uint16_t *)0xE30200)
/* La imagen de 256x224 va centrada en la pantalla de 320x240 */
#define SNES_OUT    (LCD_RAM + 8 * 320 + 32)
/* La VRAM de la SNES (64 KB) va en la segunda mitad de la RAM del LCD,
 * libre porque se dibuja con un solo búfer (sin gfx_SetDrawBuffer). */
#define SNES_VRAM   (LCD_RAM + 320 * 240)

static cpu_t cpu;

static uint16_t ram16(uint32_t addr)
{
    return bus_read(addr) | ((uint16_t)bus_read(addr + 1) << 8);
}

static void print_hex16(uint16_t v)
{
    static const char digits[] = "0123456789ABCDEF";
    char buf[6];
    buf[0] = '$';
    buf[1] = digits[(v >> 12) & 0xF];
    buf[2] = digits[(v >> 8) & 0xF];
    buf[3] = digits[(v >> 4) & 0xF];
    buf[4] = digits[v & 0xF];
    buf[5] = '\0';
    gfx_PrintString(buf);
}

static bool check(const char *label, uint32_t addr, uint16_t expected, uint8_t y)
{
    uint16_t got = ram16(addr);
    bool ok = got == expected;
    gfx_SetTextXY(8, y);
    gfx_PrintString(label);
    print_hex16(got);
    gfx_PrintString(ok ? "  OK" : "  FALLO");
    return ok;
}

/* Demo de la PPU: [<] [>] cambian de modo, [clear] sale. */
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

        /* Fotogramas por segundo (x10) cada 8 fotogramas */
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
}

int main(void)
{
    uint32_t steps = 0;
    bool ok = true;

    cpu_reset(&cpu);
    while (!cpu.stopped && steps < 100000) {
        cpu_step(&cpu);
        steps++;
    }

    gfx_Begin();
    gfx_FillScreen(255);
    gfx_SetTextFGColor(0);

    gfx_PrintStringXY("snes-ti84ce - autotest CPU 65C816", 8, 8);
    ok &= check("Suma 1..100:   ", 0x0000, 0x13BA, 32);
    ok &= check("ADC decimal:   ", 0x0002, 0x5555, 44);
    ok &= check("JSR/RTS:       ", 0x0004, 0xBEEF, 56);
    ok &= cpu.stopped != 0;

    gfx_SetTextXY(8, 80);
    gfx_PrintString("Instrucciones: ");
    gfx_PrintUInt(steps, 1);
    gfx_SetTextXY(8, 92);
    gfx_PrintString("Ciclos aprox.: ");
    gfx_PrintUInt(cpu.cycles, 1);

    gfx_PrintStringXY(ok ? "Resultado: TODO OK" : "Resultado: HAY FALLOS", 8, 116);
    gfx_PrintStringXY("[enter] demo de la PPU (modos 0-7)", 8, 208);
    gfx_PrintStringXY("[clear] salir", 8, 220);

    for (;;) {
        kb_Scan();
        if (kb_IsDown(kb_KeyClear))
            break;
        if (kb_IsDown(kb_KeyEnter)) {
            ppu_demo();
            break;
        }
    }

    gfx_End();
    return 0;
}
