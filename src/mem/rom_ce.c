/* Carga de ROMs desde AppVars en la TI-84 Plus CE (código original). */
#include <fileioc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "rom_ce.h"

#define PIXEL_SHADOW      ((uint8_t *)0xD031F6)
#define PIXEL_SHADOW_SIZE 69090u
/* Hueco al final de la RAM del LCD: 0xD40000 + 2 * 76800 = 0xD65800;
 * la VRAM de la PPU ocupa 0xD52C00-0xD62BFF. */
#define LCD_SPARE         ((uint8_t *)0xD62C00)
#define LCD_SPARE_SIZE    11264u

#define IDX_HEADER 80
#define IDX_ENTRY  12

rom_info_t rom_info;

static mem_chunk_t chunks[256];
static uint8_t *wram_pages[MEM_WRAM_PAGES];
static uint8_t wram_kind[MEM_WRAM_PAGES];  /* 0 nada, 1 malloc, 2 pixelShadow, 3 LCD */
static uint8_t *sram;
static char sav_name[9];
static uint8_t guard_bss;

enum { W_NONE, W_HEAP, W_PS, W_LCD };

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool inside(const void *p, const uint8_t *start, uint32_t len)
{
    const uint8_t *b = (const uint8_t *)p;
    return b >= start && b < start + len;
}

static void copy_name(char *dst, const uint8_t *src)
{
    memcpy(dst, src, 8);
    dst[8] = '\0';
}

uint8_t rom_list(char names[][9], uint8_t max)
{
    void *pos = NULL;
    char *name;
    uint8_t n = 0;

    while (n < max && (name = ti_Detect(&pos, "SNCE")) != NULL) {
        strncpy(names[n], name, 8);
        names[n][8] = '\0';
        n++;
    }
    return n;
}

static void free_wram(void)
{
    uint8_t i;
    for (i = 0; i < MEM_WRAM_PAGES; i++) {
        if (wram_kind[i] == W_HEAP)
            free(wram_pages[i]);
        else if (wram_kind[i] != W_NONE)
            memset(wram_pages[i], 0, MEM_PAGE_SIZE);
        wram_pages[i] = NULL;
        wram_kind[i] = W_NONE;
    }
}

static bool alloc_wram(void)
{
    uint8_t i = 0, k;
    bool use_ps;
    void *probe = malloc(1);

    /* pixelShadow solo si ni las variables globales ni el heap viven ahí */
    use_ps = !inside(&guard_bss, PIXEL_SHADOW, PIXEL_SHADOW_SIZE) &&
             !inside(chunks, PIXEL_SHADOW, PIXEL_SHADOW_SIZE) &&
             !(probe && inside(probe, PIXEL_SHADOW, PIXEL_SHADOW_SIZE));
    free(probe);

    rom_info.wram_ps = rom_info.wram_lcd = rom_info.wram_heap = 0;
    if (use_ps) {
        for (k = 0; k < PIXEL_SHADOW_SIZE / MEM_PAGE_SIZE && i < MEM_WRAM_PAGES; k++, i++) {
            wram_pages[i] = PIXEL_SHADOW + (uint32_t)k * MEM_PAGE_SIZE;
            wram_kind[i] = W_PS;
            rom_info.wram_ps++;
        }
    }
    if (i < MEM_WRAM_PAGES && LCD_SPARE_SIZE >= MEM_PAGE_SIZE) {
        wram_pages[i] = LCD_SPARE;
        wram_kind[i] = W_LCD;
        rom_info.wram_lcd++;
        i++;
    }
    for (; i < MEM_WRAM_PAGES; i++) {
        uint8_t *p = malloc(MEM_PAGE_SIZE);
        if (!p) {
            free_wram();
            return false;
        }
        wram_pages[i] = p;
        wram_kind[i] = W_HEAP;
        rom_info.wram_heap++;
    }
    /* La WRAM de la consola arranca con contenido indefinido; a cero es lo
     * más seguro para juegos que no la inicializan. */
    for (i = 0; i < MEM_WRAM_PAGES; i++)
        memset(wram_pages[i], 0, MEM_PAGE_SIZE);
    return true;
}

