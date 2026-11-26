/*
 * Gráficos, entrada y utilidades comunes.
 * Todos los sprites se dibujan en tiempo de ejecución con primitivas,
 * así el programa no necesita convertir imágenes.
 */
#include "wawas.h"

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

static const uint8_t wawa_colors[W_NUM][3] = {
    /* cuerpo, mancha, ojos */
    {C_WHITE,  C_BROWN,  C_EYE},
    {C_RED,    C_DRED,   C_YELLOW},
    {C_LBLUE,  C_BLUE,   C_DBLUE},
    {C_GREEN,  C_DGREEN, C_YELLOW},
    {C_YELLOW, C_ORANGE, C_BLUE},
    {C_PINK,   C_PURPLE, C_EYE},
    {C_GB3,    C_GB1,    C_GB0},
};

/* ---------------- Dibujo de personajes ---------------- */

/* Un wawa: gato redondo sin orejas, blanco con una mancha marrón arriba,
 * ojos verdes, dos patitas y cara seria. */
void draw_wawa_prim(int cx, int cy, int r, uint8_t body, uint8_t patch, uint8_t eye) {
    int leg = r / 3 + 1;
    int er = r / 5;

    /* patas */
    gfx_SetColor(C_BLACK);
    gfx_FillRectangle(cx - r / 2 - leg / 2 - 1, cy + r - 3, leg + 2, leg + 3);
    gfx_FillRectangle(cx + r / 2 - leg / 2 - 1, cy + r - 3, leg + 2, leg + 3);
    gfx_SetColor(body);
    gfx_FillRectangle(cx - r / 2 - leg / 2, cy + r - 3, leg, leg + 2);
    gfx_FillRectangle(cx + r / 2 - leg / 2, cy + r - 3, leg, leg + 2);

    /* cuerpo */
    gfx_SetColor(C_BLACK);
    gfx_FillCircle(cx, cy, r);
    gfx_SetColor(body);
    gfx_FillCircle(cx, cy, r - 1);

    /* mancha de arriba */
    gfx_SetColor(patch);
    gfx_FillEllipse(cx, cy - r / 2, r / 2 + r / 4, r / 2);

    /* ojos */
    if (er < 1) er = 1;
    gfx_SetColor(eye);
    gfx_FillCircle(cx - (r * 2) / 5, cy + r / 5, er);
    gfx_FillCircle(cx + (r * 2) / 5, cy + r / 5, er);
    gfx_SetColor(C_BLACK);
    gfx_SetPixel(cx - (r * 2) / 5, cy + r / 5);
    gfx_SetPixel(cx + (r * 2) / 5, cy + r / 5);
    if (r >= 10) {
        gfx_FillRectangle(cx - (r * 2) / 5 - 1, cy + r / 5 - 1, 2, 2);
        gfx_FillRectangle(cx + (r * 2) / 5 - 1, cy + r / 5 - 1, 2, 2);
    }

    /* boca seria */
    gfx_HorizLine(cx - r / 6, cy + r / 2 + 1, r / 3 + 1);
}

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
        clear_area(16, 16);
        draw_wawa_prim(8, 7, 7, wawa_colors[i][0], wawa_colors[i][1], wawa_colors[i][2]);
        spr_wawa16[i] = grab(16, 16);

        clear_area(24, 24);
        draw_wawa_prim(12, 10, 10, wawa_colors[i][0], wawa_colors[i][1], wawa_colors[i][2]);
        spr_wawa24[i] = grab(24, 24);
    }

    /* wawa grande del título */
    clear_area(64, 64);
    draw_wawa_prim(32, 28, 27, C_WHITE, C_BROWN, C_EYE);
    spr_wawa_big = grab(64, 64);

    /* wawa muerto: rojo y aplastado */
    clear_area(16, 16);
    gfx_SetColor(C_DRED);
    gfx_FillEllipse(8, 12, 7, 3);
    gfx_SetColor(C_RED);
    gfx_FillEllipse(8, 12, 6, 2);
    spr_dead16 = grab(16, 16);

    clear_area(24, 24);
    gfx_SetColor(C_DRED);
    gfx_FillEllipse(12, 19, 11, 4);
    gfx_SetColor(C_RED);
    gfx_FillEllipse(12, 19, 10, 3);
    spr_dead24 = grab(24, 24);

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

    /* Petrovich: el jefe final (perro enorme y enfadado) */
    clear_area(48, 48);
    gfx_SetColor(C_DBROWN);
    gfx_FillTriangle(4, 2, 16, 10, 6, 20);      /* orejas */
    gfx_FillTriangle(43, 2, 31, 10, 41, 20);
    gfx_SetColor(C_BLACK);
    gfx_FillCircle(24, 26, 20);
    gfx_SetColor(C_GREY);
    gfx_FillCircle(24, 26, 19);
    gfx_SetColor(C_TAN);
    gfx_FillEllipse(24, 34, 11, 8);             /* hocico */
    gfx_SetColor(C_RED);
    gfx_FillCircle(16, 21, 3);                  /* ojos rojos */
    gfx_FillCircle(32, 21, 3);
    gfx_SetColor(C_BLACK);
    gfx_Line(10, 14, 20, 18);                   /* cejas */
    gfx_Line(38, 14, 28, 18);
    gfx_FillEllipse(24, 30, 4, 2);              /* nariz */
    gfx_HorizLine(16, 38, 17);                  /* boca */
    gfx_SetColor(C_WHITE);
    gfx_FillTriangle(18, 39, 21, 39, 19, 43);   /* colmillos */
    gfx_FillTriangle(27, 39, 30, 39, 29, 43);
    spr_petro = grab(48, 48);

    /* jugador: chaval con gorra roja */
    clear_area(14, 18);
    gfx_SetColor(C_BLUE);
    gfx_FillRectangle(3, 11, 8, 5);             /* camiseta */
    gfx_SetColor(C_DBLUE);
    gfx_FillRectangle(3, 16, 3, 2);             /* piernas */
    gfx_FillRectangle(8, 16, 3, 2);
    gfx_SetColor(C_BLACK);
    gfx_FillCircle(7, 6, 5);
    gfx_SetColor(C_YELLOW);
    gfx_FillCircle(7, 6, 4);
    gfx_SetColor(C_RED);
    gfx_FillRectangle(2, 1, 10, 3);             /* gorra */
    gfx_FillRectangle(9, 3, 5, 1);
    gfx_SetColor(C_BLACK);
    gfx_SetPixel(5, 6);
    gfx_SetPixel(9, 6);
    gfx_HorizLine(6, 8, 3);
    spr_player = grab(14, 18);

    /* bola de pinchos */
    clear_area(16, 16);
    gfx_SetColor(C_DGREY);
    gfx_Line(0, 8, 15, 8);
    gfx_Line(8, 0, 8, 15);
    gfx_Line(2, 2, 13, 13);
    gfx_Line(13, 2, 2, 13);
    gfx_SetColor(C_BLACK);
    gfx_FillCircle(8, 8, 5);
    gfx_SetColor(C_GREY);
    gfx_FillCircle(8, 8, 4);
    gfx_SetColor(C_WHITE);
    gfx_SetPixel(6, 6);
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
    static uint16_t pal[C_NUM];
    int i;

    for (i = 0; i < C_NUM; i++)
        pal[i] = gfx_RGBTo1555(pal_rgb[i][0], pal_rgb[i][1], pal_rgb[i][2]);
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
