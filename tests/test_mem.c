/* Tests del mapa de memoria (LoROM/HiROM/ExHiROM, WRAM, SRAM, E/S) en PC. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "cpu/65c816.h"
#include "demo_rom.h"
#include "mem/cart.h"
#include "mem/mem.h"

static int failures;

#define EXPECT(cond, ...) do { \
    if (!(cond)) { failures++; printf("  FALLO %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t wram[MEM_WRAM_SIZE];
static uint8_t sram[0x8000];
static mem_chunk_t chunks[256];

/* Contenido reconocible: cada byte depende de su desplazamiento */
static uint8_t pattern(uint32_t off)
{
    return (uint8_t)(off ^ (off >> 8) * 3 ^ (off >> 16) * 7);
}

static uint8_t *make_rom(uint32_t size)
{
    uint8_t *r = malloc(size);
    uint32_t i;
    for (i = 0; i < size; i++)
        r[i] = pattern(i);
    return r;
}

/* Escribe una cabecera interna en base con checksum correcto */
static void write_header(uint8_t *rom, uint32_t size, uint32_t base, const char *title,
                         uint8_t map_mode, uint8_t type, uint8_t sram_code, uint16_t reset,
                         uint32_t reset_off)
{
    uint8_t *h = rom + base;
    uint16_t chk;
    size_t n = strlen(title);

    memset(h, ' ', 21);
    memcpy(h, title, n);
    h[0x15] = map_mode;
    h[0x16] = type;
    h[0x17] = 0x0A;
    h[0x18] = sram_code;
    h[0x19] = 0x01;
    h[0x1C] = 0xFF; h[0x1D] = 0xFF; h[0x1E] = 0; h[0x1F] = 0;
    h[0x3C] = (uint8_t)reset;
    h[0x3D] = (uint8_t)(reset >> 8);
    rom[reset_off] = 0x78;                /* SEI */
    chk = cart_checksum(rom, size);
    h[0x1E] = (uint8_t)chk; h[0x1F] = (uint8_t)(chk >> 8);
    h[0x1C] = (uint8_t)~chk; h[0x1D] = (uint8_t)(~chk >> 8);
}

