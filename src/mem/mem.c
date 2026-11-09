/*
 * Mapa de memoria de la SNES: LoROM, HiROM y ExHiROM, WRAM, SRAM y
 * registros de E/S del bus A. Implementación original.
 */
#include <stddef.h>
#include <string.h>

#include "mem.h"
#include "../bus.h"

/* Tipos de página (camino lento) */
enum { K_OPEN, K_WRAM, K_ROM, K_SRAM, K_IO };

const uint8_t *mem_rd[MEM_PAGES];
uint8_t *mem_wr[MEM_PAGES];
mem_hooks_t mem_hooks;

uint8_t mem_nmitimen;
uint8_t mem_hdmaen;
uint8_t mem_memsel;
uint8_t mem_dma[8][16];

static mem_config_t cfg;

/* Registros $42xx */
static uint8_t  wrio = 0xFF;
static uint8_t  wrmpya = 0xFF;
static uint16_t wrdiv = 0xFFFF;
static uint16_t rddiv, rdmpy;
static uint16_t htime = 0x1FF, vtime = 0x1FF;
static uint8_t  rdnmi, timeup, hvbjoy;
static uint8_t  nmi_request;
static uint32_t dma_cycles;

/* Puerto de WRAM $2180-$2183 */
static uint32_t wmadd;

/* APU falsa: eco de lo que escribe la CPU */
static uint8_t apu_port[4];

/* ------------------------------------------------------------------ */
/* Decodificación                                                      */
/* ------------------------------------------------------------------ */

/* Espejado de ROMs cuyo tamaño no es potencia de 2 (p. ej. 3 MB = 2 + 1 + 1). */
static uint32_t rom_mirror(uint32_t off)
{
    uint32_t size = cfg.rom_size;
    uint32_t base = 0;
    uint32_t mask = 0x800000;

    if (size == 0)
        return 0;
    while (off >= size) {
        while (!(off & mask))
            mask >>= 1;
        off -= mask;
        if (size > mask) {
            size -= mask;
            base += mask;
        }
        mask >>= 1;
    }
    return base + off;
}

/* Clasifica la página de 8 KB que empieza en banco:sub*$2000.
 * Para WRAM/ROM/SRAM deja en *off el desplazamiento del inicio de página. */
static uint8_t classify(uint8_t bank, uint8_t sub, uint32_t *off)
{
    uint32_t a = (uint32_t)sub << MEM_PAGE_BITS;
    uint32_t o;

    if ((bank & 0xFE) == 0x7E) {
        *off = ((uint32_t)(bank & 1) << 16) | a;
        return K_WRAM;
    }
    if (!(bank & 0x40)) {           /* $00-$3F, $80-$BF */
        if (sub == 0) {
            *off = 0;
            return K_WRAM;
        }
        if (sub == 1 || sub == 2)
            return K_IO;
        if (sub == 3) {
            if (cfg.map != MAP_LOROM && (bank & 0x20) && cfg.sram_size) {
                *off = (uint32_t)(bank & 0x1F) << MEM_PAGE_BITS;
                return K_SRAM;
            }
            return K_OPEN;
        }
    }

    if (cfg.map == MAP_LOROM) {
        if (a < 0x8000 && cfg.sram_size &&
            ((bank >= 0x70 && bank <= 0x7D) || bank >= 0xF0)) {
            *off = ((uint32_t)(bank & 0x0F) << 15) | a;
            return K_SRAM;
        }
        o = ((uint32_t)(bank & 0x7F) << 15) | (a & 0x7FFF);
    } else {
        o = ((uint32_t)(bank & 0x3F) << 16) | a;
        if (cfg.map == MAP_EXHIROM && !(bank & 0x80))
            o += 0x400000;
    }
    *off = rom_mirror(o);
    return K_ROM;
}

int32_t mem_rom_offset(uint32_t addr)
{
    uint32_t off;
    addr &= 0xFFFFFF;
    if (classify((uint8_t)(addr >> 16), (uint8_t)((addr >> MEM_PAGE_BITS) & 7), &off) != K_ROM)
        return -1;
    return (int32_t)(off + (addr & MEM_PAGE_MASK));
}

static uint8_t rom_byte(uint32_t off)
{
    uint32_t ci = off >> MEM_CHUNK_BITS;
    uint16_t in = (uint16_t)(off & (MEM_CHUNK_SIZE - 1));
    const mem_chunk_t *ch;

    if (ci >= cfg.nchunks)
        return 0xFF;
    ch = &cfg.chunks[ci];
    if (ch->data && in < ch->len)
        return ch->data[in];
    return ch->fill;
}

