#ifndef CPU_65C816_H
#define CPU_65C816_H

#include <stdint.h>

/* Bits del registro de estado P */
#define FLAG_C 0x01 /* Acarreo */
#define FLAG_Z 0x02 /* Cero */
#define FLAG_I 0x04 /* IRQ deshabilitadas */
#define FLAG_D 0x08 /* Modo decimal */
#define FLAG_X 0x10 /* Índices de 8 bits (en emulación: B, break) */
#define FLAG_M 0x20 /* Acumulador/memoria de 8 bits */
#define FLAG_V 0x40 /* Desbordamiento */
#define FLAG_N 0x80 /* Negativo */

/* Estado de la CPU 65C816. */
typedef struct {
    uint16_t a;   /* Acumulador (C = B:A) */
    uint16_t x;   /* Índice X */
    uint16_t y;   /* Índice Y */
    uint16_t s;   /* Puntero de pila */
    uint16_t d;   /* Registro de página directa */
    uint16_t pc;  /* Contador de programa */
    uint8_t  db;  /* Banco de datos */
    uint8_t  pb;  /* Banco de programa */
    uint8_t  p;   /* Flags NVMXDIZC */
    uint8_t  e;   /* 1 = modo emulación 6502, 0 = nativo */

    uint8_t  waiting;     /* Detenida por WAI hasta la siguiente interrupción */
    uint8_t  stopped;     /* Detenida por STP hasta un reset */
    uint8_t  nmi_pending; /* NMI pendiente (flanco) */
    uint8_t  irq_line;    /* Línea IRQ activa (nivel) */

    uint32_t cycles;      /* Ciclos aproximados ejecutados */
} cpu_t;

/* Reinicia la CPU y carga PC desde el vector de reset ($00:FFFC). */
void cpu_reset(cpu_t *cpu);

/* Ejecuta una instrucción (o atiende una interrupción). Devuelve los ciclos usados. */
uint32_t cpu_step(cpu_t *cpu);

/* Señala una NMI (se atiende antes de la siguiente instrucción). */
void cpu_nmi(cpu_t *cpu);

/* Fija el nivel de la línea IRQ (1 = activa). */
void cpu_set_irq(cpu_t *cpu, uint8_t level);

#endif
