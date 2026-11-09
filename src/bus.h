#ifndef BUS_H
#define BUS_H

#include <stdint.h>

/*
 * Bus de memoria de 24 bits visto por la CPU.
 * La implementación real es el mapa de memoria de la SNES (src/mem/mem.c);
 * los tests de la CPU en PC aportan un bus plano de 16 MB.
 */
uint8_t bus_read(uint32_t addr);
void bus_write(uint32_t addr, uint8_t value);

#endif
