/*
 * Los 12 minijuegos del modo clásico.
 * Versiones adaptadas a la pantalla de 320x240 y al teclado de la calculadora:
 *   flechas = moverse / mover el cursor, [2nd] = clic / disparar / atacar,
 *   [clear] = volver al menú.
 */
#include "wawas.h"

#include <stdlib.h>
#include <string.h>

#define DEAD_FRAMES 12
#define IFRAMES 40

/* Un objeto genérico con posición en coma fija 8.8 */
typedef struct {
    int x, y;       /* posición (<<8) */
    int vx, vy;     /* velocidad (<<8) */
    uint8_t type;
    int8_t hp;
    uint8_t dead;   /* >0: animación de muerte en curso */
    bool alive;
    int timer;
} obj_t;

#define FX(v) ((v) * 256)
#define PX(v) ((v) >> 8)

/* Dibuja un wawa vivo o su animación de muerte (parpadeo rojo y aplastado) */
static void draw_obj_wawa16(const obj_t *o) {
    if (o->alive)
        gfx_TransparentSprite(spr_wawa16[o->type], PX(o->x), PX(o->y));
    else if (o->dead && (o->dead & 2))
        gfx_TransparentSprite(spr_dead16, PX(o->x), PX(o->y));
}

static void draw_obj_wawa24(const obj_t *o) {
    if (o->alive)
        gfx_TransparentSprite(spr_wawa24[o->type], PX(o->x), PX(o->y));
    else if (o->dead && (o->dead & 2))
        gfx_TransparentSprite(spr_dead24, PX(o->x), PX(o->y));
}

static void kill_obj(obj_t *o) {
    o->alive = false;
    o->dead = DEAD_FRAMES;
}

static void tick_dead(obj_t *arr, int n) {
    int i;
    for (i = 0; i < n; i++)
        if (arr[i].dead) arr[i].dead--;
}

/* Movimiento del jugador con flechas, limitado a una caja */
static void move_player(int *x, int *y, int spd, int w, int h, int top) {
    if (in.left) *x -= spd;
    if (in.right) *x += spd;
    if (in.up) *y -= spd;
    if (in.down) *y += spd;
    *x = clampi(*x, 0, SCR_W - w);
    *y = clampi(*y, top, SCR_H - h);
}

/* Cursor con aceleración */
static int cursor_hold;
static void move_cursor(int *x, int *y, int top) {
    int spd;
    if (in.left || in.right || in.up || in.down) {
        if (cursor_hold < 24) cursor_hold++;
    } else {
        cursor_hold = 0;
    }
    spd = 2 + cursor_hold / 4;
    if (in.left) *x -= spd;
    if (in.right) *x += spd;
    if (in.up) *y -= spd;
    if (in.down) *y += spd;
    *x = clampi(*x, 0, SCR_W - 1);
    *y = clampi(*y, top, SCR_H - 1);
}

/* Velocidad aleatoria (<<8) con módulo entre lo y hi y signo aleatorio */
static int rnd_vel(int lo, int hi) {
    int v = rnd(lo, hi);
    return rnd(0, 1) ? v : -v;
}

/* Rebote en los bordes de la zona de juego */
static void bounce(obj_t *o, int w, int h, int top) {
    o->x += o->vx;
    o->y += o->vy;
    if (o->x < 0) { o->x = 0; o->vx = -o->vx; }
    if (o->x > FX(SCR_W - w)) { o->x = FX(SCR_W - w); o->vx = -o->vx; }
    if (o->y < FX(top)) { o->y = FX(top); o->vy = -o->vy; }
    if (o->y > FX(SCR_H - h)) { o->y = FX(SCR_H - h); o->vy = -o->vy; }
}

/* Velocidad hacia un punto con módulo speed (<<8) */
static void aim(int fx, int fy, int tx, int ty, int speed, int *vx, int *vy) {
    int dx = tx - fx, dy = ty - fy;
    int len = isqrt(dx * dx + dy * dy);
    if (len == 0) { *vx = 0; *vy = 0; return; }
    *vx = dx * speed / len;
    *vy = dy * speed / len;
}

/* ======================================================================
 * 1. click the wawas!!  -  Haz clic en 35 wawas en 30 segundos
 * ====================================================================== */
result_t mg_click(void) {
    obj_t w[6];
    int cx = 160, cy = 130, clicked = 0, i;
    int t = SEC(30);
    const int goal = 35;

    cursor_hold = 0;
    for (i = 0; i < 6; i++) {
        w[i].x = FX(rnd(0, SCR_W - 24));
        w[i].y = FX(rnd(HUD_H, SCR_H - 24));
        w[i].vx = rnd_vel(64, 200);
        w[i].vy = rnd_vel(64, 200);
        w[i].type = W_NORMAL;
        w[i].alive = true;
        w[i].dead = 0;
    }

    for (;;) {
        input_read();
        if (in.quit_p) return RES_QUIT;
        move_cursor(&cx, &cy, HUD_H);

        for (i = 0; i < 6; i++) {
            if (w[i].alive) bounce(&w[i], 24, 24, HUD_H);
            else if (!w[i].dead) {
                /* reaparece en otro sitio */
                w[i].x = FX(rnd(0, SCR_W - 24));
                w[i].y = FX(rnd(HUD_H, SCR_H - 24));
                w[i].alive = true;
            }
        }
        tick_dead(w, 6);

        if (in.act_p) {
            for (i = 5; i >= 0; i--) {
                if (w[i].alive && overlap(cx, cy, 1, 1, PX(w[i].x) + 2, PX(w[i].y), 20, 24)) {
                    kill_obj(&w[i]);
                    clicked++;
                    break;
                }
            }
        }
        if (clicked >= goal) return RES_WIN;
        if (--t <= 0) return RES_LOSE;

        gfx_FillScreen(C_SKY);
        gfx_SetColor(C_GREEN);
        gfx_FillRectangle(0, 200, SCR_W, 40);
        for (i = 0; i < 6; i++) draw_obj_wawa24(&w[i]);
        draw_cursor_arrow(cx, cy);
        hud_draw("wawas: ", clicked, goal, t, SEC(30), 0, 0);
        frame_end();
    }
}

