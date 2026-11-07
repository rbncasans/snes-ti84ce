#include <graphx.h>
#include <keypadc.h>
#include <stdbool.h>
#include <stdint.h>

#include "bus.h"
#include "cpu/65c816.h"

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
    gfx_SetDrawBuffer();
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
    gfx_PrintStringXY("Pulsa [clear] para salir", 8, 220);
    gfx_SwapDraw();

    do {
        kb_Scan();
    } while (!kb_IsDown(kb_KeyClear));

    gfx_End();
    return 0;
}
