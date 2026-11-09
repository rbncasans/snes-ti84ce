#ifndef PPU_DEMO_H
#define PPU_DEMO_H

#include <stdint.h>

/*
 * Escenas de prueba para la PPU, generadas por código (sin recursos con
 * copyright). Todo se carga con ppu_write(), igual que lo haría un juego,
 * así que también sirven para probar los registros.
 */

#define DEMO_SCENES 8

/* Carga la escena del modo `mode` (0-7) en la PPU. */
void demo_setup(uint8_t mode);

/* Animación por fotograma (scroll, sprites). */
void demo_frame(uint8_t mode, uint16_t frame);

/* Cambios por línea, como haría el HDMA (perspectiva del modo 7). */
void demo_line(uint8_t mode, uint16_t v, uint16_t frame);

/* Nombre corto de la escena. */
const char *demo_name(uint8_t mode);

#endif