/* ======================================================================
 * 2. spike the wawas  -  Toca todos los wawas que rebotan
 * ====================================================================== */
result_t mg_spike(void) {
    obj_t w[8];
    int px = 152, py = 120, i, left = 8;
    int t = SEC(30);

    for (i = 0; i < 8; i++) {
        w[i].x = FX(rnd(0, SCR_W - 16));
        w[i].y = FX(rnd(HUD_H, 100));
        w[i].vx = rnd_vel(256, 512);
        w[i].vy = rnd_vel(256, 512);
        w[i].type = W_NORMAL;
        w[i].alive = true;
        w[i].dead = 0;
    }

    for (;;) {
        input_read();
        if (in.quit_p) return RES_QUIT;
        move_player(&px, &py, 3, 16, 16, HUD_H);

        for (i = 0; i < 8; i++) {
            if (!w[i].alive) continue;
            bounce(&w[i], 16, 16, HUD_H);
            if (overlap(px + 2, py + 2, 12, 12, PX(w[i].x) + 1, PX(w[i].y), 14, 16)) {
                kill_obj(&w[i]);
                left--;
            }
        }
        tick_dead(w, 8);
        if (left == 0) return RES_WIN;
        if (--t <= 0) return RES_LOSE;

        gfx_FillScreen(C_LBLUE);
        for (i = 0; i < 8; i++) draw_obj_wawa16(&w[i]);
        gfx_TransparentSprite(spr_spike, px, py);
        hud_draw("wawas: ", 8 - left, 8, t, SEC(30), 0, 0);
        frame_end();
    }
}

/* ======================================================================
 * 3. shoot the wawa  -  Apunta con el cursor y dispara con [2nd]
 * ====================================================================== */
#define MAX_BULLETS 5
result_t mg_shoot(void) {
    obj_t w[6];
    obj_t b[MAX_BULLETS];
    int cx = 160, cy = 100, i, j, left = 6;
    int t = SEC(20);
    const int gx = 160, gy = 230;

    cursor_hold = 0;
    for (i = 0; i < 6; i++) {
        w[i].x = FX(rnd(0, SCR_W - 24));
        w[i].y = FX(HUD_H + 8 + i * 28);
        w[i].vx = rnd_vel(256, 640);
        w[i].vy = 0;
        w[i].type = W_NORMAL;
        w[i].alive = true;
        w[i].dead = 0;
    }
    for (i = 0; i < MAX_BULLETS; i++) b[i].alive = false;

    for (;;) {
        input_read();
        if (in.quit_p) return RES_QUIT;
        move_cursor(&cx, &cy, HUD_H);

        if (in.act_p) {
            for (i = 0; i < MAX_BULLETS; i++) {
                if (!b[i].alive) {
                    b[i].alive = true;
                    b[i].x = FX(gx);
                    b[i].y = FX(gy);
                    aim(gx, gy, cx, cy, FX(8), &b[i].vx, &b[i].vy);
                    break;
                }
            }
        }

        for (i = 0; i < 6; i++)
            if (w[i].alive) bounce(&w[i], 24, 24, HUD_H);
        tick_dead(w, 6);

        for (i = 0; i < MAX_BULLETS; i++) {
            int bx, by;
            if (!b[i].alive) continue;
            b[i].x += b[i].vx;
            b[i].y += b[i].vy;
            bx = PX(b[i].x);
            by = PX(b[i].y);
            if (bx < 0 || bx >= SCR_W || by < HUD_H || by >= SCR_H) {
                b[i].alive = false;
                continue;
            }
            for (j = 0; j < 6; j++) {
                if (w[j].alive && overlap(bx - 1, by - 1, 3, 3, PX(w[j].x) + 2, PX(w[j].y), 20, 22)) {
                    kill_obj(&w[j]);
                    left--;
                    b[i].alive = false;
                    break;
                }
            }
        }
        if (left == 0) return RES_WIN;
        if (--t <= 0) return RES_LOSE;

        gfx_FillScreen(C_TAN);
        gfx_SetColor(C_FLOOR);
        gfx_FillRectangle(0, 210, SCR_W, 30);
        for (i = 0; i < 6; i++) draw_obj_wawa24(&w[i]);
        gfx_SetColor(C_YELLOW);
        for (i = 0; i < MAX_BULLETS; i++)
            if (b[i].alive) gfx_FillCircle(PX(b[i].x), PX(b[i].y), 2);
        /* pistola */
        {
            int vx, vy;
            aim(gx, gy, cx, cy, 22, &vx, &vy);
            gfx_SetColor(C_DGREY);
            gfx_Line(gx, gy, gx + vx, gy + vy);
            gfx_Line(gx + 1, gy, gx + vx + 1, gy + vy);
            gfx_SetColor(C_BLACK);
            gfx_FillCircle(gx, gy + 4, 7);
        }
        draw_crosshair(cx, cy);
        hud_draw("wawas: ", 6 - left, 6, t, SEC(20), 0, 0);
        frame_end();
    }
}

