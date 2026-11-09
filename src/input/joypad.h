#ifndef INPUT_JOYPAD_H
#define INPUT_JOYPAD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Mando SNES emulado con el teclado de la TI-84 Plus CE.
 *
 * Formato de la palabra de 16 bits (igual que JOY1H:JOY1L en $4219:$4218
 * y que el orden en que el mando la envía por $4016, del bit 15 al 0):
 *
 *   15 14  13     12    11 10   9    8     7 6 5 4  3-0
 *   B  Y   Select Start Up Down Left Right A X L R  0 (firma del mando)
 */
#define SNES_B      0x8000u
#define SNES_Y      0x4000u
#define SNES_SELECT 0x2000u
#define SNES_START  0x1000u
#define SNES_UP     0x0800u
#define SNES_DOWN   0x0400u
#define SNES_LEFT   0x0200u
#define SNES_RIGHT  0x0100u
#define SNES_A      0x0080u
#define SNES_X      0x0040u
#define SNES_L      0x0020u
#define SNES_R      0x0010u

/* Deja el mando en reposo (sin botones, latch a 0, registros a 0). */
void joypad_reset(void);

/*
 * Escanea el teclado y actualiza el estado actual del mando 1.
 * Llamar una vez por fotograma (por ejemplo al empezar el VBlank).
 * Devuelve la palabra de 16 bits leída.
 */
uint16_t joypad_poll(void);

/* Estado actual del mando 1 (lo último que leyó joypad_poll). */
uint16_t joypad_state(void);

/* true si se pulsó la tecla de salir del emulador ([clear]). */
bool joypad_quit_requested(void);

/*
 * Lectura automática del mando ($4200 bit 0 = 1): el bus la llama al
 * empezar el VBlank. Copia el estado a $4218-$421F y vacía los
 * registros serie, como hace la consola.
 */
void joypad_auto_read(void);

/*
 * Acceso desde el bus de la CPU.
 *   joypad_write: $4016 (bit 0 = latch del mando).
 *   joypad_read:  $4016, $4017 (lectura serie) y $4218-$421F (lectura automática).
 * Para otras direcciones joypad_read devuelve 0.
 */
void    joypad_write(uint16_t addr, uint8_t value);
uint8_t joypad_read(uint16_t addr);

#endif
