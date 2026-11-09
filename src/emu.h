#ifndef EMU_H
#define EMU_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/65c816.h"

/*
 * Bucle principal de la consola: une CPU, mapa de memoria (DMA y HDMA),
 * PPU y mando. El mapa de memoria tiene que estar ya configurado con
 * mem_init() (lo hace rom_load() en la calculadora).
 *
 * Un fotograma NTSC son 262 líneas. En cada línea v:
 *   1. IRQ de línea si toca ($4200/$4209)
 *   2. Si v es la primera línea de V-blank: la PPU entra en V-blank, se lee
 *      el teclado, lectura automática del mando y NMI
 *   3. La CPU ejecuta EMU_LINE_CYCLES ciclos (más lo que roben las DMA)
 *   4. La PPU dibuja la línea v (1..visibles)
 *   5. H-blank: HDMA (afecta a la línea siguiente)
 */

#define EMU_LINE_CYCLES 170u   /* ~1364 ciclos maestros / 8 */
#define EMU_FRAME_LINES 262u

extern cpu_t emu_cpu;

/* Instrucciones ejecutadas desde la última vez que se puso a 0 */
extern uint32_t emu_instr;

/* vram: 64 KB para la VRAM. out/pitch: salida de la PPU (ver ppu_init). */
void emu_init(uint8_t *vram, uint8_t *out, uint16_t pitch);

/* Ejecuta un fotograma completo. Con render = false no se dibuja nada
 * (salto de fotogramas), pero la CPU, el HDMA y el mando siguen igual. */
void emu_frame(bool render);

/* 1 si el usuario ha pedido salir ([clear]) */
bool emu_quit_requested(void);

#endif