/* ======================================================================
 * 4. we r in space!!!  -  Flota con inercia y toca a todos los wawas
 * ====================================================================== */
result_t mg_space(void) {
    obj_t w[6];
    static uint16_t sx[40];
    static uint8_t sy[40];
    int x = FX(150), y = FX(120), vx = 0, vy = 0, i, left = 6;
    int t = SEC(30);
    const int maxv = FX(4);

    for (i = 0; i < 40; i++) {
        sx[i] = rnd(0, SCR_W - 1);
        sy[i] = rnd(HUD_H, SCR_H - 1);
    }
    for (i = 0; i < 6; i++) {
        w[i].x = FX(rnd(0, SCR_W - 16));
        w[i].y = FX(rnd(HUD_H, SCR_H - 16));
        w[i].vx = rnd_vel(64, 220);
        w[i].vy = rnd_vel(64, 220);
        w[i].type = W_NORMAL;
        w[i].alive = true;
        w[i].dead = 0;
    }

    for (;;) {
        input_read();
        if (in.quit_p) return RES_QUIT;
        if (in.left) vx -= 40;
        if (in.right) vx += 40;
        if (in.up) vy -= 40;
        if (in.down) vy += 40;
        vx -= vx / 48;
        vy -= vy / 48;
        vx = clampi(vx, -maxv, maxv);
        vy = clampi(vy, -maxv, maxv);
        x += vx;
        y += vy;
        /* el espacio da la vuelta */
        if (x < FX(-14)) x = FX(SCR_W);
        if (x > FX(SCR_W)) x = FX(-14);
        if (y < FX(HUD_H - 18)) y = FX(SCR_H);
        if (y > FX(SCR_H)) y = FX(HUD_H - 18);

        for (i = 0; i < 6; i++) {
            if (!w[i].alive) continue;
            w[i].x += w[i].vx;
            w[i].y += w[i].vy;
            if (w[i].x < FX(-16)) w[i].x = FX(SCR_W);
            if (w[i].x > FX(SCR_W)) w[i].x = FX(-16);
            if (w[i].y < FX(HUD_H - 16)) w[i].y = FX(SCR_H);
            if (w[i].y > FX(SCR_H)) w[i].y = FX(HUD_H - 16);
            if (overlap(PX(x) + 2, PX(y) + 2, 10, 14, PX(w[i].x) + 1, PX(w[i].y), 14, 16)) {
                kill_obj(&w[i]);
                left--;
            }
        }
        tick_dead(w, 6);
        if (left == 0) return RES_WIN;
        if (--t <= 0) return RES_LOSE;

        gfx_FillScreen(C_SPACE);
        gfx_SetColor(C_WHITE);
        for (i = 0; i < 40; i++) gfx_SetPixel(sx[i], sy[i]);
        gfx_SetColor(C_GREY);
        gfx_FillCircle(260, 190, 30);
        gfx_SetColor(C_DGREY);
        gfx_FillCircle(250, 180, 6);
        gfx_FillCircle(272, 200, 4);
        for (i = 0; i < 6; i++) draw_obj_wawa16(&w[i]);
        /* casco de astronauta */
        gfx_SetColor(C_LBLUE);
        gfx_Circle(PX(x) + 7, PX(y) + 6, 8);
        gfx_TransparentSprite(spr_player, PX(x), PX(y));
        hud_draw("wawas: ", 6 - left, 6, t, SEC(30), 0, 0);
        frame_end();
    }
}

/* ======================================================================
 * 5. catch the wawas!!!  -  Atrapa los wawas que caen con la cesta
 * ====================================================================== */
result_t mg_catch(void) {
    obj_t w[8];
    int px = 150, i, caught = 0, spawn = 0;
    int t = SEC(30);
    const int goal = 15, py = 200;

    for (i = 0; i < 8; i++) { w[i].alive = false; w[i].dead = 0; }

    for (;;) {
        input_read();
        if (in.quit_p) return RES_QUIT;
        if (in.left) px -= 5;
        if (in.right) px += 5;
        px = clampi(px, 0, SCR_W - 32);

        if (--spawn <= 0) {
            for (i = 0; i < 8; i++) {
                if (!w[i].alive && !w[i].dead) {
                    w[i].alive = true;
                    w[i].x = FX(rnd(0, SCR_W - 16));
                    w[i].y = FX(HUD_H - 16);
                    w[i].vy = rnd(400, 700);
                    w[i].type = W_NORMAL;
                    break;
                }
            }
            spawn = rnd(18, 30);
        }

        for (i = 0; i < 8; i++) {
            if (!w[i].alive) continue;
            w[i].y += w[i].vy;
            if (overlap(px, py + 6, 32, 8, PX(w[i].x) + 2, PX(w[i].y) + 8, 12, 8)) {
                w[i].alive = false;
                caught++;
            } else if (PX(w[i].y) > 220) {
                kill_obj(&w[i]);   /* se estrella contra el suelo */
                w[i].y = FX(220);
            }
        }
        tick_dead(w, 8);
        if (caught >= goal) return RES_WIN;
        if (--t <= 0) return RES_LOSE;

        gfx_FillScreen(C_SKY);
        gfx_SetColor(C_GREEN);
        gfx_FillRectangle(0, 228, SCR_W, 12);
        for (i = 0; i < 8; i++) draw_obj_wawa16(&w[i]);
        gfx_TransparentSprite(spr_player, px + 9, py + 10);
        /* cesta */
        gfx_SetColor(C_DBROWN);
        gfx_FillRectangle(px, py + 6, 32, 10);
        gfx_SetColor(C_BROWN);
        gfx_FillRectangle(px + 2, py + 8, 28, 6);
        hud_draw("atrapados: ", caught, goal, t, SEC(30), 0, 0);
        frame_end();
    }
}

