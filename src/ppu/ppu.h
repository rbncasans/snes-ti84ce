#ifndef PPU_H
#define PPU_H

#include <stdint.h>

/*
 * PPU de la SNES (S-PPU1 + S-PPU2), renderizado por líneas.
 *
 * Interfaz con el bus / bucle principal:
 *   - El bus encamina las escrituras a $2100-$213F (bancos $00-$3F y $80-$BF)
 *     a ppu_write(addr & 0xFF, valor) y las lecturas a ppu_read(addr & 0xFF).
 *     Las transferencias DMA/HDMA hacia la PPU (destino $21xx) usan las mismas
 *     funciones.
 *   - Por cada fotograma, el bucle principal llama:
 *       ppu_frame_start()             al principio (V = 0)
 *       ppu_render_line(v)            al final de cada línea visible
 *                                     v = 1..ppu_visible_lines()
 *       ppu_vblank_start()            al entrar en V-blank (V = 225 o 240)
 *     El HDMA de cada línea debe ejecutarse antes de ppu_render_line(v).
 *   - ppu_set_counters(h, v) actualiza los contadores H/V que se capturan al
 *     leer $2137 (SLHV).
 *
 * Memoria: la PPU solo usa ppu_t (~1,4 KB), cinco búferes de línea estáticos
 * de 256 bytes y la VRAM de 64 KB que se le pasa en ppu_init(). Ni
 * ppu_render_line() ni ninguna otra función reservan memoria ni tocan RAM de
 * usuario fuera de eso. En la calculadora la VRAM vive en la segunda mitad de
 * la RAM del LCD (0xD52C00), así que no hay que usar gfx_SetDrawBuffer().
 *
 * Salida: cada píxel es un índice 0-255 de CGRAM (8 bits). En la calculadora
 * la pantalla está en modo de paleta de 8 bpp y la CGRAM se copia a la paleta
 * del LCD con ppu_build_palette(), aplicando el brillo de INIDISP.
 */

#define PPU_VRAM_SIZE 0x10000u

typedef struct {
    uint16_t hofs, vofs;   /* Desplazamiento (10 bits) */
    uint8_t  sc;           /* BGnSC: base del mapa y tamaño */
    uint8_t  nba;          /* Base de caracteres (nibble) */
} ppu_bg_t;

typedef struct {
    /* Memorias */
    uint8_t  *vram;        /* 64 KB (fuera de la estructura: ver ppu_init) */
    uint16_t cgram[256];   /* Colores BGR555 */
    uint8_t  oam[544];     /* 512 bytes tabla baja + 32 tabla alta */

    /* $2100 INIDISP */
    uint8_t  inidisp;
    /* $2101 OBSEL */
    uint8_t  obsel;
    /* OAM */
    uint16_t oam_reload;   /* Dirección de recarga (palabras) */
    uint16_t oam_addr;     /* Dirección actual (bytes, 10 bits) */
    uint8_t  oam_latch;
    uint8_t  oam_prio_rot; /* Bit 7 de $2103 */
    /* $2105 BGMODE, $2106 MOSAIC */
    uint8_t  bgmode;
    uint8_t  mosaic;
    ppu_bg_t bg[4];
    uint8_t  ofs_latch, hofs_latch;
    /* VRAM */
    uint8_t  vmain;
    uint16_t vaddr;        /* Dirección en palabras */
    uint16_t vprefetch;
    /* Modo 7 */
    uint8_t  m7sel;
    int16_t  m7a, m7b, m7c, m7d;
    int16_t  m7x, m7y, m7hofs, m7vofs;
    uint8_t  m7_latch;
    /* CGRAM */
    uint16_t cg_addr;      /* Dirección en bytes (9 bits) */
    uint8_t  cg_latch;
    /* Ventanas */
    uint8_t  wsel[3];      /* $2123-$2125 */
    uint8_t  wl[2], wr[2]; /* $2126-$2129 */
    uint8_t  wbglog, wobjlog;
    uint8_t  tm, ts, tmw, tsw;
    /* Matemática de color (se guarda, no se aplica: ver README) */
    uint8_t  cgwsel, cgadsub;
    uint16_t fixed_color;
    /* $2133 SETINI */
    uint8_t  setini;
    /* Contadores y estado */
    uint16_t hcount, vcount;
    uint16_t hlatch, vlatch;
    uint8_t  hlatch_hi, vlatch_hi;
    uint8_t  counters_latched;
    uint8_t  stat77, field;
    uint8_t  ppu1_open_bus, ppu2_open_bus;
    uint8_t  mosaic_start; /* Línea donde empieza el bloque de mosaico */
} ppu_t;

extern ppu_t ppu;

/* vram: 64 KB de memoria para la VRAM de la SNES.
 * out:  primera fila del área de 256 píxeles de salida (índices de 8 bits).
 * pitch: bytes entre filas de salida (320 en la calculadora, 256 en PC). */
void ppu_init(uint8_t *vram, uint8_t *out, uint16_t pitch);
void ppu_reset(void);

void    ppu_write(uint8_t reg, uint8_t value);
uint8_t ppu_read(uint8_t reg);

void ppu_frame_start(void);
void ppu_render_line(uint16_t v);
void ppu_vblank_start(void);
void ppu_set_counters(uint16_t h, uint16_t v);

/* 224 o 239 según el bit de overscan de SETINI. */
uint16_t ppu_visible_lines(void);

/* Convierte la CGRAM con el brillo actual a formato 1555 del LCD de la CE
 * (0RRRRRGGGGGBBBBB). Negro si hay forced blank. */
void ppu_build_palette(uint16_t *dst);

/* Se pone a 1 cuando cambian la CGRAM o el brillo; lo limpia quien copie
 * la paleta. */
extern uint8_t ppu_palette_dirty;

/* Valor de bus abierto de la CPU: el bus lo fija antes de llamar a ppu_read()
 * y es lo que se devuelve al leer registros de solo escritura y $2137. */
extern uint8_t ppu_open_bus;

#endif