static void load_sram(void)
{
    uint8_t h;
    memset(sram, 0, rom_info.sram_size);
    if (!sav_name[0])
        return;
    h = ti_Open(sav_name, "r");
    if (h) {
        uint16_t n = ti_GetSize(h);
        if (n > rom_info.sram_size)
            n = (uint16_t)rom_info.sram_size;
        memcpy(sram, ti_GetDataPtr(h), n);
        ti_Close(h);
    }
}

static void save_sram(void)
{
    uint8_t h;
    if (!sram || !sav_name[0])
        return;
    h = ti_Open(sav_name, "w");
    if (!h)
        return;
    ti_Write(sram, rom_info.sram_size, 1, h);
    ti_SetArchiveStatus(true, h);
    ti_Close(h);
}

int rom_load(const char *idx_name)
{
    const uint8_t *idx;
    mem_config_t cfg;
    uint8_t h, i;
    uint16_t c;

    memset(&rom_info, 0, sizeof rom_info);
    strncpy(rom_info.idx_name, idx_name, 8);

    h = ti_Open(idx_name, "r");
    if (!h)
        return ROM_ERR_OPEN;
    idx = ti_GetDataPtr(h);
    if (ti_GetSize(h) < IDX_HEADER || memcmp(idx, "SNCE", 4) != 0 || idx[4] != 1 ||
        idx[5] > MAP_EXHIROM || idx[16] != MEM_CHUNK_BITS ||
        ti_GetSize(h) < IDX_HEADER + (uint16_t)idx[17] * IDX_ENTRY) {
        ti_Close(h);
        return ROM_ERR_FORMAT;
    }
    ti_Close(h); /* El puntero a Flash sigue siendo válido */

    rom_info.map = idx[5];
    rom_info.rom_size = rd32(idx + 8);
    rom_info.sram_size = rd32(idx + 12);
    rom_info.nchunks = idx[17];
    rom_info.fastrom = idx[19] & 1;
    memcpy(rom_info.title, idx + 24, 21);
    rom_info.title[21] = '\0';
    for (i = 21; i > 0 && rom_info.title[i - 1] == ' '; i--)
        rom_info.title[i - 1] = '\0';
    copy_name(sav_name, idx + 68);

    for (c = 0; c < rom_info.nchunks; c++) {
        const uint8_t *e = idx + IDX_HEADER + c * IDX_ENTRY;
        char name[9];
        copy_name(name, e);
        chunks[c].fill = e[9];
        chunks[c].len = rd16(e + 10);
        chunks[c].data = NULL;
        if (name[0] && chunks[c].len) {
            h = ti_Open(name, "r");
            if (!h) {
                strcpy(rom_info.missing, name);
                return ROM_ERR_CHUNK;
            }
            if (ti_GetSize(h) < chunks[c].len)
                chunks[c].len = ti_GetSize(h);
            chunks[c].data = ti_GetDataPtr(h);
            ti_Close(h);
            rom_info.stored++;
        }
    }

    if (rom_info.sram_size) {
        sram = malloc(rom_info.sram_size);
        if (!sram)
            return ROM_ERR_RAM;
        load_sram();
    }
    if (!alloc_wram()) {
        free(sram);
        sram = NULL;
        return ROM_ERR_RAM;
    }

    memset(&cfg, 0, sizeof cfg);
    cfg.map = rom_info.map;
    cfg.rom_size = rom_info.rom_size;
    cfg.nchunks = rom_info.nchunks;
    cfg.chunks = chunks;
    cfg.sram = sram;
    cfg.sram_size = rom_info.sram_size;
    for (i = 0; i < MEM_WRAM_PAGES; i++)
        cfg.wram[i] = wram_pages[i];
    if (mem_init(&cfg) != 0) {
        rom_unload();
        return ROM_ERR_FORMAT;
    }
    return ROM_OK;
}

void rom_unload(void)
{
    /* Primero liberar la WRAM: guardar en Flash necesita RAM libre */
    free_wram();
    save_sram();
    free(sram);
    sram = NULL;
}

const char *rom_error(int err)
{
    switch (err) {
    case ROM_OK:         return "OK";
    case ROM_ERR_OPEN:   return "No se puede abrir el indice";
    case ROM_ERR_FORMAT: return "Indice no valido (formato v1)";
    case ROM_ERR_CHUNK:  return "Falta una AppVar de la ROM";
    case ROM_ERR_RAM:    return "No hay RAM libre suficiente";
    default:             return "Error desconocido";
    }
}