/* ======================================================================
 * 6 y 12. revenge part one / two  -  Los wawas atacan; [2nd] para golpear
 * ====================================================================== */
#define REV_MAX 6

typedef struct {
    int hp;
    int speed;  /* <<8 */
} wtype_t;

static const wtype_t wtypes[6] = {
    {1, 200},  /* normal */
    {1, 360},  /* rojo: rápido */
    {3, 140},  /* azul: duro y lento */
    {2, 230},  /* verde */
    {1, 300},  /* amarillo */
    {2, 260},  /* morado */
};

static result_t revenge(int total, int ntypes, bool boss) {
    obj_t w[REV_MAX];
    int px = 153, py = 120, hearts = 5, inv = 0, i;
    int facing = 0;          /* 0 der, 1 izq, 2 arriba, 3 abajo */
    int atk = 0, cool = 0;   /* ataque */
    int spawned = 0, killed = 0, spawn = 20;
    /* jefe */
    bool boss_on = false;
    int bx = 0, by = 0, bvx = 0, bvy = 0, bhp = 0, bstate = 0, btimer = 0, bhit = 0;
    const int bmax = 24;

    for (i = 0; i < REV_MAX; i++) { w[i].alive = false; w[i].dead = 0; }

    for (;;) {
        int ax = 0, ay = 0, aw = 0, ah = 0;

        input_read();
        if (in.quit_p) return RES_QUIT;
        move_player(&px, &py, 3, 14, 18, HUD_H);
        if (in.right) facing = 0;
        else if (in.left) facing = 1;
        else if (in.up) facing = 2;
        else if (in.down) facing = 3;

        if (cool) cool--;
        if (in.act_p && !cool) {
            atk = 6;
            cool = 10;
        }
        if (atk) {
            atk--;
            switch (facing) {
                case 0: ax = px + 12; ay = py - 4; aw = 22; ah = 26; break;
                case 1: ax = px - 20; ay = py - 4; aw = 22; ah = 26; break;
                case 2: ax = px - 6; ay = py - 20; aw = 26; ah = 22; break;
                default: ax = px - 6; ay = py + 14; aw = 26; ah = 22; break;
            }
        }

        /* aparecen wawas por los bordes */
        if (spawned < total && --spawn <= 0) {
            for (i = 0; i < REV_MAX; i++) {
                if (!w[i].alive && !w[i].dead) {
                    int side = rnd(0, 3);
                    w[i].type = (uint8_t)rnd(0, ntypes - 1);
                    w[i].hp = wtypes[w[i].type].hp;
                    w[i].alive = true;
                    w[i].timer = 0;
                    if (side == 0) { w[i].x = FX(-16); w[i].y = FX(rnd(HUD_H, SCR_H - 16)); }
                    else if (side == 1) { w[i].x = FX(SCR_W); w[i].y = FX(rnd(HUD_H, SCR_H - 16)); }
                    else if (side == 2) { w[i].x = FX(rnd(0, SCR_W - 16)); w[i].y = FX(HUD_H - 16); }
                    else { w[i].x = FX(rnd(0, SCR_W - 16)); w[i].y = FX(SCR_H); }
                    spawned++;
                    break;
                }
            }
            spawn = rnd(25, 45);
        }

        for (i = 0; i < REV_MAX; i++) {
            int wx, wy;
            if (!w[i].alive) continue;
            wx = PX(w[i].x);
            wy = PX(w[i].y);
            if (w[i].timer) {
                /* retroceso tras un golpe */
                w[i].timer--;
            } else {
                aim(wx, wy, px, py, wtypes[w[i].type].speed, &w[i].vx, &w[i].vy);
                if (w[i].type == 4) {  /* el amarillo se mueve a lo loco */
                    w[i].vx += rnd(-200, 200);
                    w[i].vy += rnd(-200, 200);
                }
            }
            w[i].x += w[i].vx;
            w[i].y += w[i].vy;
            wx = PX(w[i].x);
            wy = PX(w[i].y);

            if (atk && overlap(ax, ay, aw, ah, wx, wy, 16, 16) && w[i].timer == 0) {
                if (--w[i].hp <= 0) {
                    kill_obj(&w[i]);
                    killed++;
                    continue;
                }
                w[i].vx = -w[i].vx * 3;
                w[i].vy = -w[i].vy * 3;
                w[i].timer = 8;
            }
            if (!inv && overlap(px + 2, py + 2, 10, 14, wx + 2, wy + 2, 12, 12)) {
                hearts--;
                inv = IFRAMES;
            }
        }
        tick_dead(w, REV_MAX);

        if (killed >= total) {
            if (!boss) return RES_WIN;
            if (!boss_on && bhp == 0) {
                boss_on = true;
                bhp = bmax;
                bx = FX(136);
                by = FX(HUD_H - 48);
                bstate = 0;
                btimer = SEC(2);
            }
        }

        if (boss_on) {
            int bpx = PX(bx), bpy = PX(by);
            if (bhit) bhit--;
            switch (bstate) {
                case 0:  /* entrada y persecución lenta */
                    aim(bpx + 24, bpy + 24, px + 7, py + 9, 150, &bvx, &bvy);
                    if (--btimer <= 0) { bstate = 1; btimer = 20; }
                    break;
                case 1:  /* aviso: tiembla antes de embestir */
                    bvx = rnd(-256, 256);
                    bvy = rnd(-256, 256);
                    if (--btimer <= 0) {
                        bstate = 2;
                        btimer = 22;
                        aim(bpx + 24, bpy + 24, px + 7, py + 9, FX(5) + (bmax - bhp) * 16, &bvx, &bvy);
                    }
                    break;
                default: /* embestida */
                    if (--btimer <= 0) { bstate = 0; btimer = rnd(SEC(1), SEC(2)); }
                    break;
            }
            bx += bvx;
            by += bvy;
            bx = clampi(bx, FX(-8), FX(SCR_W - 40));
            by = clampi(by, FX(HUD_H - 48), FX(SCR_H - 40));
            bpx = PX(bx);
            bpy = PX(by);

            if (atk && !bhit && overlap(ax, ay, aw, ah, bpx + 6, bpy + 6, 36, 36)) {
                bhit = 10;
                if (--bhp <= 0) return RES_WIN;
            }
            if (!inv && overlap(px + 2, py + 2, 10, 14, bpx + 6, bpy + 8, 36, 36)) {
                hearts--;
                inv = IFRAMES;
            }
        }

        if (inv) inv--;
        if (hearts <= 0) return RES_LOSE;

        /* dibujo */
        gfx_FillScreen(C_FLOOR);
        gfx_SetColor(C_DBROWN);
        for (i = HUD_H + 24; i < SCR_H; i += 32) gfx_HorizLine(0, i, SCR_W);
        for (i = 0; i < REV_MAX; i++) draw_obj_wawa16(&w[i]);
        if (boss_on && !(bhit & 2))
            gfx_TransparentSprite(spr_petro, PX(bx), PX(by));
        if (!(inv & 2))
            gfx_TransparentSprite(spr_player, px, py);
        if (atk) {
            gfx_SetColor(C_WHITE);
            switch (facing) {
                case 0: gfx_Line(px + 16, py - 2, px + 30, py + 18); gfx_Line(px + 17, py - 2, px + 31, py + 18); break;
                case 1: gfx_Line(px - 2, py - 2, px - 16, py + 18); gfx_Line(px - 3, py - 2, px - 17, py + 18); break;
                case 2: gfx_Line(px - 4, py - 4, px + 18, py - 14); gfx_Line(px - 4, py - 5, px + 18, py - 15); break;
                default: gfx_Line(px - 4, py + 30, px + 18, py + 20); gfx_Line(px - 4, py + 31, px + 18, py + 21); break;
            }
        }
        if (boss_on) {
            hud_draw(NULL, 0, -1, 0, 0, hearts, 5);
            text_at("PETROVICH", 4, 4, C_RED);
            gfx_SetColor(C_BLACK);
            gfx_FillRectangle(82, 4, 64, 8);
            gfx_SetColor(C_RED);
            gfx_FillRectangle(83, 5, bhp * 62 / bmax, 6);
        } else {
            hud_draw("wawas: ", killed, total, 0, 0, hearts, 5);
        }
        if (inv > IFRAMES - 4) hud_flash_damage();
        frame_end();
    }
}

