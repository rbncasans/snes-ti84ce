/* Tests del HDMA y del bucle principal (CPU + memoria + PPU + mando) en PC. */
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "emu.h"
#include "input/joypad.h"
#include "mem/mem.h"
#include "ppu/ppu.h"

uint8_t kb_Data[8]; /* teclado falso (stub de keypadc.h) */

static int failures;

#define EXPECT(cond, ...) do { \
    if (!(cond)) { failures++; printf("  FALLO %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t wram[MEM_WRAM_SIZE];
static uint8_t rom[0x8000];
static mem_chunk_t chunk;
static uint8_t vram[0x10000];
static uint8_t screen[256 * 240];

static void setup_lorom(void)
{
    mem_config_t cfg;
    uint8_t i;

    chunk.data = rom;
    chunk.len = sizeof rom;
    chunk.fill = 0xFF;
    memset(&cfg, 0, sizeof cfg);
    cfg.map = MAP_LOROM;
    cfg.rom_size = sizeof rom;
    cfg.nchunks = 1;
    cfg.chunks = &chunk;
    memset(wram, 0, sizeof wram);
    for (i = 0; i < MEM_WRAM_PAGES; i++)
        cfg.wram[i] = wram + (uint32_t)i * MEM_PAGE_SIZE;
    mem_init(&cfg);
}

/* Registro de escrituras en el bus B: línea, registro y valor */
#define MAX_LOG 64
static struct { uint16_t line; uint8_t reg, val; } wlog[MAX_LOG];
static int nlog;
static uint16_t cur_line;

static void log_write(uint8_t reg, uint8_t v)
{
    if (nlog < MAX_LOG) {
        wlog[nlog].line = cur_line;
        wlog[nlog].reg = reg;
        wlog[nlog].val = v;
        nlog++;
    }
}

static void put(uint32_t addr, const uint8_t *bytes, uint32_t n)
{
    memcpy(rom + (addr & 0x7FFF), bytes, n);
}

/* HDMA directo: modo 1 (dos registros), entrada repetida y no repetida */
static void test_hdma_direct(void)
{
    /* 2 líneas sin repetir (1 transferencia), 3 líneas repitiendo, fin */
    static const uint8_t table[] = {
        0x02, 0x11, 0x12,
        0x83, 0x21, 0x22, 0x31, 0x32, 0x41, 0x42,
        0x00,
    };
    uint16_t v;

    puts("hdma_directo");
    memset(rom, 0xFF, sizeof rom);
    put(0x9000, table, sizeof table);
    setup_lorom();
    mem_hooks.ppu_write = log_write;
    nlog = 0;

    bus_write(0x4300, 0x01);  /* modo 1: $2118, $2119 */
    bus_write(0x4301, 0x18);
    bus_write(0x4302, 0x00);
    bus_write(0x4303, 0x90);
    bus_write(0x4304, 0x00);
    bus_write(0x420C, 0x01);

    mem_hdma_init();
    for (v = 0; v < 10; v++) {
        cur_line = v;
        mem_hdma_line();
    }
    EXPECT(nlog == 8, "%d escrituras (esperaba 8)", nlog);
    EXPECT(wlog[0].line == 0 && wlog[0].reg == 0x18 && wlog[0].val == 0x11, "línea 0");
    EXPECT(wlog[1].line == 0 && wlog[1].reg == 0x19 && wlog[1].val == 0x12, "línea 0 b");
    EXPECT(wlog[2].line == 2 && wlog[2].val == 0x21, "línea 2: %d %02X", wlog[2].line, wlog[2].val);
    EXPECT(wlog[4].line == 3 && wlog[4].val == 0x31, "línea 3");
    EXPECT(wlog[6].line == 4 && wlog[6].val == 0x41 && wlog[7].val == 0x42, "línea 4");
    EXPECT(mem_take_dma_cycles() > 0, "el HDMA consume ciclos");

    /* Al reiniciar el fotograma vuelve a empezar la tabla */
    nlog = 0;
    mem_hdma_init();
    cur_line = 0;
    mem_hdma_line();
    EXPECT(nlog == 2 && wlog[0].val == 0x11, "reinicio de la tabla");

    /* El DMA normal no usa un canal ocupado por el HDMA */
    nlog = 0;
    bus_write(0x4305, 4);
    bus_write(0x4306, 0);
    bus_write(0x420B, 0x01);
    EXPECT(nlog == 0, "DMA en canal de HDMA");
}

/* HDMA indirecto: la tabla contiene punteros a los datos */
static void test_hdma_indirect(void)
{
    static const uint8_t table[] = { 0x01, 0x00, 0xA0, 0x81, 0x10, 0xA0, 0x00 };
    static const uint8_t d1[] = { 0x55 };
    static const uint8_t d2[] = { 0x66, 0x77 };
    uint16_t v;

    puts("hdma_indirecto");
    memset(rom, 0xFF, sizeof rom);
    put(0x9000, table, sizeof table);
    put(0xA000, d1, sizeof d1);
    put(0xA010, d2, sizeof d2);
    setup_lorom();
    mem_hooks.ppu_write = log_write;
    nlog = 0;

    bus_write(0x4320, 0x40);  /* indirecto, modo 0 */
    bus_write(0x4321, 0x00);  /* $2100 */
    bus_write(0x4322, 0x00);
    bus_write(0x4323, 0x90);
    bus_write(0x4324, 0x00);
    bus_write(0x4327, 0x00);  /* banco de los datos */
    bus_write(0x420C, 0x04);

    mem_hdma_init();
    for (v = 0; v < 5; v++) {
        cur_line = v;
        mem_hdma_line();
    }
    EXPECT(nlog == 2, "%d escrituras (esperaba 2)", nlog);
    EXPECT(wlog[0].line == 0 && wlog[0].val == 0x55, "línea 0");
    EXPECT(wlog[1].line == 1 && wlog[1].val == 0x66, "línea 1: %d %02X", wlog[1].line, wlog[1].val);
}

/* Programa completo: NMI + lectura automática del mando + HDMA a $2100 */
static const uint8_t prog[] = {
    0x78,                   /* 8000 SEI            */
    0xA9, 0x81,             /*      LDA #$81       */
    0x8D, 0x00, 0x42,       /*      STA $4200      NMI + mando */
    0xA9, 0x00,             /*      LDA #$00       */
    0x8D, 0x00, 0x43,       /*      STA $4300      modo 0 */
    0x8D, 0x01, 0x43,       /*      STA $4301      $2100 */
    0x8D, 0x02, 0x43,       /*      STA $4302      */
    0x8D, 0x04, 0x43,       /*      STA $4304      */
    0xA9, 0x81,             /*      LDA #$81       */
    0x8D, 0x03, 0x43,       /*      STA $4303      tabla en $8100 */
    0xA9, 0x01,             /*      LDA #$01       */
    0x8D, 0x0C, 0x42,       /*      STA $420C      */
    0xCB,                   /* 8020 WAI            */
    0x80, 0xFD,             /*      BRA $8020      */
};
static const uint8_t nmi[] = {
    0xE6, 0x10,             /* 8030 INC $10        */
    0xAD, 0x19, 0x42,       /*      LDA $4219      */
    0x85, 0x12,             /*      STA $12        */
    0x40,                   /*      RTI            */
};
static const uint8_t hdma_table[] = { 0x0A, 0x0F, 0x01, 0x05, 0x00 };

static void spy_ppu_write(uint8_t reg, uint8_t v)
{
    ppu_write(reg, v);
    if (reg == 0x00 && nlog < MAX_LOG) {
        wlog[nlog].line = ppu.vcount;
        wlog[nlog].val = v;
        nlog++;
    }
}

static void test_main_loop(void)
{
    int f;

    puts("bucle_principal");
    memset(rom, 0xFF, sizeof rom);
    put(0x8000, prog, sizeof prog);
    put(0x8030, nmi, sizeof nmi);
    put(0x8100, hdma_table, sizeof hdma_table);
    rom[0x7FFA] = 0x30; rom[0x7FFB] = 0x80;   /* NMI (emulación) */
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;   /* RESET */
    setup_lorom();

    emu_init(vram, screen, 256);
    mem_hooks.ppu_write = spy_ppu_write;
    memset(kb_Data, 0, sizeof kb_Data);
    kb_Data[2] = 0x80;    /* [alpha] = B */
    nlog = 0;

    for (f = 0; f < 3; f++)
        emu_frame(true);

    EXPECT(bus_read(0x7E0010) == 3, "NMI: %d (esperaba 3)", bus_read(0x7E0010));
    EXPECT(bus_read(0x7E0012) == 0x80, "mando en $4219: %02X", bus_read(0x7E0012));
    EXPECT(!emu_cpu.stopped, "la CPU se ha parado");
    /* El primer fotograma arranca sin HDMA (se activa durante él); en los
     * dos siguientes: $0F en la línea 0 y $05 en la línea 10 */
    EXPECT(nlog == 4, "%d escrituras de INIDISP (esperaba 4)", nlog);
    if (nlog == 4) {
        EXPECT(wlog[0].line == 0 && wlog[0].val == 0x0F, "línea %d %02X", wlog[0].line, wlog[0].val);
        EXPECT(wlog[1].line == 10 && wlog[1].val == 0x05, "línea %d %02X", wlog[1].line, wlog[1].val);
        EXPECT(wlog[2].line == 0 && wlog[3].line == 10, "segundo fotograma");
    }
    EXPECT(ppu.inidisp == 0x05, "INIDISP final %02X", ppu.inidisp);

    /* Salir con [clear] */
    kb_Data[6] = 0x40;
    emu_frame(false);
    EXPECT(emu_quit_requested(), "no detecta [clear]");
}

int main(void)
{
    test_hdma_direct();
    test_hdma_indirect();
    test_main_loop();

    if (failures) {
        printf("%d fallos\n", failures);
        return 1;
    }
    puts("Todos los tests del HDMA y del bucle principal pasan");
    return 0;
}
