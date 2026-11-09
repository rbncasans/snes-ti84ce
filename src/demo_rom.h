#ifndef DEMO_ROM_H
#define DEMO_ROM_H

#include <stdint.h>

/*
 * Programa de autotest de la CPU, ensamblado a mano (código original).
 * Se carga en $00:8000 y escribe sus resultados en la RAM baja:
 *   $0000 = suma 1..100 en 16 bits     (esperado 5050 = $13BA)
 *   $0002 = $1234 + $4321 en decimal   (esperado $5555)
 *   $0004 = valor devuelto por JSR/RTS (esperado $BEEF)
 * Termina con STP.
 */
#define DEMO_ROM_BASE 0x8000

static const uint8_t demo_rom[] = {
    /* 8000 */ 0x18,             /* CLC                */
    /* 8001 */ 0xFB,             /* XCE   ; modo nativo */
    /* 8002 */ 0xC2, 0x30,       /* REP #$30 ; 16 bits */
    /* 8004 */ 0xA9, 0x00, 0x00, /* LDA #$0000         */
    /* 8007 */ 0xA2, 0x64, 0x00, /* LDX #100           */
    /* 800A */ 0x86, 0x10,       /* STX $10            */
    /* 800C */ 0x18,             /* CLC   ; bucle      */
    /* 800D */ 0x65, 0x10,       /* ADC $10            */
    /* 800F */ 0xC6, 0x10,       /* DEC $10            */
    /* 8011 */ 0xD0, 0xF9,       /* BNE $800C          */
    /* 8013 */ 0x85, 0x00,       /* STA $00            */
    /* 8015 */ 0xF8,             /* SED                */
    /* 8016 */ 0x18,             /* CLC                */
    /* 8017 */ 0xA9, 0x34, 0x12, /* LDA #$1234         */
    /* 801A */ 0x69, 0x21, 0x43, /* ADC #$4321         */
    /* 801D */ 0x85, 0x02,       /* STA $02            */
    /* 801F */ 0xD8,             /* CLD                */
    /* 8020 */ 0x20, 0x30, 0x80, /* JSR $8030          */
    /* 8023 */ 0x85, 0x04,       /* STA $04            */
    /* 8025 */ 0xDB,             /* STP                */
    /* 8026 */ 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA,
    /* 8030 */ 0xA9, 0xEF, 0xBE, /* LDA #$BEEF         */
    /* 8033 */ 0x60,             /* RTS                */
};

#endif