result_t mg_revenge1(void) { return revenge(10, 1, false); }
result_t mg_revenge2(void) { return revenge(18, 6, true); }

/* ======================================================================
 * 7. dont touch the red wawas!!!  -  Llega a la bandera sin tocar los rojos
 * ====================================================================== */
result_t mg_redwawas(void) {
    obj_t r[9];
    const int sx = 6, sy = 214;          /* salida */
    const int fx = 292, fy = HUD_H + 6;  /* bandera */
    int px = sx, py = sy, hearts = 3, inv = 0, i;
    int t = SEC(30);

    for (i = 0; i < 9; i++) {
        r[i].type = W_RED;
        r[i].alive = true;
        r[i].dead = 0;
        if (i < 5) {   /* filas horizontales */
            r[i].x = FX(rnd(20, 280));
            r[i].y = FX(HUD_H + 26 + i * 38);
            r[i].vx = rnd_vel(384, 768);
            r[i].vy = 0;
        } else {       /* columnas verticales */
            r[i].x = FX(50 + (i - 5) * 66);
            r[i].y = FX(rnd(HUD_H, 200));
            r[i].vx = 0;
            r[i].vy = rnd_vel(384, 640);
        }
    }

    for (;;) {
        input_read();
        if (in.quit_p) return RES_QUIT;
        move_player(&px, &py, 3, 14, 18, HUD_H);

        for (i = 0; i < 9; i++) {
            bounce(&r[i], 16, 16, HUD_H);
            if (!inv && overlap(px + 2, py + 2, 10, 14, PX(r[i].x) + 2, PX(r[i].y) + 2, 12, 12)) {
                hearts--;
                inv = IFRAMES;
                px = sx;
                py = sy;
            }
        }
        if (inv) inv--;
        if (hearts <= 0) return RES_LOSE;
        if (overlap(px, py, 14, 18, fx, fy, 20, 24)) return RES_WIN;
        if (--t <= 0) return RES_LOSE;

        gfx_FillScreen(C_GREEN);
        gfx_SetColor(C_DGREEN);
        gfx_FillRectangle(0, 206, 30, 34);   /* zona de salida */
        /* bandera */
        gfx_SetColor(C_DGREY);
        gfx_VertLine(fx + 2, fy, 24);
        gfx_VertLine(fx + 3, fy, 24);
        gfx_SetColor(C_YELLOW);
        gfx_FillTriangle(fx + 4, fy, fx + 4, fy + 10, fx + 18, fy + 5);
        for (i = 0; i < 9; i++) draw_obj_wawa16(&r[i]);
        if (!(inv & 2)) gfx_TransparentSprite(spr_player, px, py);
        hud_draw("ve a la bandera", 0, -1, t, SEC(30), hearts, 3);
        if (inv > IFRAMES - 4) hud_flash_damage();
        frame_end();
    }
}

