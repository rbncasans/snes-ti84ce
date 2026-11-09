#include <graphx.h>
#include <keypadc.h>
#include <stdbool.h>

#include "cpu/65c816.h"
#include "input/joypad.h"

/* Nombres de los botones en el orden de los bits 15..4 del mando. */
static const char *const button_names[12] = {
    "B", "Y", "Select", "Start", "Arriba", "Abajo",
    "Izquierda", "Derecha", "A", "X", "L", "R",
};

static void draw_pad(uint16_t pad)
{
    uint8_t i;

    gfx_FillScreen(255);
    gfx_SetTextFGColor(0);
    gfx_PrintStringXY("snes-ti84ce - prueba del mando", 8, 8);
    gfx_PrintStringXY("Pulsa [clear] para salir", 8, 24);

    for (i = 0; i < 12; i++) {
        uint16_t bit = (uint16_t)(0x8000u >> i);
        gfx_SetTextFGColor((pad & bit) ? 224 : 181); /* rojo / gris */
        gfx_PrintStringXY(button_names[i], 16 + (i / 6) * 150, 48 + (i % 6) * 16);
    }

    gfx_SetTextFGColor(0);
    gfx_PrintStringXY("$4218/$4219 = ", 8, 160);
    gfx_PrintUInt(joypad_read(0x4219), 3);
    gfx_PrintString(" ");
    gfx_PrintUInt(joypad_read(0x4218), 3);
    gfx_SwapDraw();
}

int main(void)
{
    cpu_t cpu;
    uint16_t last = 0xFFFF;

    gfx_Begin();
    gfx_SetDrawBuffer();

    cpu_reset(&cpu);
    joypad_reset();

    do {
        uint16_t pad = joypad_poll();
        joypad_auto_read(); /* lo que hará el bus en cada VBlank */
        if (pad != last) {
            draw_pad(pad);
            last = pad;
        }
    } while (!joypad_quit_requested());

    gfx_End();
    return 0;
}