static void setup(uint8_t map, const uint8_t *rom, uint32_t size, uint32_t sram_size)
{
    mem_config_t cfg;
    uint32_t i, n = (size + MEM_CHUNK_SIZE - 1) / MEM_CHUNK_SIZE;

    for (i = 0; i < n; i++) {
        uint32_t left = size - i * MEM_CHUNK_SIZE;
        chunks[i].data = rom + i * MEM_CHUNK_SIZE;
        chunks[i].len = (uint16_t)(left < MEM_CHUNK_SIZE ? left : MEM_CHUNK_SIZE);
        chunks[i].fill = 0;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.map = map;
    cfg.rom_size = size;
    cfg.nchunks = (uint16_t)n;
    cfg.chunks = chunks;
    cfg.sram = sram_size ? sram : NULL;
    cfg.sram_size = sram_size;
    /* Páginas de WRAM desordenadas a propósito: no hace falta que sean contiguas */
    for (i = 0; i < MEM_WRAM_PAGES; i++)
        cfg.wram[i] = wram + ((i * 5) % MEM_WRAM_PAGES) * MEM_PAGE_SIZE;
    memset(wram, 0, sizeof wram);
    memset(sram, 0, sizeof sram);
    EXPECT(mem_init(&cfg) == 0, "mem_init falló");
}

static uint8_t *wram_byte(uint32_t off)
{
    uint32_t page = off >> MEM_PAGE_BITS;
    return wram + ((page * 5) % MEM_WRAM_PAGES) * MEM_PAGE_SIZE + (off & MEM_PAGE_MASK);
}

/* ------------------------------------------------------------------ */

static void test_lorom(void)
{
    const uint32_t size = 0x80000; /* 512 KB */
    uint8_t *rom = make_rom(size);
    cart_info_t ci;

    puts("lorom");
    write_header(rom, size, 0x7FC0, "PRUEBA LOROM", 0x20, 0x02, 0x03, 0x8000, 0x0000);
    EXPECT(cart_detect(rom, size, &ci) == 0, "cart_detect");
    EXPECT(ci.map == MAP_LOROM, "mapa = %d", ci.map);
    EXPECT(strcmp(ci.title, "PRUEBA LOROM") == 0, "título '%s'", ci.title);
    EXPECT(ci.sram_size == 8192, "sram = %u", (unsigned)ci.sram_size);
    EXPECT(ci.checksum == cart_checksum(rom, size), "checksum");
    EXPECT(ci.reset == 0x8000, "reset");

    setup(MAP_LOROM, rom, size, ci.sram_size);

    EXPECT(bus_read(0x008000) == rom[0], "$00:8000");
    EXPECT(bus_read(0x00FFC0) == 'P', "cabecera en $00:FFC0");
    EXPECT(bus_read(0x018000) == rom[0x8000], "$01:8000");
    EXPECT(bus_read(0x0FABCD) == rom[0x7ABCD], "$0F:ABCD");
    EXPECT(bus_read(0x8FABCD) == rom[0x7ABCD], "espejo $8F:ABCD");
    EXPECT(bus_read(0x101234 | 0x8000) == rom[(0x10 << 15 | 0x1234) % size], "espejo por tamaño");
    EXPECT(bus_read(0x401234) == rom[((0x40 << 15) | 0x1234) % size], "$40:1234 espejo bajo");
    EXPECT(bus_read(0xC0FFFF) == rom[((0x40 << 15) | 0x7FFF) % size], "$C0:FFFF");
    EXPECT(mem_rom_offset(0x00FFFC) == 0x7FFC, "offset $00:FFFC = %X", mem_rom_offset(0x00FFFC));
    EXPECT(mem_rom_offset(0x001000) == -1, "WRAM no es ROM");

    /* La ROM no se puede escribir */
    bus_write(0x008000, (uint8_t)~rom[0]);
    EXPECT(bus_read(0x008000) == rom[0], "escritura en ROM");

    /* WRAM y espejos */
    bus_write(0x001234, 0x55);
    EXPECT(bus_read(0x7E1234) == 0x55, "espejo WRAM $7E");
    EXPECT(bus_read(0xBF1234) == 0x55, "espejo WRAM $BF");
    EXPECT(*wram_byte(0x1234) == 0x55, "página de WRAM");
    bus_write(0x7FFFFF, 0x99);
    EXPECT(*wram_byte(0x1FFFF) == 0x99, "$7F:FFFF");
    EXPECT(bus_read(0x7EFFFF) != 0x99 || *wram_byte(0xFFFF) == 0x99, "$7E y $7F distintos");
    bus_write(0x7E2000, 0x11);
    EXPECT(bus_read(0x002000) == 0x20, "$00:2000 es E/S (bus abierto), no WRAM");

    /* SRAM en $70-$7D y $F0-$FF, 8 KB repetidos */
    bus_write(0x700000, 0xA5);
    EXPECT(sram[0] == 0xA5, "SRAM $70:0000");
    EXPECT(bus_read(0x702000) == 0xA5, "espejo SRAM $70:2000");
    EXPECT(bus_read(0xF00000) == 0xA5, "espejo SRAM $F0:0000");
    bus_write(0x701FFF, 0x5A);
    EXPECT(sram[0x1FFF] == 0x5A, "SRAM $70:1FFF");
    EXPECT(bus_read(0x708000) == rom[((0x70 << 15)) % size], "$70:8000 es ROM");

    /* LoROM: $6000 en bancos de sistema es bus abierto */
    EXPECT(bus_read(0x206000) == 0x60, "bus abierto $20:6000");

    free(rom);
}

static void test_lorom_fill_and_short(void)
{
    const uint32_t size = 0x20000; /* 128 KB: 4 trozos */
    uint8_t *rom = make_rom(size);

    puts("lorom_relleno");
    setup(MAP_LOROM, rom, size, 0);
    chunks[2].data = NULL;              /* trozo 2 de relleno */
    chunks[2].fill = 0xEE;
    chunks[3].len = 0x5000;             /* último trozo corto */
    chunks[3].fill = 0x00;
    {
        mem_config_t cfg;
        int i;
        memset(&cfg, 0, sizeof cfg);
        cfg.map = MAP_LOROM; cfg.rom_size = size; cfg.nchunks = 4; cfg.chunks = chunks;
        for (i = 0; i < 16; i++) cfg.wram[i] = wram + i * MEM_PAGE_SIZE;
        mem_init(&cfg);
    }
    EXPECT(bus_read(0x028123) == 0xEE, "trozo de relleno");
    EXPECT(bus_read(0x038000) == rom[0x18000], "trozo corto, dentro");
    EXPECT(bus_read(0x03C000) == rom[0x1C000], "trozo corto, página parcial");
    EXPECT(bus_read(0x03D000) == 0x00, "trozo corto, fuera");
    EXPECT(bus_read(0x700000) == rom[(0x70 << 15) % size], "sin SRAM: ROM");
    free(rom);
}

static void test_mirror_non_pow2(void)
{
    const uint32_t size = 0x180000; /* 1,5 MB = 1 MB + 512 KB */
    uint8_t *rom = make_rom(size);

    puts("espejo_no_potencia_2");
    setup(MAP_LOROM, rom, size, 0);
    /* $30:8000 = desplazamiento 0x180000 -> 0x100000 */
    EXPECT(mem_rom_offset(0x308000) == 0x100000, "0x180000 -> %X", mem_rom_offset(0x308000));
    EXPECT(mem_rom_offset(0x388000) == 0x140000, "0x1C0000 -> %X", mem_rom_offset(0x388000));
    EXPECT(bus_read(0x388001) == rom[0x140001], "lectura espejo");
    EXPECT(mem_rom_offset(0x2F8000) == 0x178000, "dentro del tamaño");
    free(rom);
}

static void test_hirom(void)
{
    const uint32_t size = 0x100000; /* 1 MB */
    uint8_t *rom = make_rom(size);
    cart_info_t ci;

    puts("hirom");
    write_header(rom, size, 0xFFC0, "PRUEBA HIROM", 0x31, 0x02, 0x01, 0x8100, 0x8100);
    EXPECT(cart_detect(rom, size, &ci) == 0, "cart_detect");
    EXPECT(ci.map == MAP_HIROM, "mapa = %d", ci.map);
    EXPECT(ci.fastrom == 1, "FastROM");
    EXPECT(ci.sram_size == 2048, "sram = %u", (unsigned)ci.sram_size);

    setup(MAP_HIROM, rom, size, ci.sram_size);
    EXPECT(bus_read(0xC00000) == rom[0], "$C0:0000");
    EXPECT(bus_read(0xC12345) == rom[0x12345], "$C1:2345");
    EXPECT(bus_read(0x008100) == 0x78, "$00:8100 (reset)");
    EXPECT(bus_read(0x00FFC0) == 'P', "cabecera en $00:FFC0");
    EXPECT(bus_read(0x05ABCD) == rom[0x5ABCD], "$05:ABCD");
    EXPECT(bus_read(0x450123) == rom[0x50123], "$45:0123");
    bus_write(0x7E0123, 0x3C);
    EXPECT(bus_read(0x850123) == 0x3C, "$85:0123 es WRAM, no ROM");
    EXPECT(bus_read(0xFFFFFF) == rom[0x3FFFFF % size], "$FF:FFFF espejo");
    EXPECT(bus_read(0x000000) == 0, "WRAM en $00:0000");

    /* SRAM de 2 KB en $20-$3F:6000-7FFF (camino lento, con espejo) */
    bus_write(0x206000, 0x42);
    EXPECT(sram[0] == 0x42, "SRAM $20:6000");
    EXPECT(bus_read(0x206800) == 0x42, "espejo 2 KB");
    EXPECT(bus_read(0xB07800) == 0x42, "espejo $B0");
    EXPECT(bus_read(0x106000) == 0x60, "$10:6000 bus abierto");
    free(rom);
}

static void test_hirom_big_sram(void)
{
    const uint32_t size = 0x40000;
    uint8_t *rom = make_rom(size);

    puts("hirom_sram_32k");
    setup(MAP_HIROM, rom, size, 0x8000);
    bus_write(0x216001, 0x77);  /* banco $21 -> segundo bloque de 8 KB */
    EXPECT(sram[0x2001] == 0x77, "SRAM banco $21");
    bus_write(0x246002, 0x78);  /* banco $24 -> bloque 4 -> 4*8K mod 32K = 0 */
    EXPECT(sram[0x0002] == 0x78, "SRAM espejo banco $24");
    EXPECT(mem_wr[(0x216000 >> 13)] != NULL, "SRAM de 32 KB por el camino rápido");
    free(rom);
}

static void test_exhirom(void)
{
    const uint32_t size = 0x600000; /* 6 MB */
    uint8_t *rom = make_rom(size);
    cart_info_t ci;

    puts("exhirom");
    write_header(rom, size, 0x40FFC0, "PRUEBA EXHIROM", 0x35, 0x00, 0x00, 0x8000, 0x408000);
    EXPECT(cart_detect(rom, size, &ci) == 0, "cart_detect");
    EXPECT(ci.map == MAP_EXHIROM, "mapa = %d", ci.map);
    EXPECT(ci.sram_size == 0, "sin SRAM");

    setup(MAP_EXHIROM, rom, size, 0);
    EXPECT(bus_read(0xC01234) == rom[0x1234], "$C0:1234");
    EXPECT(bus_read(0xFF1234) == rom[0x3F1234], "$FF:1234");
    EXPECT(bus_read(0x401234) == rom[0x401234], "$40:1234");
    EXPECT(bus_read(0x008000) == 0x78, "$00:8000 -> 0x408000");
    EXPECT(bus_read(0x808000) == rom[0x8000], "$80:8000 -> 0x8000");
    /* $7D:0000 = 0x400000 + 0x3D0000 = 0x7D0000, fuera de 6 MB -> espejo */
    EXPECT(mem_rom_offset(0x7D0000) == 0x5D0000, "$7D -> %X", mem_rom_offset(0x7D0000));
    free(rom);
}

static void test_detect_copier(void)
{
    puts("cabecera_copiadora");
    EXPECT(cart_copier_header(0x80200) == 512, "con cabecera");
    EXPECT(cart_copier_header(0x80000) == 0, "sin cabecera");
}

/* ------------------------------------------------------------------ */
/* E/S                                                                 */
/* ------------------------------------------------------------------ */

static uint8_t ppu_log[64];
static uint8_t ppu_regs[64];
static int ppu_writes;

static void fake_ppu_write(uint8_t reg, uint8_t v)
{
    if (ppu_writes < 64)
        ppu_log[ppu_writes] = reg;
    ppu_writes++;
    ppu_regs[reg & 63] = v;
}

static uint8_t fake_ppu_read(uint8_t reg)
{
    return ppu_regs[reg & 63];
}

static uint16_t joy_last_addr;
static int joy_autoreads;
static uint8_t fake_joy_read(uint16_t a) { joy_last_addr = a; return 0x5A; }
static void fake_joy_write(uint16_t a, uint8_t v) { joy_last_addr = a; (void)v; }
static void fake_joy_auto(void) { joy_autoreads++; }

static void test_io(void)
{
    const uint32_t size = 0x8000;
    uint8_t *rom = make_rom(size);
    int i;

    puts("registros_es");
    setup(MAP_LOROM, rom, size, 0);

    /* Multiplicación y división */
    bus_write(0x4202, 200);
    bus_write(0x4203, 150);
    EXPECT((bus_read(0x4216) | bus_read(0x4217) << 8) == 30000, "mul");
    bus_write(0x4204, 0x39);
    bus_write(0x4205, 0x30);   /* 12345 */
    bus_write(0x4206, 100);
    EXPECT((bus_read(0x4214) | bus_read(0x4215) << 8) == 123, "div");
    EXPECT((bus_read(0x4216) | bus_read(0x4217) << 8) == 45, "resto");
    bus_write(0x4206, 0);
    EXPECT((bus_read(0x4214) | bus_read(0x4215) << 8) == 0xFFFF, "div por 0");
    EXPECT((bus_read(0x804216) | bus_read(0x804217) << 8) == 12345, "div por 0 resto (banco $80)");

    /* Puerto WRAM $2180 */
    bus_write(0x2181, 0x00);
    bus_write(0x2182, 0x40);
    bus_write(0x2183, 0x01);   /* $7F:4000 */
    bus_write(0x2180, 0xAB);
    bus_write(0x2180, 0xCD);
    EXPECT(bus_read(0x7F4000) == 0xAB && bus_read(0x7F4001) == 0xCD, "WMDATA escritura");
    bus_write(0x2183, 0x01);
    bus_write(0x2182, 0x40);
    bus_write(0x2181, 0x01);
    EXPECT(bus_read(0x2180) == 0xCD, "WMDATA lectura");

    /* APU falsa */
    EXPECT(bus_read(0x2140) == 0xAA && bus_read(0x2141) == 0xBB, "APU $AA/$BB");
    bus_write(0x2140, 0xCC);
    EXPECT(bus_read(0x2140) == 0xCC && bus_read(0x2144) == 0xCC, "APU eco y espejo");

    /* Sin PPU conectada: bus abierto, sin colgarse */
    EXPECT(bus_read(0x2139) == 0x21, "PPU sin conectar = bus abierto");

    /* RDNMI / HVBJOY / NMI */
    bus_write(0x4200, 0x81);
    EXPECT((bus_read(0x4210) & 0x80) == 0, "RDNMI antes del VBlank");
    mem_hooks.joy_auto_read = fake_joy_auto;
    EXPECT(mem_vblank_start() == 1, "NMI activada");
    EXPECT(joy_autoreads == 1, "lectura automática de mandos");
    EXPECT(bus_read(0x4212) & 0x80, "HVBJOY en VBlank");
    EXPECT(bus_read(0x4210) & 0x80, "RDNMI = 1");
    EXPECT((bus_read(0x4210) & 0x80) == 0, "RDNMI se borra al leer");
    EXPECT((bus_read(0x4210) & 0x0F) == 0x02, "versión de la CPU");
    mem_vblank_end();
    EXPECT((bus_read(0x4212) & 0x80) == 0, "fin del VBlank");
    bus_write(0x4200, 0x01);
    EXPECT(mem_vblank_start() == 0, "NMI desactivada");
    bus_write(0x4200, 0x81);
    EXPECT(mem_take_nmi_request() == 1, "activar NMI en VBlank la lanza");
    EXPECT(mem_take_nmi_request() == 0, "solo una vez");
    mem_vblank_end();

    /* IRQ por línea */
    bus_write(0x4209, 100);
    bus_write(0x420A, 0);
    bus_write(0x4200, 0x20);
    EXPECT(mem_line_irq(99) == 0, "V-IRQ línea 99");
    EXPECT(mem_line_irq(100) == 1, "V-IRQ línea 100");
    EXPECT(mem_irq_pending(), "TIMEUP pendiente");
    EXPECT(bus_read(0x4211) & 0x80, "TIMEUP leído");
    EXPECT(!mem_irq_pending(), "TIMEUP se borra al leer");

    /* Mandos: se encaminan al módulo de entrada */
    mem_hooks.joy_read = fake_joy_read;
    mem_hooks.joy_write = fake_joy_write;
    EXPECT(bus_read(0x4218) == 0x5A && joy_last_addr == 0x4218, "JOY1L");
    EXPECT(bus_read(0x804017) == 0x5A && joy_last_addr == 0x4017, "$4017 banco $80");
    bus_write(0x4016, 1);
    EXPECT(joy_last_addr == 0x4016, "latch $4016");

    /* DMA: WRAM -> PPU en modo 1 ($2118/$2119 alternos) */
    mem_hooks.ppu_write = fake_ppu_write;
    mem_hooks.ppu_read = fake_ppu_read;
    for (i = 0; i < 8; i++)
        bus_write(0x7E0100 + i, (uint8_t)(0x10 + i));
    bus_write(0x4310, 0x01);   /* A->B, incremento, modo 1 */
    bus_write(0x4311, 0x18);
    bus_write(0x4312, 0x00);
    bus_write(0x4313, 0x01);
    bus_write(0x4314, 0x7E);
    bus_write(0x4315, 8);
    bus_write(0x4316, 0);
    ppu_writes = 0;
    bus_write(0x420B, 0x02);
    EXPECT(ppu_writes == 8, "DMA: %d escrituras", ppu_writes);
    EXPECT(ppu_log[0] == 0x18 && ppu_log[1] == 0x19 && ppu_log[2] == 0x18, "DMA modo 1");
    EXPECT(ppu_regs[0x19] == 0x17, "DMA último byte");
    EXPECT(bus_read(0x4315) == 0 && bus_read(0x4312) == 8, "DMA registros al terminar");
    EXPECT(mem_take_dma_cycles() > 0, "DMA consume ciclos");

    /* DMA B->A, dirección fija en el bus B */
    ppu_regs[0x39] = 0x66;
    bus_write(0x4300, 0x80);   /* B->A, modo 0 */
    bus_write(0x4301, 0x39);
    bus_write(0x4302, 0x00);
    bus_write(0x4303, 0x02);
    bus_write(0x4304, 0x7E);
    bus_write(0x4305, 3);
    bus_write(0x4306, 0);
    bus_write(0x420B, 0x01);
    EXPECT(bus_read(0x7E0200) == 0x66 && bus_read(0x7E0202) == 0x66, "DMA B->A");

    /* DMA hacia el puerto WRAM (ROM -> WRAM, típico para descomprimir) */
    bus_write(0x2181, 0x00);
    bus_write(0x2182, 0x30);
    bus_write(0x2183, 0x00);
    bus_write(0x4300, 0x08);   /* A->B, fijo, modo 0 */
    bus_write(0x4301, 0x80);
    bus_write(0x4302, 0x05);
    bus_write(0x4303, 0x80);
    bus_write(0x4304, 0x00);
    bus_write(0x4305, 4);
    bus_write(0x4306, 0);
    bus_write(0x420B, 0x01);
    EXPECT(bus_read(0x7E3000) == rom[5] && bus_read(0x7E3003) == rom[5], "DMA a WMDATA");

    memset(&mem_hooks, 0, sizeof mem_hooks);
    free(rom);
}

/* La CPU ejecuta el autotest desde un cartucho LoROM a través del mapa real */
static void test_cpu_on_lorom(void)
{
    static uint8_t rom[0x8000];
    cpu_t cpu;
    int steps = 0;

    puts("cpu_en_lorom");
    memset(rom, 0xFF, sizeof rom);
    memcpy(rom, demo_rom, sizeof demo_rom);
    rom[0x7FFC] = DEMO_ROM_BASE & 0xFF;
    rom[0x7FFD] = DEMO_ROM_BASE >> 8;
    setup(MAP_LOROM, rom, sizeof rom, 0);

    cpu_reset(&cpu);
    while (!cpu.stopped && steps++ < 100000)
        cpu_step(&cpu);
    EXPECT(cpu.stopped, "no llegó a STP");
    EXPECT((bus_read(0x7E0000) | bus_read(0x7E0001) << 8) == 0x13BA, "suma");
    EXPECT((bus_read(0x7E0002) | bus_read(0x7E0003) << 8) == 0x5555, "decimal");
    EXPECT((bus_read(0x7E0004) | bus_read(0x7E0005) << 8) == 0xBEEF, "jsr");
}

int main(void)
{
    test_lorom();
    test_lorom_fill_and_short();
    test_mirror_non_pow2();
    test_hirom();
    test_hirom_big_sram();
    test_exhirom();
    test_detect_copier();
    test_io();
    test_cpu_on_lorom();

    if (failures) {
        printf("%d fallos\n", failures);
        return 1;
    }
    puts("Todos los tests del mapa de memoria pasan");
    return 0;
}