/* ======================================================================
 * 8. survive the dogs for 30 seconds  -  Esquiva a los perros
 * ====================================================================== */
result_t mg_dogs(void) {
    obj_t d[6];
    int px = 153, py = 120, hearts = 3, inv = 0, i, ndogs = 0, spawn = 0;
    int t = SEC(30);

    for (i = 0; i < 6; i++) d[i].alive = false;

    for (;;) {
        input_read();
        if (in.quit_p) return RES_QUIT;
        move_player(&px, &py, 3, 14, 18, HUD_H);

        if (ndogs < 6 && --spawn <= 0) {
            d[ndogs].alive = true;
            d[ndogs].x = FX(rnd(0, 1) ? -24 : SCR_W);
            d[ndogs].y = FX(rnd(HUD_H, SCR_H - 16));
            d[ndogs].timer = 0;
            ndogs++;
            spawn = ndogs < 2 ? SEC(1) : SEC(5);
        }

        for (i = 0; i < ndogs; i++) {
            int dx = PX(d[i].x), dy = PX(d[i].y);
            if (--d[i].timer <= 0) {
                /* vuelven a apuntar cada poco, con algo de error */
                aim(dx + 12, dy + 8, px + 7 + rnd(-20, 20), py + 9 + rnd(-20, 20),
                    230 + i * 20, &d[i].vx, &d[i].vy);
                d[i].timer = rnd(8, 16);
            }
            d[i].x += d[i].vx;
            d[i].y += d[i].vy;
            if (!inv && overlap(px + 2, py + 2, 10, 14, PX(d[i].x) + 3, PX(d[i].y) + 3, 18, 11)) {
                hearts--;
                inv = IFRAMES;
            }
        }
        if (inv) inv--;
        if (hearts <= 0) return RES_LOSE;
        if (--t <= 0) return RES_WIN;   /* ¡sobreviviste! */

        gfx_FillScreen(C_GREEN);
        gfx_SetColor(C_DGREEN);
        for (i = 0; i < 12; i++) gfx_FillCircle((i * 97) % 310 + 5, (i * 53) % 210 + 25, 3);
        for (i = 0; i < ndogs; i++)
            gfx_TransparentSprite(d[i].vx >= 0 ? spr_dog_r : spr_dog_l, PX(d[i].x), PX(d[i].y));
        if (!(inv & 2)) gfx_TransparentSprite(spr_player, px, py);
        hud_draw("aguanta!", 0, -1, t, SEC(30), hearts, 3);
        if (inv > IFRAMES - 4) hud_flash_damage();
        frame_end();
    }
}

/* ======================================================================
 * 9. crush th wawas before they get to the end!!!!  -  Baja la prensa
 * ====================================================================== */
result_t mg_crush(void) {
    obj_t w[8];
    const int floor_y = 200, cx = 140, cw = 44, ch = 40;
    const int goal = 15;
    int cy = HUD_H + 8;     /* parte de arriba de la prensa */
    int hearts = 3, crushed = 0, spawn = 10, i, flash = 0;

    for (i = 0; i < 8; i++) { w[i].alive = false; w[i].dead = 0; }

    for (;;) {
        input_read();
        if (in.quit_p) return RES_QUIT;
        if (in.down) cy += 7;
        if (in.up) cy -= 4;
        cy = clampi(cy, HUD_H + 4, floor_y - ch);

        if (--spawn <= 0) {
            for (i = 0; i < 8; i++) {
                if (!w[i].alive && !w[i].dead) {
                    w[i].alive = true;
                    w[i].type = W_NORMAL;
                    w[i].x = FX(-16);
                    w[i].y = FX(floor_y - 16);
                    w[i].vx = rnd(256, 520);
                    break;
                }
            }
            spawn = rnd(30, 55);
        }

        for (i = 0; i < 8; i++) {
            int wx;
            if (!w[i].alive) continue;
            w[i].x += w[i].vx;
            wx = PX(w[i].x);
            if (cy + ch > floor_y - 12 && overlap(cx, cy, cw, ch, wx + 2, floor_y - 16, 12, 16)) {
                kill_obj(&w[i]);
                crushed++;
            } else if (wx > SCR_W) {
                w[i].alive = false;
                hearts--;
                flash = 6;
            }
        }
        tick_dead(w, 8);
        if (flash) flash--;
        if (hearts <= 0) return RES_LOSE;
        if (crushed >= goal) return RES_WIN;

        gfx_FillScreen(C_LGREY);
        gfx_SetColor(C_DGREY);
        gfx_FillRectangle(0, floor_y, SCR_W, SCR_H - floor_y);
        gfx_SetColor(C_RED);
        gfx_FillRectangle(SCR_W - 6, floor_y - 40, 6, 40);   /* meta */
        for (i = 0; i < 8; i++) draw_obj_wawa16(&w[i]);
        /* prensa */
        gfx_SetColor(C_GREY);
        gfx_FillRectangle(cx + cw / 2 - 4, HUD_H, 8, cy - HUD_H);
        gfx_SetColor(C_BLACK);
        gfx_FillRectangle(cx, cy, cw, ch);
        gfx_SetColor(C_GREY);
        gfx_FillRectangle(cx + 2, cy + 2, cw - 4, ch - 4);
        gfx_SetColor(C_YELLOW);
        for (i = 0; i < cw - 4; i += 8)
            gfx_FillTriangle(cx + 2 + i, cy + ch - 6, cx + 6 + i, cy + ch - 2, cx + 2 + i, cy + ch - 2);
        hud_draw("aplastados: ", crushed, goal, 0, 0, hearts, 3);
        if (flash) hud_flash_damage();
        frame_end();
    }
}

