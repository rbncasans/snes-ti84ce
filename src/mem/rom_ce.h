#ifndef ROM_CE_H
#define ROM_CE_H

#include <stdint.h>

/*
 * Carga de ROMs en la calculadora a partir de las AppVars que genera
 * tools/rom-converter.html (formato v1, ver docs/formato-appvars.md).
 *
 * La ROM no se copia a RAM: cada página de 8 KB del mapa apunta
 * directamente a la AppVar archivada en Flash. Solo van a RAM la WRAM
 * (128 KB) y la SRAM de la partida.
 *
 * WRAM: la RAM de usuario de la CE (~150 KB) no da para 128 KB contiguos
 * más el programa, así que se reparte en 16 páginas de 8 KB:
 *   - 8 páginas en pixelShadow ($D031F6, 69090 bytes), si el programa no la
 *     está usando (se comprueba al cargar) y se deja a cero al salir.
 *   - 1 página en el hueco final de la RAM del LCD ($D62C00), detrás de la
 *     VRAM de la PPU (requiere dibujar con un solo búfer).
 *   - El resto con malloc().
 */

#define ROM_OK          0
#define ROM_ERR_OPEN    1  /* No se puede abrir el índice */
#define ROM_ERR_FORMAT  2  /* El índice no es "SNCE" versión 1 */
#define ROM_ERR_CHUNK   3  /* Falta alguna AppVar de la ROM */
#define ROM_ERR_RAM     4  /* No hay RAM para la WRAM o la SRAM */

typedef struct {
    char     idx_name[9];
    char     title[22];
    uint8_t  map;          /* MAP_LOROM / MAP_HIROM / MAP_EXHIROM */
    uint8_t  fastrom;
    uint32_t rom_size;
    uint32_t sram_size;
    uint16_t nchunks;
    uint16_t stored;       /* Trozos guardados (el resto son de relleno) */
    char     missing[9];   /* AppVar que falta si ROM_ERR_CHUNK */
    uint8_t  wram_heap;    /* Páginas de WRAM que salieron de malloc */
    uint8_t  wram_ps;      /* Páginas en pixelShadow */
    uint8_t  wram_lcd;     /* Páginas en la RAM del LCD */
} rom_info_t;

extern rom_info_t rom_info;

/* Busca índices de ROM (AppVars que empiezan por "SNCE").
 * Copia hasta max nombres y devuelve cuántos encontró. */
uint8_t rom_list(char names[][9], uint8_t max);

/* Carga la ROM y configura el mapa de memoria (mem_init). */
int rom_load(const char *idx_name);

/* Guarda la SRAM en su AppVar (archivada) y libera la memoria.
 * Llamar al salir, cuando ya no se va a ejecutar la CPU. */
void rom_unload(void);

/* Texto de un código de error */
const char *rom_error(int err);

#endif
