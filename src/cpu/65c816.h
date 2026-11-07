#ifndef CPU_65C816_H
#define CPU_65C816_H

#include <stdint.h>

/* Estado de la CPU 65C816 (registros principales). */
typedef struct {
    uint16_t a;   /* Acumulador */
    uint16_t x;   /* Índice X */
    uint16_t y;   /* Índice Y */
    uint16_t s;   /* Puntero de pila */
    uint16_t d;   /* Registro directo */
    uint8_t  db;  /* Banco de datos */
    uint8_t  pb;  /* Banco de programa */
    uint16_t pc;  /* Contador de programa */
    uint8_t  p;   /* Flags NVMXDIZC */
    uint8_t  e;   /* Modo emulación (1) / nativo (0) */
} cpu_t;

void cpu_reset(cpu_t *cpu);

#endif
