/*
 * Bus provisional para la calculadora: 512 bytes de RAM en $00:0000-$01FF
 * y el programa de autotest en $00:8000. El mapa de memoria real de la
 * SNES (LoROM/HiROM, WRAM, registros) llegará en una fase posterior.
 */
#include "bus.h"
#include "demo_rom.h"

static uint8_t ram[0x200];

uint8_t bus_read(uint32_t addr)
{
    if (addr < sizeof(ram))
        return ram[addr];
    if (addr >= DEMO_ROM_BASE && addr < DEMO_ROM_BASE + sizeof(demo_rom))
        return demo_rom[addr - DEMO_ROM_BASE];
    if (addr == 0xFFFC)
        return DEMO_ROM_BASE & 0xFF;
    if (addr == 0xFFFD)
        return DEMO_ROM_BASE >> 8;
    return 0;
}

void bus_write(uint32_t addr, uint8_t value)
{
    if (addr < sizeof(ram))
        ram[addr] = value;
}
