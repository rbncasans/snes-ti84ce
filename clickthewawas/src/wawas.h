/*
 * click the wawas - port para TI-84 Plus CE
 * Juego original de aidenkp11 (Scratch / TurboWarp). Port no oficial.
 */
#ifndef WAWAS_H
#define WAWAS_H

#include <graphx.h>
#include <stdbool.h>
#include <stdint.h>

/* ---------- Pantalla y tiempo ---------- */
#define SCR_W 320
#define SCR_H 240
#define HUD_H 16          /* barra superior */
#define FPS 30
#define SEC(s) ((s) * FPS)

/* ---------- Paleta ---------- */
enum {
    C_TRANS = 0, C_BLACK, C_WHITE, C_BROWN, C_EYE, C_RED, C_DRED, C_BLUE,
    C_GREY, C_DGREY, C_LGREY, C_YELLOW, C_SKY, C_ORANGE, C_PURPLE,
    C_GB0, C_GB1, C_GB2, C_GB3, C_DGREEN, C_SPACE, C_PINK, C_TAN, C_FLOOR,
    C_LBLUE, C_CACTUS, C_DBROWN, C_GREEN, C_DBLUE, C_NUM
};

/* ---------- Tipos de wawa (colores) ---------- */
enum { W_NORMAL, W_RED, W_BLUE, W_GREEN, W_YELLOW, W_PURPLE, W_GB, W_NUM };

extern gfx_sprite_t *spr_wawa16[W_NUM];
extern gfx_sprite_t *spr_wawa24[W_NUM];
extern gfx_sprite_t *spr_dead16;
extern gfx_sprite_t *spr_dead24;
extern gfx_sprite_t *spr_dog_r, *spr_dog_l;
extern gfx_sprite_t *spr_petro;
extern gfx_sprite_t *spr_player;
extern gfx_sprite_t *spr_spike;
extern gfx_sprite_t *spr_cactus;
extern gfx_sprite_t *spr_wawa_big;

void gfx_setup(void);
void draw_wawa_prim(int cx, int cy, int r, uint8_t body, uint8_t patch, uint8_t eye);
void text_center(const char *s, int y, uint8_t scale, uint8_t color);
void text_at(const char *s, int x, int y, uint8_t color);
void draw_heart(int x, int y, bool full);
void draw_cursor_arrow(int x, int y);
void draw_crosshair(int x, int y);

/* ---------- Entrada ---------- */
typedef struct {
    bool up, down, left, right;
    bool act;      /* [2nd] mantenido */
    bool act_p;    /* [2nd] recién pulsado */
    bool ok_p;     /* [2nd] o [enter] recién pulsado */
    bool up_p, down_p;
    bool quit_p;   /* [clear] recién pulsado */
} input_t;

extern input_t in;
void input_read(void);
void frame_end(void);           /* intercambia buffers y limita a 30 fps */
void wait_release(void);

/* ---------- Utilidades ---------- */
int rnd(int lo, int hi);        /* entero en [lo, hi] */
int isqrt(int v);
bool overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);
int clampi(int v, int lo, int hi);

/* HUD común */
void hud_draw(const char *label, int value, int goal, int frames_left, int total_frames, int hearts, int max_hearts);
void hud_flash_damage(void);

/* ---------- Minijuegos ---------- */
typedef enum { RES_WIN, RES_LOSE, RES_QUIT } result_t;

#define NUM_STAGES 12

typedef struct {
    const char *menu_name;   /* nombre en el menú de minijuegos */
    const char *prompt;      /* texto original del aviso */
    const char *hint;        /* instrucciones en español */
    result_t (*play)(void);
} minigame_t;

extern const minigame_t minigames[NUM_STAGES];

result_t mg_click(void);
result_t mg_spike(void);
result_t mg_shoot(void);
result_t mg_space(void);
result_t mg_catch(void);
result_t mg_revenge1(void);
result_t mg_redwawas(void);
result_t mg_dogs(void);
result_t mg_crush(void);
result_t mg_gameboy(void);
result_t mg_rob(void);
result_t mg_revenge2(void);

#endif
