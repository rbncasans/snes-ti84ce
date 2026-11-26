/*
 * Gráficos, entrada y utilidades comunes.
 * Los wawas y Petrovich salen de imágenes (src/sprites.c, generado por
 * tools/make_sprites.py); el resto se dibuja con primitivas.
 */
#include "wawas.h"
#include "sprites.h"

#include <keypadc.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

gfx_sprite_t *spr_wawa16[W_NUM];
gfx_sprite_t *spr_wawa24[W_NUM];
gfx_sprite_t *spr_dead16;
gfx_sprite_t *spr_dead24;
gfx_sprite_t *spr_dog_r, *spr_dog_l;
gfx_sprite_t *spr_petro;
gfx_sprite_t *spr_player;
gfx_sprite_t *spr_spike;
gfx_sprite_t *spr_cactus;
gfx_sprite_t *spr_wawa_big;

input_t in;

/* ---------------- Paleta ---------------- */
static const uint8_t pal_rgb[C_NUM][3] = {
    {255,   0, 255}, /* C_TRANS (magenta, transparente) */
    {  0,   0,   0}, /* C_BLACK */
    {255, 255, 255}, /* C_WHITE */
    {150,  95,  50}, /* C_BROWN */
    { 90, 190,  60}, /* C_EYE */
    {230,  40,  40}, /* C_RED */
    {130,  10,  10}, /* C_DRED */
    { 50, 100, 230}, /* C_BLUE */
    {140, 140, 140}, /* C_GREY */
    { 60,  60,  60}, /* C_DGREY */
    {205, 205, 205}, /* C_LGREY */
    {250, 220,  40}, /* C_YELLOW */
    {120, 190, 240}, /* C_SKY */
    {245, 140,  30}, /* C_ORANGE */
    {150,  70, 200}, /* C_PURPLE */
    { 15,  56,  15}, /* C_GB0 (más oscuro) */
    { 48,  98,  48}, /* C_GB1 */
    {139, 172,  15}, /* C_GB2 */
    {155, 188,  15}, /* C_GB3 (más claro) */
    { 30, 110,  40}, /* C_DGREEN */
    { 10,  10,  40}, /* C_SPACE */
    {250, 150, 180}, /* C_PINK */
    {205, 165, 110}, /* C_TAN */
    {110,  80,  60}, /* C_FLOOR */
    {170, 215, 250}, /* C_LBLUE */
    { 60, 170,  70}, /* C_CACTUS */
    { 80,  50,  25}, /* C_DBROWN */
    { 60, 200,  80}, /* C_GREEN */
    { 25,  45, 120}, /* C_DBLUE */
};

/* Sprites de las imágenes (generados por tools/make_sprites.py) */
static const uint8_t *const wawa16_data[W_NUM] = {
    spr_wawa16_normal_data, spr_wawa16_red_data, spr_wawa16_blue_data, spr_wawa16_green_data,
    spr_wawa16_yellow_data, spr_wawa16_purple_data, spr_wawa16_gb_data,
};
static const uint8_t *const wawa24_data[W_NUM] = {
    spr_wawa24_normal_data, spr_wawa24_red_data, spr_wawa24_blue_data, spr_wawa24_green_data,
    spr_wawa24_yellow_data, spr_wawa24_purple_data, spr_wawa24_gb_data,
};

/* ---------------- Sprites dibujados con primitivas ---------------- */

static gfx_sprite_t *grab(int w, int h) {
    gfx_sprite_t *s = gfx_MallocSprite(w, h);
    gfx_GetSprite(s, 0, 0);
    return s;
}

static void clear_area(int w, int h) {
    gfx_SetColor(C_TRANS);
    gfx_FillRectangle(0, 0, w, h);
}

