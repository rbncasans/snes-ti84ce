#include "joypad.h"

#include <keypadc.h>

/*
 * Distribución por defecto (ver README):
 *
 *   Cruceta  -> flechas
 *   Y X      -> [2nd]   [mode]
 *   B A      -> [alpha] [X,T,θ,n]
 *   L R      -> [y=]    [graph]
 *   Select   -> [+]
 *   Start    -> [enter]
 *   Salir    -> [clear]
 *
 * Los botones Y/X/B/A ocupan el bloque 2x2 que hay a la izquierda de las
 * flechas, con la misma posición relativa que en el mando de la SNES
 * (Y y B a la izquierda, X y A a la derecha; X e Y arriba, B y A abajo).
 */
typedef struct {
    uint8_t  group;  /* índice en kb_Data[] */
    uint8_t  mask;   /* bit de la tecla en ese grupo */
    uint16_t button; /* bit SNES */
} key_map_t;

static const key_map_t key_map[] = {
    { 7, kb_Up,       SNES_UP     },
    { 7, kb_Down,     SNES_DOWN   },
    { 7, kb_Left,     SNES_LEFT   },
    { 7, kb_Right,    SNES_RIGHT  },
    { 1, kb_2nd,      SNES_Y      },
    { 1, kb_Mode,     SNES_X      },
    { 2, kb_Alpha,    SNES_B      },
    { 3, kb_GraphVar, SNES_A      },
    { 1, kb_Yequ,     SNES_L      },
    { 1, kb_Graph,    SNES_R      },
    { 6, kb_Add,      SNES_SELECT },
    { 6, kb_Enter,    SNES_START  },
};

#define KEY_MAP_LEN (sizeof key_map / sizeof key_map[0])

static uint16_t pad_state;   /* estado actual del mando 1 */
static uint16_t auto_joy1;   /* $4218/$4219 */
static uint16_t shift1;      /* registro serie del mando 1 */
static uint8_t  shift_left;  /* bits que quedan por leer por $4016 */
static bool     latch;       /* bit 0 de la última escritura en $4016 */
static bool     quit;

void joypad_reset(void)
{
    pad_state = 0;
    auto_joy1 = 0;
    shift1 = 0;
    shift_left = 0;
    latch = false;
    quit = false;
}

uint16_t joypad_poll(void)
{
    uint16_t s = 0;
    uint8_t i;

    kb_Scan();

    for (i = 0; i < KEY_MAP_LEN; i++) {
        if (kb_Data[key_map[i].group] & key_map[i].mask) {
            s |= key_map[i].button;
        }
    }

    /*
     * Una cruceta real no puede pulsar dos direcciones opuestas a la vez
     * y algunos juegos se comportan mal si ocurre; en el teclado sí es
     * posible, así que se anulan.
     */
    if ((s & (SNES_UP | SNES_DOWN)) == (SNES_UP | SNES_DOWN)) {
        s &= (uint16_t)~(SNES_UP | SNES_DOWN);
    }
    if ((s & (SNES_LEFT | SNES_RIGHT)) == (SNES_LEFT | SNES_RIGHT)) {
        s &= (uint16_t)~(SNES_LEFT | SNES_RIGHT);
    }

    quit = (kb_Data[6] & kb_Clear) != 0;
    pad_state = s;

    /* Con el latch a 1 el mando recarga continuamente su estado. */
    if (latch) {
        shift1 = s;
        shift_left = 16;
    }
    return s;
}

uint16_t joypad_state(void)
{
    return pad_state;
}

bool joypad_quit_requested(void)
{
    return quit;
}

void joypad_auto_read(void)
{
    auto_joy1 = pad_state;
    /* La lectura automática saca los 16 bits por la línea serie. */
    shift1 = 0;
    shift_left = 0;
}

void joypad_write(uint16_t addr, uint8_t value)
{
    if (addr == 0x4016) {
        bool new_latch = (value & 1) != 0;
        if (new_latch || latch) {
            /* Mientras el latch está a 1 (y al bajarlo) se captura el estado. */
            shift1 = pad_state;
            shift_left = 16;
        }
        latch = new_latch;
    }
}

uint8_t joypad_read(uint16_t addr)
{
    switch (addr) {
    case 0x4016: {
        uint8_t bit;
        if (latch) {
            /* Con el latch alto siempre se lee el botón B. */
            return (pad_state & SNES_B) ? 1 : 0;
        }
        if (shift_left == 0) {
            /* Un mando oficial devuelve 1 tras los 16 bits. */
            return 1;
        }
        bit = (shift1 & 0x8000u) ? 1 : 0;
        shift1 <<= 1;
        shift_left--;
        return bit;
    }
    case 0x4017:
        /* Mando 2 desconectado; los bits 2-4 se leen siempre a 1. */
        return 0x1C;
    case 0x4218: return (uint8_t)(auto_joy1 & 0xFF);
    case 0x4219: return (uint8_t)(auto_joy1 >> 8);
    default:
        /* $421A-$421F: mandos 2, 3 y 4 sin conectar. */
        return 0;
    }
}