int mem_init(const mem_config_t *c)
{
    uint16_t page;

    if (c->map > MAP_EXHIROM || (c->nchunks && !c->chunks))
        return -1;
    cfg = *c;
    if (!cfg.sram)
        cfg.sram_size = 0;

    for (page = 0; page < MEM_PAGES; page++) {
        uint32_t off = 0;
        const uint8_t *rd = NULL;
        uint8_t *wr = NULL;

        switch (classify((uint8_t)(page >> 3), (uint8_t)(page & 7), &off)) {
        case K_WRAM:
            wr = cfg.wram[off >> MEM_PAGE_BITS];
            rd = wr;
            break;
        case K_ROM: {
            uint32_t ci = off >> MEM_CHUNK_BITS;
            uint16_t in = (uint16_t)(off & (MEM_CHUNK_SIZE - 1));
            if (ci < cfg.nchunks && cfg.chunks[ci].data &&
                (uint32_t)in + MEM_PAGE_SIZE <= cfg.chunks[ci].len)
                rd = cfg.chunks[ci].data + in;
            break;
        }
        case K_SRAM:
            if (cfg.sram_size >= MEM_PAGE_SIZE) {
                wr = cfg.sram + off % cfg.sram_size;
                rd = wr;
            }
            break;
        default:
            break;
        }
        mem_rd[page] = rd;
        mem_wr[page] = wr;
    }

    mem_reset_io();
    return 0;
}

void mem_reset_io(void)
{
    mem_nmitimen = 0;
    mem_hdmaen = 0;
    mem_memsel = 0;
    memset(mem_dma, 0xFF, sizeof mem_dma);
    wrio = 0xFF;
    wrmpya = 0xFF;
    wrdiv = 0xFFFF;
    rddiv = rdmpy = 0;
    htime = vtime = 0x1FF;
    rdnmi = timeup = hvbjoy = 0;
    nmi_request = 0;
    dma_cycles = 0;
    wmadd = 0;
    apu_port[0] = 0xAA;
    apu_port[1] = 0xBB;
    apu_port[2] = apu_port[3] = 0;
}

/* ------------------------------------------------------------------ */
/* WRAM                                                                */
/* ------------------------------------------------------------------ */

static uint8_t *wram_ptr(uint32_t off)
{
    uint8_t *p = cfg.wram[(off >> MEM_PAGE_BITS) & (MEM_WRAM_PAGES - 1)];
    return p ? p + (off & MEM_PAGE_MASK) : NULL;
}

/* ------------------------------------------------------------------ */
/* Bus B ($21xx)                                                       */
/* ------------------------------------------------------------------ */

static uint8_t bbus_read(uint8_t reg, uint8_t open)
{
    if (reg < 0x40)
        return mem_hooks.ppu_read ? mem_hooks.ppu_read(reg) : open;
    if (reg < 0x80) {
        if (mem_hooks.apu_read)
            return mem_hooks.apu_read(reg & 3);
        return apu_port[reg & 3];
    }
    if (reg == 0x80) {
        uint8_t *p = wram_ptr(wmadd);
        wmadd = (wmadd + 1) & (MEM_WRAM_SIZE - 1);
        return p ? *p : open;
    }
    return open;
}

static void bbus_write(uint8_t reg, uint8_t v)
{
    if (reg < 0x40) {
        if (mem_hooks.ppu_write)
            mem_hooks.ppu_write(reg, v);
    } else if (reg < 0x80) {
        if (mem_hooks.apu_write)
            mem_hooks.apu_write(reg & 3, v);
        else
            apu_port[reg & 3] = v;
    } else if (reg == 0x80) {
        uint8_t *p = wram_ptr(wmadd);
        if (p)
            *p = v;
        wmadd = (wmadd + 1) & (MEM_WRAM_SIZE - 1);
    } else if (reg == 0x81) {
        wmadd = (wmadd & 0x1FF00) | v;
    } else if (reg == 0x82) {
        wmadd = (wmadd & 0x100FF) | ((uint32_t)v << 8);
    } else if (reg == 0x83) {
        wmadd = (wmadd & 0x0FFFF) | ((uint32_t)(v & 1) << 16);
    }
}

