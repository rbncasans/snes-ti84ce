/* Tests del mando en el PC: make -C tests/joypad */
#include <stdio.h>
#include <string.h>
#include "../../src/input/joypad.h"
#include "keypadc.h"

uint8_t kb_Data[8];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void press(int g, int m) { kb_Data[g] |= (uint8_t)m; }
static void clear_keys(void) { memset(kb_Data, 0, sizeof kb_Data); }

static uint16_t read_serial(void)
{
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) v = (uint16_t)((v << 1) | (joypad_read(0x4016) & 1));
    return v;
}

int main(void)
{
    joypad_reset();

    /* Cada tecla activa su botón. */
    struct { int g, m; uint16_t b; } keys[] = {
        {7, kb_Up, SNES_UP}, {7, kb_Down, SNES_DOWN}, {7, kb_Left, SNES_LEFT},
        {7, kb_Right, SNES_RIGHT}, {1, kb_2nd, SNES_Y}, {1, kb_Mode, SNES_X},
        {2, kb_Alpha, SNES_B}, {3, kb_GraphVar, SNES_A}, {1, kb_Yequ, SNES_L},
        {1, kb_Graph, SNES_R}, {6, kb_Add, SNES_SELECT}, {6, kb_Enter, SNES_START},
    };
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        clear_keys(); press(keys[i].g, keys[i].m);
        CHECK(joypad_poll() == keys[i].b);
    }

    /* Direcciones opuestas se anulan; las demás se conservan. */
    clear_keys(); press(7, kb_Up); press(7, kb_Down); press(7, kb_Left);
    CHECK(joypad_poll() == SNES_LEFT);

    /* Salir. */
    clear_keys(); joypad_poll(); CHECK(!joypad_quit_requested());
    press(6, kb_Clear); joypad_poll(); CHECK(joypad_quit_requested());

    /* Lectura automática: $4218 = A X L R 0000, $4219 = B Y Sel Start Up Down Left Right. */
    clear_keys(); press(2, kb_Alpha); press(3, kb_GraphVar); press(1, kb_Graph); press(7, kb_Right);
    CHECK(joypad_poll() == (SNES_B | SNES_A | SNES_R | SNES_RIGHT));
    CHECK(joypad_read(0x4218) == 0); /* aún no hubo lectura automática */
    joypad_auto_read();
    CHECK(joypad_read(0x4218) == 0x90);
    CHECK(joypad_read(0x4219) == 0x81);
    CHECK(joypad_read(0x421A) == 0 && joypad_read(0x421B) == 0);
    /* Tras la lectura automática el puerto serie devuelve 1s. */
    CHECK(joypad_read(0x4016) == 1);

    /* Lectura serie manual: latch 1 -> 0 y 16 lecturas, bit 15 primero. */
    clear_keys(); press(1, kb_2nd); press(6, kb_Enter); press(1, kb_Yequ);
    joypad_poll();
    joypad_write(0x4016, 1);
    CHECK(joypad_read(0x4016) == 0); /* latch alto: devuelve B (no pulsado) */
    joypad_write(0x4016, 0);
    CHECK(read_serial() == (SNES_Y | SNES_START | SNES_L));
    CHECK(joypad_read(0x4016) == 1); /* después de 16 bits, 1 */

    /* Con el latch alto se lee B continuamente. */
    clear_keys(); press(2, kb_Alpha); joypad_poll();
    joypad_write(0x4016, 1);
    CHECK(joypad_read(0x4016) == 1);
    CHECK(joypad_read(0x4016) == 1);

    /* $4017: mando 2 sin conectar. */
    CHECK(joypad_read(0x4017) == 0x1C);

    if (fails) { printf("%d fallos\n", fails); return 1; }
    printf("joypad: todos los tests OK\n");
    return 0;
}
