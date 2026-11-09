/* Bucle principal: CPU + mapa de memoria (DMA/HDMA) + PPU + mando. */
#include "emu.h"

#include <string.h>

#include "input/joypad.h"
#include "mem/mem.h"
#include "ppu/ppu.h"

cpu_t emu_cpu;
uint32_t emu_instr;

void emu_init(uint8_t *vram, uint8_t *out, uint16_t pitch)
{
    ppu_init(vram, out, pitch);
    joypad_reset();

    memset(&mem_hooks, 0, sizeof mem_hooks);
    mem_hooks.ppu_read = ppu_read;
    mem_hooks.ppu_write = ppu_write;
    mem_hooks.joy_read = joypad_read;
    mem_hooks.joy_write = joypad_write;
    mem_hooks.joy_auto_read = joypad_auto_read;

    cpu_reset(&emu_cpu);
    emu_instr = 0;
}

/* Ejecuta la CPU durante `cycles` ciclos de bus. */
static void run_cpu(uint32_t cycles)
{
    cpu_t *c = &emu_cpu;
    uint32_t target = c->cycles + cycles;

    while ((int32_t)(c->cycles - target) < 0) {
        if (c->stopped)
            return;
        cpu_step(c);
        c->cycles += mem_take_dma_cycles();
        emu_instr++;
        if (mem_take_nmi_request())
            cpu_nmi(c);
        if (c->irq_line && !mem_irq_pending())
            cpu_set_irq(c, 0);
    }
}

void emu_frame(bool render)
{
    uint16_t v, visible;

    mem_vblank_end();
    ppu_frame_start();
    mem_hdma_init();
    emu_cpu.cycles += mem_take_dma_cycles();
    visible = ppu_visible_lines();

    for (v = 0; v < EMU_FRAME_LINES; v++) {
        if (mem_line_irq(v))
            cpu_set_irq(&emu_cpu, 1);
        ppu_set_counters(0, v);
        if (v == visible + 1) {
            ppu_vblank_start();
            joypad_poll();
            if (mem_vblank_start())
                cpu_nmi(&emu_cpu);
        }

        run_cpu(EMU_LINE_CYCLES);

        if (render && v >= 1 && v <= visible)
            ppu_render_line(v);
        if (v < visible) {
            mem_hdma_line();
            emu_cpu.cycles += mem_take_dma_cycles();
        }
    }
}

bool emu_quit_requested(void)
{
    return joypad_quit_requested();
}