/* ======================================================================
 * 10. gameboy  -  Plataformas en verde Game Boy; toca a todos los wawas
 * ====================================================================== */
typedef struct { int16_t x, y, w; } plat_t;
static const plat_t plats[] = {
    {0, 224, 320},     /* suelo */
    {30, 186, 70},
    {130, 160, 60},
    {220, 186, 80},
    {10, 128, 70},
    {240, 124, 70},
    {110, 98, 100},
    {20, 70, 60},
    {250, 66, 60},
};
#define NPLATS (int)(sizeof plats / sizeof plats[0])

result_t mg_gameboy(void) {
    obj_t w[6];
    static const int16_t wpos[6][2] = {
        {50, 170}, {150, 144}, {270, 108}, {30, 54}, {270, 50}, {150, 82},
    };
    int x = FX(150), y = FX(200), vy = 0, i, left = 6;
    bool ground = false;
    int t = SEC(30);

    for (i = 0; i < 6; i++) {
        w[i].x = FX(wpos[i][0]);
        w[i].y = FX(wpos[i][1]);
        w[i].type = W_GB;
        w[i].alive = true;
        w[i].dead = 0;
    }

    for (;;) {
        int oldy = PX(y);
        input_read();
        if (in.quit_p) return RES_QUIT;
        if (in.left) x -= FX(3);
        if (in.right) x += FX(3);
        x = clampi(x, 0, FX(SCR_W - 14));
        if ((in.act_p || in.up_p) && ground) vy = -FX(6) - 64;
        vy += 90;
        if (vy > FX(7)) vy = FX(7);
        y += vy;

        /* aterrizar en plataformas (solo cayendo) */
        ground = false;
        if (vy >= 0) {
            for (i = 0; i < NPLATS; i++) {
                int feet_old = oldy + 18, feet = PX(y) + 18;
                if (PX(x) + 12 > plats[i].x && PX(x) + 2 < plats[i].x + plats[i].w &&
                    feet_old <= plats[i].y && feet >= plats[i].y) {
                    y = FX(plats[i].y - 18);
                    vy = 0;
                    ground = true;
                }
            }
        }
        if (PX(y) < HUD_H) { y = FX(HUD_H); vy = 0; }

        for (i = 0; i < 6; i++) {
            if (w[i].alive && overlap(PX(x) + 1, PX(y) + 1, 12, 16, PX(w[i].x) + 1, PX(w[i].y), 14, 16)) {
                kill_obj(&w[i]);
                left--;
            }
        }
        tick_dead(w, 6);
        if (left == 0) return RES_WIN;
        if (--t <= 0) return RES_LOSE;

        gfx_FillScreen(C_GB3);
        gfx_SetColor(C_GB2);
        for (i = 0; i < NPLATS; i++) gfx_FillRectangle(plats[i].x, plats[i].y + 3, plats[i].w, 5);
        gfx_SetColor(C_GB1);
        for (i = 0; i < NPLATS; i++) gfx_FillRectangle(plats[i].x, plats[i].y, plats[i].w, 3);
        for (i = 0; i < 6; i++) {
            if (w[i].alive) gfx_TransparentSprite(spr_wawa16[W_GB], PX(w[i].x), PX(w[i].y));
            else if (w[i].dead & 2) {
                gfx_SetColor(C_GB0);
                gfx_FillRectangle(PX(w[i].x) + 1, PX(w[i].y) + 11, 14, 4);
            }
        }
        /* jugador en tonos Game Boy */
        gfx_SetColor(C_GB0);
        gfx_FillRectangle(PX(x) + 2, PX(y), 10, 18);
        gfx_SetColor(C_GB2);
        gfx_FillRectangle(PX(x) + 4, PX(y) + 3, 6, 5);
        gfx_SetColor(C_GB0);
        gfx_SetPixel(PX(x) + 5, PX(y) + 5);
        gfx_SetPixel(PX(x) + 8, PX(y) + 5);
        /* HUD en verde */
        gfx_SetColor(C_GB0);
        gfx_FillRectangle(0, 0, SCR_W, HUD_H);
        gfx_SetTextFGColor(C_GB3);
        gfx_SetTextXY(4, 4);
        gfx_PrintString("GAMEBOY  wawas: ");
        gfx_PrintInt(6 - left, 1);
        gfx_PrintString("/6");
        gfx_SetColor(C_GB1);
        gfx_FillRectangle(230, 4, 62, 8);
        gfx_SetColor(C_GB3);
        gfx_FillRectangle(231, 5, t * 60 / SEC(30), 6);
        gfx_SetTextXY(296, 4);
        gfx_PrintInt((t + FPS - 1) / FPS, 2);
        frame_end();
    }
}

/* ======================================================================
 * 11. rob the wawa  -  Roba el wawa del pedestal y vuelve a la puerta
 * ====================================================================== */