static void make_sprites(void) {
    int i;

    for (i = 0; i < W_NUM; i++) {
        spr_wawa16[i] = (gfx_sprite_t *)wawa16_data[i];
        spr_wawa24[i] = (gfx_sprite_t *)wawa24_data[i];
    }
    spr_wawa_big = (gfx_sprite_t *)spr_wawa64_data;
    spr_dead16 = (gfx_sprite_t *)spr_dead16_data;
    spr_dead24 = (gfx_sprite_t *)spr_dead24_data;
    spr_petro = (gfx_sprite_t *)spr_petro_data;

    /* perro (mirando a la derecha) */
    clear_area(24, 16);
    gfx_SetColor(C_DBROWN);
    gfx_Line(1, 4, 4, 8);                       /* cola */
    gfx_FillRectangle(5, 12, 3, 4);             /* patas */
    gfx_FillRectangle(14, 12, 3, 4);
    gfx_SetColor(C_TAN);
    gfx_FillRectangle(3, 6, 16, 7);             /* cuerpo */
    gfx_FillCircle(18, 6, 5);                   /* cabeza */
    gfx_FillRectangle(19, 7, 5, 3);             /* hocico */
    gfx_SetColor(C_DBROWN);
    gfx_FillRectangle(14, 0, 3, 5);             /* oreja */
    gfx_SetColor(C_BLACK);
    gfx_SetPixel(20, 5);                        /* ojo */
    gfx_SetPixel(23, 7);                        /* nariz */
    spr_dog_r = grab(24, 16);
    spr_dog_l = gfx_MallocSprite(24, 16);
    gfx_FlipSpriteY(spr_dog_r, spr_dog_l);

    /* jugador: cuadrado azul con borde negro (14x14 dentro de 14x18) */
    clear_area(14, 18);
    gfx_SetColor(C_BLACK);
    gfx_FillRectangle(0, 2, 14, 14);
    gfx_SetColor(C_BLUE);
    gfx_FillRectangle(2, 4, 10, 10);
    spr_player = grab(14, 18);

    /* spike the wawas: triángulo blanco con borde negro */
    clear_area(16, 16);
    gfx_SetColor(C_BLACK);
    gfx_FillTriangle(8, 0, 0, 15, 15, 15);
    gfx_SetColor(C_WHITE);
    gfx_FillTriangle(8, 4, 3, 13, 12, 13);
    spr_spike = grab(16, 16);

    /* cactus */
    clear_area(16, 24);
    gfx_SetColor(C_DGREEN);
    gfx_FillRectangle(5, 1, 6, 23);
    gfx_FillRectangle(0, 8, 5, 3);
    gfx_FillRectangle(0, 3, 3, 6);
    gfx_FillRectangle(11, 12, 5, 3);
    gfx_FillRectangle(13, 6, 3, 7);
    gfx_SetColor(C_CACTUS);
    gfx_FillRectangle(6, 2, 4, 21);
    gfx_SetColor(C_WHITE);
    gfx_SetPixel(7, 6);
    gfx_SetPixel(8, 12);
    gfx_SetPixel(7, 18);
    spr_cactus = grab(16, 24);
}

void gfx_setup(void) {
    static uint16_t pal[256];
    int i;

    for (i = 0; i < C_NUM; i++)
        pal[i] = gfx_RGBTo1555(pal_rgb[i][0], pal_rgb[i][1], pal_rgb[i][2]);
    for (i = 0; i < SPRITE_PAL_COUNT; i++)
        pal[SPRITE_PAL_FIRST + i] = gfx_RGBTo1555(sprite_palette[i][0], sprite_palette[i][1], sprite_palette[i][2]);
    gfx_SetPalette(pal, sizeof pal, 0);
    gfx_SetTransparentColor(C_TRANS);
    gfx_SetTextTransparentColor(C_TRANS);
    gfx_SetTextBGColor(C_TRANS);

    gfx_SetDrawBuffer();
    make_sprites();
    gfx_FillScreen(C_BLACK);
}

/* ---------------- Texto ---------------- */
void text_center(const char *s, int y, uint8_t scale, uint8_t color) {
    int w;
    gfx_SetTextScale(scale, scale);
    w = gfx_GetStringWidth(s);
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(s, (SCR_W - w) / 2, y);
    gfx_SetTextScale(1, 1);
}

void text_at(const char *s, int x, int y, uint8_t color) {
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(s, x, y);
}

void draw_heart(int x, int y, bool full) {
    gfx_SetColor(full ? C_RED : C_DGREY);
    gfx_FillCircle(x + 3, y + 3, 3);
    gfx_FillCircle(x + 8, y + 3, 3);
    gfx_FillTriangle(x, y + 4, x + 11, y + 4, x + 5, y + 10);
}

