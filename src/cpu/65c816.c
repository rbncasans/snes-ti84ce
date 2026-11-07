/*
 * Intérprete de la CPU WDC 65C816 (núcleo del Ricoh 5A22 de la SNES).
 *
 * Implementación original. Cubre los 256 opcodes, los modos de 8/16 bits
 * (flags M y X), el modo emulación, la aritmética decimal y las
 * interrupciones (NMI, IRQ, BRK, COP). Los ciclos son aproximados:
 * se cuenta un ciclo por acceso al bus más los ciclos internos principales.
 */

#include <stdbool.h>

#include "65c816.h"
#include "../bus.h"

/* CPU en ejecución (evita pasar el puntero a cada función auxiliar). */
static cpu_t *c;

/* Dirección efectiva calculada por el último modo de direccionamiento. */
static uint32_t ea;
/* true si el segundo byte de un acceso de 16 bits debe quedarse en el banco 0
 * (página directa y pila); false si cruza al siguiente banco (datos). */
static bool ea_bank0;

#define MFLAG (c->p & FLAG_M)
#define XFLAG (c->p & FLAG_X)

/* ------------------------------------------------------------------ */
/* Acceso al bus                                                       */
/* ------------------------------------------------------------------ */

static uint8_t rd8(uint32_t addr)
{
    c->cycles++;
    return bus_read(addr & 0xFFFFFF);
}

static void wr8(uint32_t addr, uint8_t v)
{
    c->cycles++;
    bus_write(addr & 0xFFFFFF, v);
}

static uint32_t next_addr(uint32_t addr, bool bank0)
{
    if (bank0)
        return (addr & 0xFF0000) | ((addr + 1) & 0xFFFF);
    return (addr + 1) & 0xFFFFFF;
}

static uint16_t rd16(uint32_t addr, bool bank0)
{
    uint16_t lo = rd8(addr);
    return lo | ((uint16_t)rd8(next_addr(addr, bank0)) << 8);
}

static void wr16(uint32_t addr, uint16_t v, bool bank0)
{
    wr8(addr, (uint8_t)v);
    wr8(next_addr(addr, bank0), (uint8_t)(v >> 8));
}

static void io(void)
{
    c->cycles++;
}

static uint8_t fetch8(void)
{
    uint8_t v = rd8(((uint32_t)c->pb << 16) | c->pc);
    c->pc++;
    return v;
}

static uint16_t fetch16(void)
{
    uint16_t lo = fetch8();
    return lo | ((uint16_t)fetch8() << 8);
}

static uint32_t fetch24(void)
{
    uint32_t lo = fetch16();
    return lo | ((uint32_t)fetch8() << 16);
}

/* ------------------------------------------------------------------ */
/* Pila                                                                */
/* ------------------------------------------------------------------ */

/* Las instrucciones nuevas del 65C816 (PHD, PLD, PEA, JSL...) acceden a la
 * pila sin limitarse a la página 1 incluso en modo emulación; al terminar,
 * el byte alto de S vuelve a ser $01. */
static bool stack_linear;

static void dec_s(void)
{
    if (c->e && !stack_linear)
        c->s = 0x0100 | ((c->s - 1) & 0xFF);
    else
        c->s--;
}

static void inc_s(void)
{
    if (c->e && !stack_linear)
        c->s = 0x0100 | ((c->s + 1) & 0xFF);
    else
        c->s++;
}

static void push8(uint8_t v)
{
    wr8(c->s, v);
    dec_s();
}

static void push16(uint16_t v)
{
    push8((uint8_t)(v >> 8));
    push8((uint8_t)v);
}

static uint8_t pull8(void)
{
    inc_s();
    return rd8(c->s);
}

static uint16_t pull16(void)
{
    uint16_t lo = pull8();
    return lo | ((uint16_t)pull8() << 8);
}

/* ------------------------------------------------------------------ */
/* Flags                                                               */
/* ------------------------------------------------------------------ */

static void set_nz8(uint8_t v)
{
    c->p &= (uint8_t)~(FLAG_N | FLAG_Z);
    if (v == 0)
        c->p |= FLAG_Z;
    c->p |= v & FLAG_N;
}

static void set_nz16(uint16_t v)
{
    c->p &= (uint8_t)~(FLAG_N | FLAG_Z);
    if (v == 0)
        c->p |= FLAG_Z;
    if (v & 0x8000)
        c->p |= FLAG_N;
}

static void set_flag(uint8_t flag, bool on)
{
    if (on)
        c->p |= flag;
    else
        c->p &= (uint8_t)~flag;
}

