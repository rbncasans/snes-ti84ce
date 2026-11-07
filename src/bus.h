#ifndef BUS_H
#define BUS_H

#include <stdint.h>

/*
 * Bus de memoria de 24 bits visto por la CPU.
 * Cada plataforma (calculadora, tests en PC) aporta su implementación.
 */
uint8_t bus_read(uint32_t addr);
void bus_write(uint32_t addr, uint8_t value);

#endif