result_t mg_rob(void) {
    obj_t b[6];
    const int door_x = 4, ped_x = 280;
    const int top = HUD_H + 10, bot = SCR_H - 10;
    const int lx[2] = {100, 196};
    int px = 10, py = 120, hearts = 3, inv = 0, i, frame = 0, shoot = 0;
    bool have = false;

    for (i = 0; i < 6; i++) b[i].alive = false;

    for (;;) {
        bool light_on[2];
        input_read();
        if (in.quit_p) return RES_QUIT;
        move_player(&px, &py, 3, 14, 18, top);
        if (py > bot - 18) py = bot - 18;
        frame++;

        /* las luces se encienden y apagan */
        light_on[0] = !have && ((frame / 40) % 2 == 0);
        light_on[1] = !have && (((frame + 25) / 35) % 2 == 0);

        for (i = 0; i < 2; i++) {
            if (light_on[i] && !inv && overlap(px + 2, py, 10, 18, lx[i], top, 16, bot - top)) {
                hearts--;
                inv = IFRAMES;
                px = 10;
                py = 120;
            }
        }

        if (!have && overlap(px, py, 14, 18, ped_x, 104, 24, 30)) have = true;

        /* con el wawa robado, el cañón dispara cada segundo */
        if (have && ++shoot >= FPS) {
            shoot = 0;
            for (i = 0; i < 6; i++) {
                if (!b[i].alive) {
                    b[i].alive = true;
                    b[i].x = FX(SCR_W - 20);
                    b[i].y = FX(top + 10);
                    aim(SCR_W - 20, top + 10, px + 7, py + 9, FX(4), &b[i].vx, &b[i].vy);
                    break;
                }
            }
        }
        for (i = 0; i < 6; i++) {
            if (!b[i].alive) continue;
            b[i].x += b[i].vx;
            b[i].y += b[i].vy;
            if (b[i].y < FX(top) || b[i].y > FX(bot - 4)) b[i].vy = -b[i].vy;  /* rebotan */
            if (b[i].x < FX(-6)) b[i].alive = false;
            if (!inv && overlap(px + 2, py + 2, 10, 14, PX(b[i].x) - 3, PX(b[i].y) - 3, 6, 6)) {
                hearts--;
                inv = IFRAMES;
                b[i].alive = false;
            }
        }
        if (inv) inv--;
        if (hearts <= 0) return RES_LOSE;
        if (have && px < door_x + 16) return RES_WIN;

        gfx_FillScreen(C_DGREY);
        gfx_SetColor(C_DBLUE);
        gfx_FillRectangle(0, top, SCR_W, bot - top);
        /* puerta */
        gfx_SetColor(C_BROWN);
        gfx_FillRectangle(door_x - 4, 100, 12, 40);
        gfx_SetColor(C_YELLOW);
        gfx_SetPixel(door_x + 4, 120);
        /* pedestal */
        gfx_SetColor(C_LGREY);
        gfx_FillRectangle(ped_x, 122, 24, 14);
        if (!have) gfx_TransparentSprite(spr_wawa16[W_NORMAL], ped_x + 4, 106);
        /* luces */
        for (i = 0; i < 2; i++) {
            gfx_SetColor(light_on[i] ? C_YELLOW : C_DGREY);
            gfx_FillRectangle(lx[i] - 4, top, 24, 6);
            if (light_on[i]) {
                gfx_SetColor(C_ORANGE);
                gfx_FillRectangle(lx[i], top + 6, 16, bot - top - 6);
            }
        }
        /* cañón */
        if (have) {
            gfx_SetColor(C_BLACK);
            gfx_FillRectangle(SCR_W - 20, top, 20, 16);
            gfx_SetColor(C_RED);
            for (i = 0; i < 6; i++)
                if (b[i].alive) gfx_FillCircle(PX(b[i].x), PX(b[i].y), 3);
        }
        if (!(inv & 2)) {
            gfx_TransparentSprite(spr_player, px, py);
            if (have) gfx_TransparentSprite(spr_wawa16[W_NORMAL], px - 2, py - 14);
        }
        hud_draw(have ? "vuelve a la puerta" : "roba el wawa", 0, -1, 0, 0, hearts, 3);
        if (inv > IFRAMES - 4) hud_flash_damage();
        frame_end();
    }
}

/* ====================================================================== */
const minigame_t minigames[NUM_STAGES] = {
    {"click the wawas", "click the wawas!!", "Cursor y [2nd]: 35 wawas", mg_click},
    {"spike the wawas", "spike the wawas", "Toca todos los wawas", mg_spike},
    {"shoot the wawas", "shoot the wawa", "Apunta y dispara con [2nd]", mg_shoot},
    {"we're in space!", "we r in space!!!", "Flota y toca los wawas", mg_space},
    {"catch the wawas", "catch the wawas!!!", "Atrapa 15 wawas con la cesta", mg_catch},
    {"revenge part one", "theyre taking revenge....", "Golpea con [2nd]", mg_revenge1},
    {"don't touch the red", "dont touch the red wawas!!!", "Llega a la bandera", mg_redwawas},
    {"survive the dogs", "survive the dogs for 30 seconds", "Esquiva a los perros", mg_dogs},
    {"crush the wawas", "crush th wawas before they get to the end!!!!", "Baja la prensa con abajo", mg_crush},
    {"gameboy", "gameboy", "Salta con [2nd] o arriba", mg_gameboy},
    {"rob the wawa", "rob the wawa", "Esquiva luces y balas", mg_rob},
    {"revenge part two", "the wawas are taking revenge again!?\?!?!", "Y al final... Petrovich", mg_revenge2},
};