/* Fija P respetando las restricciones del modo emulación y del flag X. */
static void set_p(uint8_t v)
{
    c->p = v;
    if (c->e)
        c->p |= FLAG_M | FLAG_X;
    if (c->p & FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
}

/* ------------------------------------------------------------------ */
/* Modos de direccionamiento (calculan ea / ea_bank0)                  */
/* ------------------------------------------------------------------ */

static uint32_t dbank(void)
{
    return (uint32_t)c->db << 16;
}

/* Página directa con índice opcional. En modo emulación con DL = 0 el
 * índice se queda dentro de la página (comportamiento del 6502). */
static uint16_t dp_addr(bool indexed, uint16_t index)
{
    uint8_t off = fetch8();
    if (c->d & 0xFF)
        io();
    if (!indexed)
        return (uint16_t)(c->d + off);
    io();
    if (c->e && (c->d & 0xFF) == 0)
        return (uint16_t)(c->d | (uint8_t)(off + index));
    return (uint16_t)(c->d + off + index);
}

static void am_dp(void)   { ea = dp_addr(false, 0); ea_bank0 = true; }
static void am_dpx(void)  { ea = dp_addr(true, c->x);     ea_bank0 = true; }
static void am_dpy(void)  { ea = dp_addr(true, c->y);     ea_bank0 = true; }

static void am_idp(void)  /* (dp) */
{
    uint16_t p = dp_addr(false, 0);
    ea = dbank() | rd16(p, true);
    ea_bank0 = false;
}

static void am_idpx(void) /* (dp,X) */
{
    uint16_t p = dp_addr(true, c->x);
    ea = dbank() | rd16(p, true);
    ea_bank0 = false;
}

static void am_idpy(void) /* (dp),Y */
{
    uint16_t p = dp_addr(false, 0);
    ea = ((dbank() | rd16(p, true)) + c->y) & 0xFFFFFF;
    ea_bank0 = false;
}

static uint32_t rd24_dp(uint16_t p)
{
    uint32_t lo = rd16(p, true);
    return lo | ((uint32_t)rd8((uint16_t)(p + 2)) << 16);
}

static void am_ildp(void) /* [dp] */
{
    ea = rd24_dp(dp_addr(false, 0));
    ea_bank0 = false;
}

static void am_ildpy(void) /* [dp],Y */
{
    ea = (rd24_dp(dp_addr(false, 0)) + c->y) & 0xFFFFFF;
    ea_bank0 = false;
}

static void am_abs(void)  { ea = dbank() | fetch16(); ea_bank0 = false; }

static void am_absx(void)
{
    ea = ((dbank() | fetch16()) + c->x) & 0xFFFFFF;
    io();
    ea_bank0 = false;
}

static void am_absy(void)
{
    ea = ((dbank() | fetch16()) + c->y) & 0xFFFFFF;
    io();
    ea_bank0 = false;
}

static void am_long(void)  { ea = fetch24(); ea_bank0 = false; }

static void am_longx(void)
{
    ea = (fetch24() + c->x) & 0xFFFFFF;
    ea_bank0 = false;
}

static void am_sr(void) /* sr,S */
{
    ea = (uint16_t)(c->s + fetch8());
    io();
    ea_bank0 = true;
}

static void am_isry(void) /* (sr,S),Y */
{
    uint16_t p = (uint16_t)(c->s + fetch8());
    io();
    ea = ((dbank() | rd16(p, true)) + c->y) & 0xFFFFFF;
    io();
    ea_bank0 = false;
}

/* Lectura/escritura en ea con el ancho del acumulador (M) o índices (X). */
static uint16_t read_m(void)
{
    return MFLAG ? rd8(ea) : rd16(ea, ea_bank0);
}

static uint16_t read_x(void)
{
    return XFLAG ? rd8(ea) : rd16(ea, ea_bank0);
}

static void write_m(uint16_t v)
{
    if (MFLAG)
        wr8(ea, (uint8_t)v);
    else
        wr16(ea, v, ea_bank0);
}

static void write_x(uint16_t v)
{
    if (XFLAG)
        wr8(ea, (uint8_t)v);
    else
        wr16(ea, v, ea_bank0);
}

static uint16_t imm_m(void)
{
    return MFLAG ? fetch8() : fetch16();
}

static uint16_t imm_x(void)
{
    return XFLAG ? fetch8() : fetch16();
}

/* ------------------------------------------------------------------ */
/* Operaciones                                                         */
/* ------------------------------------------------------------------ */

static void set_a(uint16_t v)
{
    if (MFLAG) {
        c->a = (c->a & 0xFF00) | (v & 0xFF);
        set_nz8((uint8_t)v);
    } else {
        c->a = v;
        set_nz16(v);
    }
}

static void op_ora(uint16_t v) { set_a(c->a | v); }
static void op_and(uint16_t v) { set_a(c->a & v); }
static void op_eor(uint16_t v) { set_a(c->a ^ v); }
static void op_lda(uint16_t v) { set_a(v); }

static void op_adc(uint16_t v)
{
    int32_t a = c->a, r;
    int32_t carry = c->p & FLAG_C;
    bool dec = (c->p & FLAG_D) != 0;

    if (MFLAG) {
        a &= 0xFF;
        v &= 0xFF;
        if (!dec) {
            r = a + v + carry;
        } else {
            r = (a & 0x0F) + (v & 0x0F) + carry;
            if (r > 0x09) r += 0x06;
            carry = r > 0x0F;
            r = (a & 0xF0) + (v & 0xF0) + (carry << 4) + (r & 0x0F);
        }
        set_flag(FLAG_V, (~(a ^ v) & (a ^ r) & 0x80) != 0);
        if (dec && r > 0x9F) r += 0x60;
        set_flag(FLAG_C, r > 0xFF);
        set_a((uint16_t)r);
    } else {
        if (!dec) {
            r = a + v + carry;
        } else {
            r = (a & 0x000F) + (v & 0x000F) + carry;
            if (r > 0x0009) r += 0x0006;
            carry = r > 0x000F;
            r = (a & 0x00F0) + (v & 0x00F0) + (carry << 4) + (r & 0x000F);
            if (r > 0x009F) r += 0x0060;
            carry = r > 0x00FF;
            r = (a & 0x0F00) + (v & 0x0F00) + (carry << 8) + (r & 0x00FF);
            if (r > 0x09FF) r += 0x0600;
            carry = r > 0x0FFF;
            r = (a & 0xF000) + (v & 0xF000) + (carry << 12) + (r & 0x0FFF);
        }
        set_flag(FLAG_V, (~(a ^ v) & (a ^ r) & 0x8000) != 0);
        if (dec && r > 0x9FFF) r += 0x6000;
        set_flag(FLAG_C, r > 0xFFFF);
        set_a((uint16_t)r);
    }
}

static void op_sbc(uint16_t v)
{
    int32_t a = c->a, r;
    int32_t carry = c->p & FLAG_C;
    bool dec = (c->p & FLAG_D) != 0;

    if (MFLAG) {
        a &= 0xFF;
        v = ~v & 0xFF;
        if (!dec) {
            r = a + v + carry;
        } else {
            r = (a & 0x0F) + (v & 0x0F) + carry;
            if (r <= 0x0F) r -= 0x06;
            carry = r > 0x0F;
            r = (a & 0xF0) + (v & 0xF0) + (carry << 4) + (r & 0x0F);
        }
        set_flag(FLAG_V, (~(a ^ v) & (a ^ r) & 0x80) != 0);
        if (dec && r <= 0xFF) r -= 0x60;
        set_flag(FLAG_C, r > 0xFF);
        set_a((uint16_t)r);
    } else {
        v = ~v & 0xFFFF;
        if (!dec) {
            r = a + v + carry;
        } else {
            r = (a & 0x000F) + (v & 0x000F) + carry;
            if (r <= 0x000F) r -= 0x0006;
            carry = r > 0x000F;
            r = (a & 0x00F0) + (v & 0x00F0) + (carry << 4) + (r & 0x000F);
            if (r <= 0x00FF) r -= 0x0060;
            carry = r > 0x00FF;
            r = (a & 0x0F00) + (v & 0x0F00) + (carry << 8) + (r & 0x00FF);
            if (r <= 0x0FFF) r -= 0x0600;
            carry = r > 0x0FFF;
            r = (a & 0xF000) + (v & 0xF000) + (carry << 12) + (r & 0x0FFF);
        }
        set_flag(FLAG_V, (~(a ^ v) & (a ^ r) & 0x8000) != 0);
        if (dec && r <= 0xFFFF) r -= 0x6000;
        set_flag(FLAG_C, r > 0xFFFF);
        set_a((uint16_t)r);
    }
}

static void compare(uint16_t reg, uint16_t v, bool eight)
{
    if (eight) {
        int32_t r = (int32_t)(reg & 0xFF) - (int32_t)(v & 0xFF);
        set_flag(FLAG_C, r >= 0);
        set_nz8((uint8_t)r);
    } else {
        int32_t r = (int32_t)reg - (int32_t)v;
        set_flag(FLAG_C, r >= 0);
        set_nz16((uint16_t)r);
    }
}

static void op_cmp(uint16_t v) { compare(c->a, v, MFLAG); }

static void op_bit(uint16_t v, bool immediate)
{
    if (MFLAG) {
        set_flag(FLAG_Z, ((c->a & v) & 0xFF) == 0);
        if (!immediate) {
            set_flag(FLAG_N, (v & 0x80) != 0);
            set_flag(FLAG_V, (v & 0x40) != 0);
        }
    } else {
        set_flag(FLAG_Z, (c->a & v) == 0);
        if (!immediate) {
            set_flag(FLAG_N, (v & 0x8000) != 0);
            set_flag(FLAG_V, (v & 0x4000) != 0);
        }
    }
}

static void set_x_reg(uint16_t *reg, uint16_t v)
{
    if (XFLAG) {
        *reg = v & 0xFF;
        set_nz8((uint8_t)v);
    } else {
        *reg = v;
        set_nz16(v);
    }
}

/* Operaciones de lectura-modificación-escritura sobre un valor del ancho M. */
enum { RMW_ASL, RMW_LSR, RMW_ROL, RMW_ROR, RMW_INC, RMW_DEC };

static uint16_t rmw(int kind, uint16_t v)
{
    uint16_t msb = MFLAG ? 0x80 : 0x8000;
    uint16_t mask = MFLAG ? 0xFF : 0xFFFF;
    uint16_t carry_in = (c->p & FLAG_C) ? 1 : 0;

    switch (kind) {
    case RMW_ASL:
        set_flag(FLAG_C, (v & msb) != 0);
        v = (uint16_t)(v << 1);
        break;
    case RMW_LSR:
        set_flag(FLAG_C, (v & 1) != 0);
        v = (uint16_t)((v & mask) >> 1);
        break;
    case RMW_ROL:
        set_flag(FLAG_C, (v & msb) != 0);
        v = (uint16_t)((v << 1) | carry_in);
        break;
    case RMW_ROR:
        set_flag(FLAG_C, (v & 1) != 0);
        v = (uint16_t)(((v & mask) >> 1) | (carry_in ? msb : 0));
        break;
    case RMW_INC:
        v++;
        break;
    default:
        v--;
        break;
    }
    v &= mask;
    if (MFLAG)
        set_nz8((uint8_t)v);
    else
        set_nz16(v);
    return v;
}

static void rmw_mem(int kind)
{
    uint16_t v = read_m();
    io();
    write_m(rmw(kind, v));
}

static void rmw_acc(int kind)
{
    io();
    if (MFLAG)
        c->a = (c->a & 0xFF00) | rmw(kind, c->a & 0xFF);
    else
        c->a = rmw(kind, c->a);
}

static void tsb_trb(bool set)
{
    uint16_t v = read_m();
    io();
    if (MFLAG)
        set_flag(FLAG_Z, ((c->a & v) & 0xFF) == 0);
    else
        set_flag(FLAG_Z, (c->a & v) == 0);
    write_m(set ? (v | c->a) : (v & ~c->a));
}

static void branch(bool cond)
{
    int8_t off = (int8_t)fetch8();
    if (cond) {
        io();
        c->pc = (uint16_t)(c->pc + off);
    }
}

/* ------------------------------------------------------------------ */
/* Interrupciones                                                      */
/* ------------------------------------------------------------------ */

enum { VEC_COP, VEC_BRK, VEC_NMI, VEC_IRQ };

static void interrupt(int kind)
{
    static const uint16_t native_vec[4] = { 0xFFE4, 0xFFE6, 0xFFEA, 0xFFEE };
    static const uint16_t emu_vec[4]    = { 0xFFF4, 0xFFFE, 0xFFFA, 0xFFFE };

    io();
    io();
    if (!c->e)
        push8(c->pb);
    push16(c->pc);
    if (c->e) {
        /* En emulación, el bit B distingue BRK de IRQ/NMI. */
        uint8_t p = c->p;
        if (kind == VEC_BRK || kind == VEC_COP)
            p |= FLAG_X;
        else
            p &= (uint8_t)~FLAG_X;
        push8(p);
    } else {
        push8(c->p);
    }
    c->p |= FLAG_I;
    c->p &= (uint8_t)~FLAG_D;
    c->pb = 0;
    c->pc = rd16(c->e ? emu_vec[kind] : native_vec[kind], false);
}

void cpu_nmi(cpu_t *cpu)
{
    cpu->nmi_pending = 1;
}

void cpu_set_irq(cpu_t *cpu, uint8_t level)
{
    cpu->irq_line = level;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

void cpu_reset(cpu_t *cpu)
{
    c = cpu;
    c->a = 0;
    c->x = 0;
    c->y = 0;
    c->s = 0x01FF;
    c->d = 0;
    c->db = 0;
    c->pb = 0;
    c->e = 1;
    c->p = FLAG_M | FLAG_X | FLAG_I;
    c->waiting = 0;
    c->stopped = 0;
    c->nmi_pending = 0;
    c->irq_line = 0;
    c->cycles = 0;
    c->pc = rd16(0xFFFC, false);
}

/* ------------------------------------------------------------------ */
/* Bucle de ejecución                                                  */
/* ------------------------------------------------------------------ */

/* Grupo principal (ORA, AND, EOR, ADC, STA, LDA, CMP, SBC) con 15 modos. */
static void group1(uint8_t op)
{
    uint8_t mode = op & 0x1F;
    uint8_t kind = op >> 5;
    uint16_t v = 0;

    if (mode == 0x09) {
        v = imm_m();
    } else {
        switch (mode) {
        case 0x01: am_idpx();  break;
        case 0x03: am_sr();    break;
        case 0x05: am_dp();    break;
        case 0x07: am_ildp();  break;
        case 0x0D: am_abs();   break;
        case 0x0F: am_long();  break;
        case 0x11: am_idpy();  break;
        case 0x12: am_idp();   break;
        case 0x13: am_isry();  break;
        case 0x15: am_dpx();   break;
        case 0x17: am_ildpy(); break;
        case 0x19: am_absy();  break;
        case 0x1D: am_absx();  break;
        default:   am_longx(); break; /* 0x1F */
        }
        if (kind == 4) { /* STA */
            write_m(c->a);
            return;
        }
        v = read_m();
    }

    switch (kind) {
    case 0: op_ora(v); break;
    case 1: op_and(v); break;
    case 2: op_eor(v); break;
    case 3: op_adc(v); break;
    case 5: op_lda(v); break;
    case 6: op_cmp(v); break;
    case 7: op_sbc(v); break;
    }
}

uint32_t cpu_step(cpu_t *cpu)
{
    uint32_t start;
    uint8_t op, lo;

    c = cpu;
    start = c->cycles;

    if (c->stopped) {
        c->cycles++;
        return 1;
    }

    if (c->nmi_pending) {
        c->nmi_pending = 0;
        c->waiting = 0;
        interrupt(VEC_NMI);
        return c->cycles - start;
    }
    if (c->irq_line) {
        c->waiting = 0; /* WAI se despierta aunque I esté activo */
        if (!(c->p & FLAG_I)) {
            interrupt(VEC_IRQ);
            return c->cycles - start;
        }
    }
    if (c->waiting) {
        c->cycles++;
        return 1;
    }

    op = fetch8();
    lo = op & 0x0F;

    if (((lo & 1) && lo != 0x0B && op != 0x89) || (op & 0x1F) == 0x12) {
        group1(op);
        return c->cycles - start;
    }

    switch (op) {
    case 0x0B: case 0x2B: case 0xAB: case 0xF4: case 0xD4:
    case 0x62: case 0x22: case 0x6B: case 0xFC:
        stack_linear = true;
        break;
    }

    switch (op) {
    /* --- Interrupciones software y control --- */
    case 0x00: fetch8(); interrupt(VEC_BRK); break;               /* BRK */
    case 0x02: fetch8(); interrupt(VEC_COP); break;               /* COP */
    case 0x42: fetch8(); break;                                   /* WDM */
    case 0xEA: io(); break;                                       /* NOP */
    case 0xCB: io(); io(); c->waiting = 1; break;                 /* WAI */
    case 0xDB: io(); io(); c->stopped = 1; break;                 /* STP */

    /* --- Saltos y subrutinas --- */
    case 0x4C: c->pc = fetch16(); break;                          /* JMP abs */
    case 0x5C: {                                                  /* JML long */
        uint32_t t = fetch24();
        c->pc = (uint16_t)t;
        c->pb = (uint8_t)(t >> 16);
        break;
    }
    case 0x6C: c->pc = rd16(fetch16(), true); break;              /* JMP (abs) */
    case 0x7C: {                                                  /* JMP (abs,X) */
        uint16_t p = (uint16_t)(fetch16() + c->x);
        io();
        c->pc = rd16(((uint32_t)c->pb << 16) | p, true);
        break;
    }
    case 0xDC: {                                                  /* JML [abs] */
        uint16_t p = fetch16();
        c->pc = rd16(p, true);
        c->pb = rd8((uint16_t)(p + 2));
        break;
    }
    case 0x20: {                                                  /* JSR abs */
        uint16_t t = fetch16();
        io();
        push16((uint16_t)(c->pc - 1));
        c->pc = t;
        break;
    }
    case 0xFC: {                                                  /* JSR (abs,X) */
        uint16_t p = fetch16();
        push16((uint16_t)(c->pc - 1));
        io();
        c->pc = rd16(((uint32_t)c->pb << 16) | (uint16_t)(p + c->x), true);
        break;
    }
    case 0x22: {                                                  /* JSL long */
        uint32_t t = fetch24();
        push8(c->pb);
        io();
        push16((uint16_t)(c->pc - 1));
        c->pc = (uint16_t)t;
        c->pb = (uint8_t)(t >> 16);
        break;
    }
    case 0x60: io(); io(); c->pc = (uint16_t)(pull16() + 1); io(); break; /* RTS */
    case 0x6B: io(); io(); c->pc = (uint16_t)(pull16() + 1); c->pb = pull8(); break; /* RTL */
    case 0x40:                                                    /* RTI */
        io(); io();
        set_p(pull8());
        c->pc = pull16();
        if (!c->e)
            c->pb = pull8();
        break;

    /* --- Saltos condicionales --- */
    case 0x10: branch(!(c->p & FLAG_N)); break;                   /* BPL */
    case 0x30: branch((c->p & FLAG_N) != 0); break;               /* BMI */
    case 0x50: branch(!(c->p & FLAG_V)); break;                   /* BVC */
    case 0x70: branch((c->p & FLAG_V) != 0); break;               /* BVS */
    case 0x90: branch(!(c->p & FLAG_C)); break;                   /* BCC */
    case 0xB0: branch((c->p & FLAG_C) != 0); break;               /* BCS */
    case 0xD0: branch(!(c->p & FLAG_Z)); break;                   /* BNE */
    case 0xF0: branch((c->p & FLAG_Z) != 0); break;               /* BEQ */
    case 0x80: branch(true); break;                               /* BRA */
    case 0x82: {                                                  /* BRL */
        uint16_t off = fetch16();
        io();
        c->pc = (uint16_t)(c->pc + off);
        break;
    }

    /* --- Flags --- */
    case 0x18: io(); c->p &= (uint8_t)~FLAG_C; break;             /* CLC */
    case 0x38: io(); c->p |= FLAG_C; break;                       /* SEC */
    case 0x58: io(); c->p &= (uint8_t)~FLAG_I; break;             /* CLI */
    case 0x78: io(); c->p |= FLAG_I; break;                       /* SEI */
    case 0xB8: io(); c->p &= (uint8_t)~FLAG_V; break;             /* CLV */
    case 0xD8: io(); c->p &= (uint8_t)~FLAG_D; break;             /* CLD */
    case 0xF8: io(); c->p |= FLAG_D; break;                       /* SED */
    case 0xC2: { uint8_t v = fetch8(); io(); set_p(c->p & (uint8_t)~v); break; } /* REP */
    case 0xE2: { uint8_t v = fetch8(); io(); set_p(c->p | v); break; }           /* SEP */
    case 0xFB: {                                                  /* XCE */
        uint8_t carry = c->p & FLAG_C;
        io();
        set_flag(FLAG_C, c->e != 0);
        c->e = carry ? 1 : 0;
        if (c->e) {
            c->s = 0x0100 | (c->s & 0xFF);
            set_p(c->p);
        }
        break;
    }

    /* --- Pila --- */
    case 0x48: io(); if (MFLAG) push8((uint8_t)c->a); else push16(c->a); break; /* PHA */
    case 0xDA: io(); if (XFLAG) push8((uint8_t)c->x); else push16(c->x); break; /* PHX */
    case 0x5A: io(); if (XFLAG) push8((uint8_t)c->y); else push16(c->y); break; /* PHY */
    case 0x68: io(); io(); set_a(MFLAG ? pull8() : pull16()); break;            /* PLA */
    case 0xFA: io(); io(); set_x_reg(&c->x, XFLAG ? pull8() : pull16()); break; /* PLX */
    case 0x7A: io(); io(); set_x_reg(&c->y, XFLAG ? pull8() : pull16()); break; /* PLY */
    case 0x08: io(); push8(c->p); break;                          /* PHP */
    case 0x28: io(); io(); set_p(pull8()); break;                 /* PLP */
    case 0x8B: io(); push8(c->db); break;                         /* PHB */
    case 0xAB: io(); io(); c->db = pull8(); set_nz8(c->db); break; /* PLB */
    case 0x0B: io(); push16(c->d); break;                         /* PHD */
    case 0x2B: io(); io(); c->d = pull16(); set_nz16(c->d); break; /* PLD */
    case 0x4B: io(); push8(c->pb); break;                         /* PHK */
    case 0xF4: push16(fetch16()); break;                          /* PEA */
    case 0xD4: { uint16_t p = dp_addr(false, 0); push16(rd16(p, true)); break; } /* PEI */
    case 0x62: {                                                  /* PER */
        uint16_t off = fetch16();
        io();
        push16((uint16_t)(c->pc + off));
        break;
    }

    /* --- Transferencias --- */
    case 0xAA: io(); set_x_reg(&c->x, c->a); break;               /* TAX */
    case 0xA8: io(); set_x_reg(&c->y, c->a); break;               /* TAY */
    case 0xBA: io(); set_x_reg(&c->x, c->s); break;               /* TSX */
    case 0x9B: io(); set_x_reg(&c->x, c->y); break;               /* TXY */
    case 0xBB: io(); set_x_reg(&c->y, c->x); break;               /* TYX */
    case 0x8A: io(); set_a(c->x); break;                          /* TXA */
    case 0x98: io(); set_a(c->y); break;                          /* TYA */
    case 0x9A:                                                    /* TXS */
        io();
        c->s = c->e ? (0x0100 | (c->x & 0xFF)) : c->x;
        break;
    case 0x1B:                                                    /* TCS */
        io();
        c->s = c->e ? (0x0100 | (c->a & 0xFF)) : c->a;
        break;
    case 0x3B: io(); c->a = c->s; set_nz16(c->a); break;          /* TSC */
    case 0x5B: io(); c->d = c->a; set_nz16(c->d); break;          /* TCD */
    case 0x7B: io(); c->a = c->d; set_nz16(c->a); break;          /* TDC */
    case 0xEB:                                                    /* XBA */
        io(); io();
        c->a = (uint16_t)((c->a >> 8) | (c->a << 8));
        set_nz8((uint8_t)c->a);
        break;

    /* --- Incrementos de registros --- */
    case 0xE8: io(); set_x_reg(&c->x, c->x + 1); break;           /* INX */
    case 0xC8: io(); set_x_reg(&c->y, c->y + 1); break;           /* INY */
    case 0xCA: io(); set_x_reg(&c->x, c->x - 1); break;           /* DEX */
    case 0x88: io(); set_x_reg(&c->y, c->y - 1); break;           /* DEY */

    /* --- Lectura-modificación-escritura --- */
    case 0x0A: rmw_acc(RMW_ASL); break;
    case 0x4A: rmw_acc(RMW_LSR); break;
    case 0x2A: rmw_acc(RMW_ROL); break;
    case 0x6A: rmw_acc(RMW_ROR); break;
    case 0x1A: rmw_acc(RMW_INC); break;
    case 0x3A: rmw_acc(RMW_DEC); break;
    case 0x06: am_dp();   rmw_mem(RMW_ASL); break;
    case 0x0E: am_abs();  rmw_mem(RMW_ASL); break;
    case 0x16: am_dpx();  rmw_mem(RMW_ASL); break;
    case 0x1E: am_absx(); rmw_mem(RMW_ASL); break;
    case 0x46: am_dp();   rmw_mem(RMW_LSR); break;
    case 0x4E: am_abs();  rmw_mem(RMW_LSR); break;
    case 0x56: am_dpx();  rmw_mem(RMW_LSR); break;
    case 0x5E: am_absx(); rmw_mem(RMW_LSR); break;
    case 0x26: am_dp();   rmw_mem(RMW_ROL); break;
    case 0x2E: am_abs();  rmw_mem(RMW_ROL); break;
    case 0x36: am_dpx();  rmw_mem(RMW_ROL); break;
    case 0x3E: am_absx(); rmw_mem(RMW_ROL); break;
    case 0x66: am_dp();   rmw_mem(RMW_ROR); break;
    case 0x6E: am_abs();  rmw_mem(RMW_ROR); break;
    case 0x76: am_dpx();  rmw_mem(RMW_ROR); break;
    case 0x7E: am_absx(); rmw_mem(RMW_ROR); break;
    case 0xE6: am_dp();   rmw_mem(RMW_INC); break;
    case 0xEE: am_abs();  rmw_mem(RMW_INC); break;
    case 0xF6: am_dpx();  rmw_mem(RMW_INC); break;
    case 0xFE: am_absx(); rmw_mem(RMW_INC); break;
    case 0xC6: am_dp();   rmw_mem(RMW_DEC); break;
    case 0xCE: am_abs();  rmw_mem(RMW_DEC); break;
    case 0xD6: am_dpx();  rmw_mem(RMW_DEC); break;
    case 0xDE: am_absx(); rmw_mem(RMW_DEC); break;
    case 0x04: am_dp();   tsb_trb(true);  break;                  /* TSB */
    case 0x0C: am_abs();  tsb_trb(true);  break;
    case 0x14: am_dp();   tsb_trb(false); break;                  /* TRB */
    case 0x1C: am_abs();  tsb_trb(false); break;

    /* --- BIT --- */
    case 0x89: op_bit(imm_m(), true); break;
    case 0x24: am_dp();   op_bit(read_m(), false); break;
    case 0x2C: am_abs();  op_bit(read_m(), false); break;
    case 0x34: am_dpx();  op_bit(read_m(), false); break;
    case 0x3C: am_absx(); op_bit(read_m(), false); break;

    /* --- Carga/almacenamiento de índices y STZ --- */
    case 0xA2: set_x_reg(&c->x, imm_x()); break;                  /* LDX */
    case 0xA6: am_dp();   set_x_reg(&c->x, read_x()); break;
    case 0xAE: am_abs();  set_x_reg(&c->x, read_x()); break;
    case 0xB6: am_dpy();  set_x_reg(&c->x, read_x()); break;
    case 0xBE: am_absy(); set_x_reg(&c->x, read_x()); break;
    case 0xA0: set_x_reg(&c->y, imm_x()); break;                  /* LDY */
    case 0xA4: am_dp();   set_x_reg(&c->y, read_x()); break;
    case 0xAC: am_abs();  set_x_reg(&c->y, read_x()); break;
    case 0xB4: am_dpx();  set_x_reg(&c->y, read_x()); break;
    case 0xBC: am_absx(); set_x_reg(&c->y, read_x()); break;
    case 0x86: am_dp();   write_x(c->x); break;                   /* STX */
    case 0x8E: am_abs();  write_x(c->x); break;
    case 0x96: am_dpy();  write_x(c->x); break;
    case 0x84: am_dp();   write_x(c->y); break;                   /* STY */
    case 0x8C: am_abs();  write_x(c->y); break;
    case 0x94: am_dpx();  write_x(c->y); break;
    case 0x64: am_dp();   write_m(0); break;                      /* STZ */
    case 0x74: am_dpx();  write_m(0); break;
    case 0x9C: am_abs();  write_m(0); break;
    case 0x9E: am_absx(); write_m(0); break;

    /* --- Comparaciones de índices --- */
    case 0xE0: compare(c->x, imm_x(), XFLAG); break;              /* CPX */
    case 0xE4: am_dp();  compare(c->x, read_x(), XFLAG); break;
    case 0xEC: am_abs(); compare(c->x, read_x(), XFLAG); break;
    case 0xC0: compare(c->y, imm_x(), XFLAG); break;              /* CPY */
    case 0xC4: am_dp();  compare(c->y, read_x(), XFLAG); break;
    case 0xCC: am_abs(); compare(c->y, read_x(), XFLAG); break;

    /* --- Movimiento de bloques (un byte por paso) --- */
    case 0x54:                                                    /* MVN */
    case 0x44: {                                                  /* MVP */
        uint8_t dst = fetch8();
        uint8_t src = fetch8();
        uint8_t v;
        c->db = dst;
        v = rd8(((uint32_t)src << 16) | c->x);
        wr8(((uint32_t)dst << 16) | c->y, v);
        io(); io();
        if (op == 0x54) { c->x++; c->y++; } else { c->x--; c->y--; }
        if (XFLAG) { c->x &= 0xFF; c->y &= 0xFF; }
        if (c->a-- != 0)
            c->pc = (uint16_t)(c->pc - 3);
        break;
    }

    default:
        /* No debería ocurrir: todos los opcodes están cubiertos. */
        break;
    }

    if (stack_linear) {
        stack_linear = false;
        if (c->e)
            c->s = 0x0100 | (c->s & 0xFF);
    }

    return c->cycles - start;
}
