#include <graphx.h>
#include <keypadc.h>
#include <stdbool.h>

#include "cpu/65c816.h"

int main(void)
{
    cpu_t cpu;

    gfx_Begin();
    gfx_SetDrawBuffer();

    cpu_reset(&cpu);

    gfx_FillScreen(255);
    gfx_SetTextFGColor(0);
    gfx_PrintStringXY("snes-ti84ce", 8, 8);
    gfx_PrintStringXY("Esqueleto inicial", 8, 24);
    gfx_PrintStringXY("Pulsa [clear] para salir", 8, 40);
    gfx_SwapDraw();

    do {
        kb_Scan();
    } while (!kb_IsDown(kb_KeyClear));

    gfx_End();
    return 0;
}