void draw_cursor_arrow(int x, int y) {
    gfx_SetColor(C_BLACK);
    gfx_FillTriangle(x - 1, y - 2, x - 1, y + 14, x + 11, y + 10);
    gfx_SetColor(C_WHITE);
    gfx_FillTriangle(x, y, x, y + 11, x + 8, y + 9);
}

void draw_crosshair(int x, int y) {
    gfx_SetColor(C_RED);
    gfx_Circle(x, y, 7);
    gfx_HorizLine(x - 10, y, 7);
    gfx_HorizLine(x + 4, y, 7);
    gfx_VertLine(x, y - 10, 7);
    gfx_VertLine(x, y + 4, 7);
}

/* ---------------- Entrada y tiempo ---------------- */
static bool prev_act, prev_ok, prev_quit, prev_up, prev_down;
static clock_t last_frame;

void input_read(void) {
    bool act, enter, quit;

    kb_Scan();
    in.up = kb_Data[7] & kb_Up;
    in.down = kb_Data[7] & kb_Down;
    in.left = kb_Data[7] & kb_Left;
    in.right = kb_Data[7] & kb_Right;
    act = (kb_Data[1] & kb_2nd) || (kb_Data[2] & kb_Alpha);
    enter = kb_Data[6] & kb_Enter;
    quit = kb_Data[6] & kb_Clear;

    in.act = act;
    in.act_p = act && !prev_act;
    in.ok_p = (act && !prev_act) || (enter && !prev_ok);
    in.quit_p = quit && !prev_quit;
    in.up_p = in.up && !prev_up;
    in.down_p = in.down && !prev_down;

    prev_act = act;
    prev_ok = enter;
    prev_quit = quit;
    prev_up = in.up;
    prev_down = in.down;
}

void wait_release(void) {
    do {
        kb_Scan();
    } while (kb_AnyKey());
    prev_act = prev_ok = prev_quit = prev_up = prev_down = false;
}

void frame_end(void) {
    gfx_SwapDraw();
    while ((clock_t)(clock() - last_frame) < CLOCKS_PER_SEC / FPS)
        ;
    last_frame = clock();
}

/* ---------------- Utilidades ---------------- */
int rnd(int lo, int hi) {
    return lo + (int)((unsigned)rand() % (unsigned)(hi - lo + 1));
}

int isqrt(int v) {
    int r = 0, b = 1 << 22;
    if (v <= 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

bool overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ---------------- Dificultad ---------------- */
int g_endless_round;

/* En endless cada ronda quita medio segundo, hasta un mínimo del 55 % */
int time_limit(int secs) {
    int full = SEC(secs);
    int t;
    if (g_endless_round <= 1) return full;
    t = full - (g_endless_round - 1) * (FPS / 2);
    if (t < full * 55 / 100) t = full * 55 / 100;
    return t;
}

/* ---------------- HUD ---------------- */
void hud_draw(const char *label, int value, int goal, int frames_left, int total_frames, int hearts, int max_hearts) {
    int i;

    gfx_SetColor(C_DGREY);
    gfx_FillRectangle(0, 0, SCR_W, HUD_H);
    gfx_SetColor(C_BLACK);
    gfx_HorizLine(0, HUD_H - 1, SCR_W);

    gfx_SetTextFGColor(C_WHITE);
    gfx_SetTextXY(4, 4);
    if (label) {
        gfx_PrintString(label);
        if (goal >= 0) {
            gfx_PrintInt(value, 1);
            gfx_PrintString("/");
            gfx_PrintInt(goal, 1);
        }
    }

    for (i = 0; i < max_hearts; i++)
        draw_heart(152 + i * 14, 3, i < hearts);

    if (total_frames > 0) {
        int w = (frames_left * 60) / total_frames;
        gfx_SetColor(C_BLACK);
        gfx_FillRectangle(230, 4, 62, 8);
        gfx_SetColor(frames_left < SEC(5) ? C_RED : C_YELLOW);
        gfx_FillRectangle(231, 5, w, 6);
        gfx_SetTextXY(296, 4);
        gfx_PrintInt((frames_left + FPS - 1) / FPS, 2);
    }
}

void hud_flash_damage(void) {
    gfx_SetColor(C_RED);
    gfx_Rectangle(0, HUD_H, SCR_W, SCR_H - HUD_H);
    gfx_Rectangle(1, HUD_H + 1, SCR_W - 2, SCR_H - HUD_H - 2);
}