/* ------------------------------------------------------------------ */
/* DMA de propósito general ($420B)                                    */
/* ------------------------------------------------------------------ */

/* Desplazamiento en el bus B de cada byte según el modo de transferencia */
static const uint8_t dma_pattern[8][4] = {
    {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
};

static void run_dma(uint8_t channels)
{
    uint8_t ch;

    for (ch = 0; ch < 8; ch++) {
        uint8_t *r = mem_dma[ch];
        uint8_t dmap, bbad, bank;
        uint16_t a1t;
        uint32_t count;
        uint32_t i;
        const uint8_t *pat;

        if (!(channels & (1u << ch)))
            continue;
        dmap = r[0];
        bbad = r[1];
        a1t = (uint16_t)(r[2] | (r[3] << 8));
        bank = r[4];
        count = (uint32_t)(r[5] | (r[6] << 8));
        if (count == 0)
            count = 0x10000;
        pat = dma_pattern[dmap & 7];

        for (i = 0; i < count; i++) {
            uint32_t a = ((uint32_t)bank << 16) | a1t;
            uint8_t b = (uint8_t)(bbad + pat[i & 3]);
            if (dmap & 0x80)
                bus_write(a, bbus_read(b, 0));
            else
                bbus_write(b, bus_read(a));
            if (!(dmap & 0x08))
                a1t = (uint16_t)(a1t + ((dmap & 0x10) ? -1 : 1));
        }
        dma_cycles += 8 + count * 8;

        r[2] = (uint8_t)a1t;
        r[3] = (uint8_t)(a1t >> 8);
        r[5] = r[6] = 0;
    }
}

uint32_t mem_take_dma_cycles(void)
{
    uint32_t c = dma_cycles;
    dma_cycles = 0;
    return c;
}

/* ------------------------------------------------------------------ */
/* Registros de E/S ($2000-$5FFF en los bancos de sistema)             */
/* ------------------------------------------------------------------ */

static uint8_t io_read(uint16_t a, uint8_t open)
{
    uint8_t v;

    if ((a & 0xFF00) == 0x2100)
        return bbus_read((uint8_t)a, open);
    if (a == 0x4016 || a == 0x4017 || (a >= 0x4218 && a <= 0x421F))
        return mem_hooks.joy_read ? mem_hooks.joy_read(a) : (a < 0x4200 ? (open & 0xFC) : 0);
    switch (a) {
    case 0x4210:            /* RDNMI: bit 7 se borra al leer; versión 2 */
        v = (uint8_t)(rdnmi | 0x02 | (open & 0x70));
        rdnmi = 0;
        return v;
    case 0x4211:            /* TIMEUP */
        v = (uint8_t)(timeup | (open & 0x7F));
        timeup = 0;
        return v;
    case 0x4212: return (uint8_t)(hvbjoy | (open & 0x3E));
    case 0x4213: return wrio;
    case 0x4214: return (uint8_t)rddiv;
    case 0x4215: return (uint8_t)(rddiv >> 8);
    case 0x4216: return (uint8_t)rdmpy;
    case 0x4217: return (uint8_t)(rdmpy >> 8);
    }
    if ((a & 0xFF80) == 0x4300 && (a & 0x0F) != 0x0C && (a & 0x0F) != 0x0D &&
        (a & 0x0F) != 0x0E)
        return mem_dma[(a >> 4) & 7][a & 0x0F];
    return open;
}

static void io_write(uint16_t a, uint8_t v)
{
    if ((a & 0xFF00) == 0x2100) {
        bbus_write((uint8_t)a, v);
        return;
    }
    if (a == 0x4016) {
        if (mem_hooks.joy_write)
            mem_hooks.joy_write(a, v);
        return;
    }
    switch (a) {
    case 0x4200:
        /* Activar la NMI en pleno VBlank con RDNMI a 1 la lanza al momento */
        if (!(mem_nmitimen & 0x80) && (v & 0x80) && (rdnmi & 0x80))
            nmi_request = 1;
        mem_nmitimen = v;
        if (!(v & 0x30))
            timeup = 0;
        return;
    case 0x4201: wrio = v; return;
    case 0x4202: wrmpya = v; return;
    case 0x4203:
        rdmpy = (uint16_t)(wrmpya * v);
        rddiv = v;
        return;
    case 0x4204: wrdiv = (uint16_t)((wrdiv & 0xFF00) | v); return;
    case 0x4205: wrdiv = (uint16_t)((wrdiv & 0x00FF) | (v << 8)); return;
    case 0x4206:
        if (v == 0) {
            rddiv = 0xFFFF;
            rdmpy = wrdiv;
        } else {
            rddiv = (uint16_t)(wrdiv / v);
            rdmpy = (uint16_t)(wrdiv % v);
        }
        return;
    case 0x4207: htime = (uint16_t)((htime & 0x100) | v); return;
    case 0x4208: htime = (uint16_t)((htime & 0x0FF) | ((v & 1) << 8)); return;
    case 0x4209: vtime = (uint16_t)((vtime & 0x100) | v); return;
    case 0x420A: vtime = (uint16_t)((vtime & 0x0FF) | ((v & 1) << 8)); return;
    case 0x420B: run_dma(v); return;
    case 0x420C: mem_hdmaen = v; return;
    case 0x420D: mem_memsel = v & 1; return;
    }
    if ((a & 0xFF80) == 0x4300)
        mem_dma[(a >> 4) & 7][a & 0x0F] = v;
}

/* ------------------------------------------------------------------ */
/* Bus A (lo que ve la CPU)                                            */
/* ------------------------------------------------------------------ */

static uint8_t read_slow(uint32_t addr)
{
    uint32_t off;
    /* Bus abierto aproximado: el último byte leído suele ser el byte alto
     * de la dirección (operando de la instrucción). */
    uint8_t open = (uint8_t)(addr >> 8);

    switch (classify((uint8_t)(addr >> 16), (uint8_t)((addr >> MEM_PAGE_BITS) & 7), &off)) {
    case K_IO:
        return io_read((uint16_t)addr, open);
    case K_ROM:
        return rom_byte(off + (addr & MEM_PAGE_MASK));
    case K_SRAM:
        return cfg.sram[(off + (addr & MEM_PAGE_MASK)) % cfg.sram_size];
    default:
        return open;
    }
}

static void write_slow(uint32_t addr, uint8_t v)
{
    uint32_t off;

    switch (classify((uint8_t)(addr >> 16), (uint8_t)((addr >> MEM_PAGE_BITS) & 7), &off)) {
    case K_IO:
        io_write((uint16_t)addr, v);
        break;
    case K_SRAM:
        cfg.sram[(off + (addr & MEM_PAGE_MASK)) % cfg.sram_size] = v;
        break;
    default:
        break;      /* ROM y bus abierto: se ignora */
    }
}

uint8_t bus_read(uint32_t addr)
{
    const uint8_t *p = mem_rd[(addr >> MEM_PAGE_BITS) & (MEM_PAGES - 1)];
    if (p)
        return p[addr & MEM_PAGE_MASK];
    return read_slow(addr & 0xFFFFFF);
}

void bus_write(uint32_t addr, uint8_t value)
{
    uint8_t *p = mem_wr[(addr >> MEM_PAGE_BITS) & (MEM_PAGES - 1)];
    if (p)
        p[addr & MEM_PAGE_MASK] = value;
    else
        write_slow(addr & 0xFFFFFF, value);
}

/* ------------------------------------------------------------------ */
/* Temporización                                                       */
/* ------------------------------------------------------------------ */

uint8_t mem_vblank_start(void)
{
    rdnmi |= 0x80;
    hvbjoy |= 0x80;
    if ((mem_nmitimen & 0x01) && mem_hooks.joy_auto_read)
        mem_hooks.joy_auto_read();
    return (uint8_t)((mem_nmitimen & 0x80) ? 1 : 0);
}

void mem_vblank_end(void)
{
    rdnmi &= 0x7F;
    hvbjoy &= 0x7F;
    nmi_request = 0;
}

uint8_t mem_line_irq(uint16_t v)
{
    uint8_t mode = (uint8_t)((mem_nmitimen >> 4) & 3);
    if (mode == 0)
        return 0;
    if ((mode & 2) && v != vtime)  /* V-IRQ (o V+H): solo en la línea VTIME */
        return 0;
    if (mode == 1 && htime > 339)  /* H-IRQ con HTIME fuera de rango */
        return 0;
    timeup = 0x80;
    return 1;
}

uint8_t mem_irq_pending(void)
{
    return (uint8_t)(timeup >> 7);
}

uint8_t mem_take_nmi_request(void)
{
    uint8_t r = nmi_request;
    nmi_request = 0;
    return r;
}
