#ifndef CART_H
#define CART_H

#include <stdint.h>

/*
 * Detección de la cabecera interna de un cartucho de SNES.
 *
 * La cabecera ocupa $FFC0-$FFDF del banco 0 visto por la CPU, que en la ROM
 * está en $7FC0 (LoROM), $FFC0 (HiROM) o $40FFC0 (ExHiROM). Como no hay
 * ninguna marca fiable del tipo de mapa, se puntúa cada candidata (checksum,
 * byte de modo, vector de reset, título legible, primera instrucción...) y
 * se queda la mejor. Es el mismo criterio que usa tools/rom-converter.html.
 *
 * En la calculadora el conversor ya guarda el resultado en el índice <P>IDX;
 * esto sirve para los tests en PC y para ROMs sin convertir.
 */

typedef struct {
    uint8_t  map;          /* MAP_LOROM / MAP_HIROM / MAP_EXHIROM */
    uint32_t header;       /* Desplazamiento de la cabecera en la ROM ($xxC0) */
    int16_t  score;
    char     title[22];    /* 21 caracteres + '\0' */
    uint8_t  map_mode;     /* $FFD5 */
    uint8_t  cart_type;    /* $FFD6 */
    uint8_t  region;       /* $FFD9 */
    uint8_t  fastrom;      /* bit 4 de $FFD5 */
    uint16_t checksum;     /* $FFDE */
    uint16_t complement;   /* $FFDC */
    uint16_t reset;        /* Vector de reset en modo emulación ($FFFC) */
    uint32_t sram_size;    /* Según $FFD8 (0 si no tiene batería/RAM) */
} cart_info_t;

/* 512 si la ROM lleva la cabecera de copiadora de los .smc, si no 0. */
uint32_t cart_copier_header(uint32_t file_size);

/* Analiza la ROM (sin cabecera de copiadora). Devuelve 0 si encontró una
 * cabecera plausible y -1 si la ROM es demasiado pequeña. */
int cart_detect(const uint8_t *rom, uint32_t size, cart_info_t *out);

/* Suma de todos los bytes de la ROM con el espejado de la SNES
 * (lo que debería valer el checksum de la cabecera). */
uint16_t cart_checksum(const uint8_t *rom, uint32_t size);

#endif
