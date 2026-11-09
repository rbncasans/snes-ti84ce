/* Tests de la CPU 65C816 en PC: gcc, sin dependencias externas. */
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "cpu/65c816.h"
#include "demo_rom.h"

static uint8_t mem[1 << 24];
static cpu_t cpu;
static int failures;

uint8_t bus_read(uint32_t addr) { return mem[addr]; }
void bus_write(uint32_t addr, uint8_t v) { mem[addr] = v; }

#define EXPECT(cond, ...) do { \
    if (!(cond)) { failures++; printf("  FALLO %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t m16(uint32_t a) { return mem[a] | (mem[a + 1] << 8); }

static void load(uint32_t addr, const uint8_t *code, size_t n)
{
    memcpy(&mem[addr], code, n);
}

static void boot(uint16_t reset_vec)
{
    mem[0xFFFC] = reset_vec & 0xFF;
    mem[0xFFFD] = reset_vec >> 8;
    cpu_reset(&cpu);
}

static void run(int max_steps)
{
    while (!cpu.stopped && max_steps-- > 0)
        cpu_step(&cpu);
}

static void test_demo_rom(void)
{
    puts("demo_rom");
    memset(mem, 0, sizeof mem);
    load(DEMO_ROM_BASE, demo_rom, sizeof demo_rom);
    boot(DEMO_ROM_BASE);
    run(100000);
    EXPECT(cpu.stopped, "no llegó a STP");
    EXPECT(m16(0) == 0x13BA, "suma = %04X", m16(0));
    EXPECT(m16(2) == 0x5555, "decimal = %04X", m16(2));
    EXPECT(m16(4) == 0xBEEF, "jsr = %04X", m16(4));
    EXPECT(cpu.e == 0, "debería estar en modo nativo");
}

static void test_brk_emulation(void)
{
    static const uint8_t code[] = { 0x00, 0x99, 0xDB };   /* BRK #$99 */
    static const uint8_t handler[] = { 0xA9, 0x42, 0x40 }; /* LDA #$42 ; RTI */
    puts("brk_emulacion");
    memset(mem, 0, sizeof mem);
    load(0x8000, code, sizeof code);
    load(0x9000, handler, sizeof handler);
    mem[0xFFFE] = 0x00; mem[0xFFFF] = 0x90;
    boot(0x8000);
    cpu_step(&cpu);
    EXPECT(cpu.pc == 0x9000, "pc = %04X", cpu.pc);
    EXPECT(cpu.s == 0x01FC, "s = %04X", cpu.s);
    EXPECT(mem[0x01FD] & FLAG_X, "B no puesto en P apilado");
    EXPECT(m16(0x01FE) == 0x8002, "pc apilado = %04X", m16(0x01FE));
    cpu_step(&cpu);
    cpu_step(&cpu);
    EXPECT(cpu.pc == 0x8002 && (cpu.a & 0xFF) == 0x42, "tras RTI pc=%04X a=%04X", cpu.pc, cpu.a);
}

static void test_nmi_native(void)
{
    static const uint8_t code[] = { 0x18, 0xFB, 0xEA, 0xEA, 0xDB }; /* CLC XCE NOP NOP STP */
    static const uint8_t handler[] = { 0xE8, 0x40 };               /* INX ; RTI */
    puts("nmi_nativo");
    memset(mem, 0, sizeof mem);
    load(0x8000, code, sizeof code);
    load(0xA000, handler, sizeof handler);
    mem[0xFFEA] = 0x00; mem[0xFFEB] = 0xA0;
    boot(0x8000);
    cpu_step(&cpu); cpu_step(&cpu);
    cpu_nmi(&cpu);
    cpu_step(&cpu);
    EXPECT(cpu.pc == 0xA000 && cpu.pb == 0, "pc = %02X:%04X", cpu.pb, cpu.pc);
    run(10);
    EXPECT(cpu.x == 1, "x = %04X", cpu.x);
    EXPECT(cpu.pc == 0x8005, "pc final = %04X", cpu.pc);
}

static void test_irq_masked_and_wai(void)
{
    static const uint8_t code[] = { 0xCB, 0xA9, 0x07, 0xDB }; /* WAI ; LDA #7 ; STP */
    puts("wai_con_irq_enmascarada");
    memset(mem, 0, sizeof mem);
    load(0x8000, code, sizeof code);
    boot(0x8000); /* I = 1 tras reset */
    cpu_step(&cpu);
    EXPECT(cpu.waiting, "WAI no detuvo la CPU");
    cpu_step(&cpu);
    EXPECT(cpu.waiting, "debería seguir esperando");
    cpu_set_irq(&cpu, 1);
    run(10);
    EXPECT((cpu.a & 0xFF) == 7, "a = %04X (WAI debía continuar sin atender la IRQ)", cpu.a);
}

static void test_mvn(void)
{
    static const uint8_t code[] = {
        0x18, 0xFB, 0xC2, 0x30,       /* CLC XCE REP #$30 */
        0xA9, 0x03, 0x00,             /* LDA #3 (4 bytes) */
        0xA2, 0x00, 0x10,             /* LDX #$1000       */
        0xA0, 0x00, 0x20,             /* LDY #$2000       */
        0x54, 0x7E, 0x7F,             /* MVN $7F -> $7E   */
        0xDB,
    };
    puts("mvn");
    memset(mem, 0, sizeof mem);
    load(0x8000, code, sizeof code);
    memcpy(&mem[0x7F1000], "SNES", 4);
    boot(0x8000);
    run(100);
    EXPECT(memcmp(&mem[0x7E2000], "SNES", 4) == 0, "copia incorrecta");
    EXPECT(cpu.a == 0xFFFF && cpu.x == 0x1004 && cpu.y == 0x2004 && cpu.db == 0x7E,
           "a=%04X x=%04X y=%04X db=%02X", cpu.a, cpu.x, cpu.y, cpu.db);
}

static void test_sbc_decimal_8bit(void)
{
    static const uint8_t code[] = { 0xF8, 0x38, 0xA9, 0x10, 0xE9, 0x01, 0xDB }; /* SED SEC LDA #$10 SBC #$01 */
    puts("sbc_decimal");
    memset(mem, 0, sizeof mem);
    load(0x8000, code, sizeof code);
    boot(0x8000);
    run(10);
    EXPECT((cpu.a & 0xFF) == 0x09 && (cpu.p & FLAG_C), "a=%02X p=%02X", cpu.a & 0xFF, cpu.p);
}

int main(void)
{
    test_demo_rom();
    test_brk_emulation();
    test_nmi_native();
    test_irq_masked_and_wai();
    test_mvn();
    test_sbc_decimal_8bit();
    if (failures) {
        printf("%d fallo(s)\n", failures);
        return 1;
    }
    puts("Todos los tests pasan");
    return 0;
}
