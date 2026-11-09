#ifndef MEM_H
#define MEM_H

#include <stdint.h>

/*
 * Mapa de memoria de la SNES (bus A de 24 bits del 5A22).
 *
 * El espacio de 16 MB se divide en 2048 páginas de 8 KB. Para cada página
 * hay un puntero de lectura y otro de escritura: si no es NULL, el byte está
 * en puntero[dir & 0x1FFF] (camino rápido: ROM en Flash, WRAM y SRAM). Si es
 * NULL se pasa por el camino lento, que decodifica la dirección (registros de
 * E/S, bus abierto, trozos de relleno, SRAM de menos de 8 KB...).
 *
 *   Bancos $00-$3F y $80-$BF
 *     $0000-$1FFF  WRAM (primeros 8 KB, espejo de $7E:0000)
 *     $2000-$5FFF  E/S: $21xx bus B (PPU, APU, puerto WRAM), $4016/$4017
 *                  mandos, $42xx registros de la CPU, $43xx DMA
 *     $6000-$7FFF  SRAM en HiROM/ExHiROM (bancos $20-$3F/$A0-$BF);
 *                  bus abierto en LoROM
 *     $8000-$FFFF  ROM
 *   Bancos $40-$7D y $C0-$FF  ROM (y SRAM en LoROM: $70-$7D/$F0-$FF, < $8000)
 *   Bancos $7E-$7F            WRAM (128 KB)
 *
 * Desplazamiento en la ROM (ver docs/formato-appvars.md):
 *   LoROM    ((banco & $7F) << 15) | (dir & $7FFF)
 *   HiROM    ((banco & $3F) << 16) | dir
 *   ExHiROM  como HiROM; los bancos $00-$7D suman $400000
 * y después se aplica el espejado de ROMs cuyo tamaño no es potencia de 2.
 */

#define MEM_PAGE_BITS   13
#define MEM_PAGE_SIZE   0x2000u
#define MEM_PAGE_MASK   0x1FFFu
#define MEM_PAGES       2048u
#define MEM_WRAM_SIZE   0x20000u
#define MEM_WRAM_PAGES  16u
#define MEM_CHUNK_BITS  15
#define MEM_CHUNK_SIZE  0x8000u

/* Tipo de mapa (mismo valor que el campo "Mapa" del índice <P>IDX) */
#define MAP_LOROM   0
#define MAP_HIROM   1
#define MAP_EXHIROM 2

/* Trozo de 32 KB de la ROM (normalmente una AppVar archivada en Flash). */
typedef struct {
    const uint8_t *data; /* NULL: trozo de relleno, todos los bytes = fill */
    uint16_t len;        /* Bytes válidos en data (32768 salvo el último) */
    uint8_t  fill;       /* Byte de relleno (y para leer más allá de len) */
} mem_chunk_t;

typedef struct {
    uint8_t  map;                 /* MAP_LOROM / MAP_HIROM / MAP_EXHIROM */
    uint32_t rom_size;            /* Tamaño real de la ROM en bytes */
    uint16_t nchunks;
    const mem_chunk_t *chunks;    /* nchunks trozos; se guarda el puntero */
    uint8_t *sram;                /* NULL si el cartucho no tiene */
    uint32_t sram_size;
    /* 16 páginas de 8 KB de WRAM: no hace falta que sean contiguas.
     * Página n = bytes n*8192 .. n*8192+8191 de los 128 KB. */
    uint8_t *wram[MEM_WRAM_PAGES];
} mem_config_t;

/*
 * Conexión con el resto de la consola. Si un puntero es NULL, las lecturas
 * devuelven bus abierto y las escrituras se ignoran.
 *   ppu_read/ppu_write   $2100-$213F (reg = dir & 0xFF), también desde DMA
 *   apu_read/apu_write   $2140-$217F (port = dir & 3). Si son NULL se usa una
 *                        APU falsa (eco de lo escrito, $AA/$BB al arrancar)
 *                        para que los juegos no se cuelguen esperando al SPC700
 *   joy_read/joy_write   $4016/$4017 y $4218-$421F (dir & 0xFFFF)
 *   joy_auto_read        lectura automática al entrar en VBlank ($4200 bit 0)
 * Encajan directamente con ppu_read/ppu_write de src/ppu y con
 * joypad_read/joypad_write/joypad_auto_read de src/input.
 */
typedef struct {
    uint8_t (*ppu_read)(uint8_t reg);
    void    (*ppu_write)(uint8_t reg, uint8_t value);
    uint8_t (*apu_read)(uint8_t port);
    void    (*apu_write)(uint8_t port, uint8_t value);
    uint8_t (*joy_read)(uint16_t addr);
    void    (*joy_write)(uint16_t addr, uint8_t value);
    void    (*joy_auto_read)(void);
} mem_hooks_t;

extern mem_hooks_t mem_hooks;

/* Tablas de páginas (las usa bus_read/bus_write) */
extern const uint8_t *mem_rd[MEM_PAGES];
extern uint8_t *mem_wr[MEM_PAGES];

/* Construye las tablas de páginas y reinicia los registros de E/S.
 * Devuelve 0 si todo bien, -1 si la configuración no es válida. */
int mem_init(const mem_config_t *cfg);

/* Reinicia los registros de E/S ($42xx, $43xx, puerto WRAM, APU falsa). */
void mem_reset_io(void);

/* Desplazamiento en la ROM de una dirección SNES, o -1 si no es ROM.
 * Útil para depurar y para los tests. */
int32_t mem_rom_offset(uint32_t addr);

/* ---- Interfaz con el bucle principal (temporización) ---------------- */

/* Llamar al entrar en VBlank. Activa RDNMI/HVBJOY, hace la lectura
 * automática de los mandos si está activada y devuelve 1 si hay que
 * lanzar la NMI a la CPU ($4200 bit 7). */
uint8_t mem_vblank_start(void);
/* Llamar al empezar un fotograma nuevo (fin del VBlank). */
void mem_vblank_end(void);
/* Llamar al empezar la línea v. Devuelve 1 si salta la IRQ de línea
 * ($4200 bits 4-5 = V-IRQ y v == VTIME). La IRQ por H se aproxima al
 * principio de la línea. Bajar la línea IRQ cuando se lea $4211. */
uint8_t mem_line_irq(uint16_t v);
/* 1 mientras TIMEUP ($4211) tenga la IRQ pendiente. */
uint8_t mem_irq_pending(void);
/* 1 si una escritura en $4200 ha activado la NMI durante el VBlank;
 * el bucle principal debe lanzar la NMI y se limpia al leerlo. */
uint8_t mem_take_nmi_request(void);

/* HDMA: mem_hdma_init() al empezar el fotograma (V = 0) y mem_hdma_line()
 * en el H-blank de cada línea 0..visibles-1 (lo que escribe en la línea v
 * se ve al renderizar la línea v+1). Los canales son los de $420C. */
void mem_hdma_init(void);
void mem_hdma_line(void);

/* Ciclos de CPU que han consumido las DMA y HDMA desde la última llamada. */
uint32_t mem_take_dma_cycles(void);

/* Registros de la CPU visibles para el bucle principal */
extern uint8_t mem_nmitimen; /* $4200 */
extern uint8_t mem_hdmaen;   /* $420C */
extern uint8_t mem_memsel;   /* $420D bit 0: FastROM */
extern uint8_t mem_dma[8][16]; /* $43x0-$43xF */

#endif
