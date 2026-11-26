/*
 * click the wawas - port para TI-84 Plus CE (modos clásico y endless)
 *
 * Juego original: "click the wawas" de aidenkp11 (Scratch / TurboWarp,
 * Game Jolt). Este es un port no oficial hecho por fans, con el permiso
 * que el autor da para hacer remixes y ports.
 */
#include "wawas.h"

#include <fileioc.h>
#include <keypadc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/rtc.h>

/* ---------------- Texto con salto de línea ---------------- */
static void draw_wrapped(const char *s, int y, int maxw, uint8_t scale, uint8_t color) {
    char line[48];
    char word[48];
    int ll = 0;

    gfx_SetTextScale(scale, scale);
    line[0] = '\0';
    while (*s) {
        int wl = 0;
        while (*s == ' ') s++;
        while (*s && *s != ' ' && wl < 47) word[wl++] = *s++;
        word[wl] = '\0';
        if (!wl) break;

        if (ll) {
            char test[96];
            strcpy(test, line);
            strcat(test, " ");
            strcat(test, word);
            if ((int)gfx_GetStringWidth(test) > maxw) {
                text_center(line, y, scale, color);
                gfx_SetTextScale(scale, scale);
                y += 10 * scale;
                strcpy(line, word);
            } else {
                strcpy(line, test);
            }
        } else {
            strcpy(line, word);
        }
        ll = (int)strlen(line);
    }
    if (ll) text_center(line, y, scale, color);
    gfx_SetTextScale(1, 1);
}

/* Texto centrado (desplazado dx) con sombra negra */
static void shadow_text(const char *s, int dx, int y, uint8_t scale, uint8_t color) {
    int x;
    gfx_SetTextScale(scale, scale);
    x = (SCR_W - (int)gfx_GetStringWidth(s)) / 2 + dx;
    gfx_SetTextFGColor(C_BLACK);
    gfx_PrintStringXY(s, x + scale, y + scale);
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(s, x, y);
    gfx_SetTextScale(1, 1);
}

/* ---------------- Aviso antes de cada minijuego ---------------- */
/* Número en texto (sin sprintf) */
static void num_str(char *buf, int n) {
    char tmp[8];
    int i = 0, j = 0;
    if (n == 0) tmp[i++] = '0';
    while (n > 0 && i < 7) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* header: "STAGE 3 / 12" o "RONDA 5"; lives >= 0 dibuja las vidas (endless) */
static bool prompt(int idx, const char *header, int lives) {
    int t = 0;
    wait_release();
    for (;;) {
        input_read();
        if (in.quit_p) return false;
        if (in.ok_p && t > 8) return true;
        t++;

        gfx_FillScreen(C_BLACK);
        gfx_SetColor(C_GREY);
        gfx_FillRectangle(24, 40, 272, 164);
        gfx_SetColor(C_LGREY);
        gfx_Rectangle(24, 40, 272, 164);

        text_center(header, 48, 1, C_YELLOW);
        if (lives >= 0) {
            int i;
            for (i = 0; i < 3; i++) draw_heart(250 + i * 14, 46, i < lives);
        }

        draw_wrapped(minigames[idx].prompt, 70, 256, 2, C_WHITE);
        text_center(minigames[idx].hint, 150, 1, C_LGREY);

        /* botón OK que late */
        gfx_SetColor((t / 8) & 1 ? C_WHITE : C_LGREY);
        gfx_FillRectangle(130, 170, 60, 22);
        gfx_SetColor(C_BLACK);
        gfx_Rectangle(130, 170, 60, 22);
        text_center("OK", 174, 2, C_BLACK);

        text_center("[2nd]/[enter] empezar   [clear] salir", 222, 1, C_GREY);
        frame_end();
    }
}

/* ---------------- Game over (estática roja) ---------------- */
/* score < 0: modo clásico (se puede reintentar). Si no, fin del endless. */
static bool game_over(int score, int best) {
    int t = 0, i;
    wait_release();
    for (;;) {
        input_read();
        t++;
        if (t > SEC(1)) {
            if (in.ok_p) return score < 0;
            if (in.quit_p) return false;
        }

        gfx_FillScreen(C_BLACK);
        for (i = 0; i < 70; i++) {
            gfx_SetColor(rnd(0, 2) ? C_DRED : C_RED);
            gfx_HorizLine(rnd(0, 300), rnd(0, SCR_H - 1), rnd(4, 60));
        }
        {
            int dx = rnd(-5, 5);
            shadow_text("GAME OVER", dx, 100, 4, C_RED);
        }
        if (score >= 0) {
            char buf[32], n[8];
            strcpy(buf, "puntos: ");
            num_str(n, score);
            strcat(buf, n);
            strcat(buf, "   record: ");
            num_str(n, best);
            strcat(buf, n);
            text_center(buf, 150, 1, C_WHITE);
            if (score >= best && score > 0) text_center("nuevo record!", 166, 1, C_YELLOW);
        }
        if (t > SEC(1))
            text_center(score >= 0 ? "[enter] menu" : "[enter] reintentar    [clear] menu", 200, 1, C_WHITE);
        frame_end();
    }
}

/* ---------------- Minijuego superado ---------------- */
static void stage_clear(void) {
    int t;
    for (t = 0; t < 30; t++) {
        input_read();
        gfx_FillScreen(C_DGREEN);
        text_center("superado!", 70, 3, C_WHITE);
        gfx_TransparentSprite(spr_wawa24[W_NORMAL], 148, 130 - ((t / 3) & 1) * 6);
        frame_end();
    }
}

/* ---------------- Final del modo clásico ---------------- */
static void ending(void) {
    static int16_t cact[40][2];
    int ncact = 0, t, i;

    wait_release();
    /* "Congratulations, you won!" */
    for (t = 0; t < SEC(4); t++) {
        int hop = (t / 4) & 1;
        input_read();
        gfx_FillScreen(C_SKY);
        gfx_SetColor(C_GREEN);
        gfx_FillRectangle(0, 180, SCR_W, 60);
        text_center("Congratulations,", 30, 2, C_WHITE);
        text_center("you won!", 54, 3, C_YELLOW);
        for (i = 0; i < 6; i++)
            gfx_TransparentSprite(spr_wawa24[i == 5 ? W_GB : W_NORMAL], 20 + i * 48, 120 - ((i + t / 4) & 1) * 4);
        gfx_TransparentSprite(spr_dog_r, 40, 160 - hop * 3);
        gfx_TransparentSprite(spr_dog_l, 256, 160 - (1 - hop) * 3);
        gfx_TransparentSprite(spr_wawa16[W_GB], 150, 160);
        frame_end();
    }

    /* ...y de repente, un cactus (y luego muchos más) */
    for (t = 0; t < SEC(3); t++) {
        int sx = rnd(-4, 4), sy = rnd(-4, 4);
        input_read();
        if (t % 3 == 0 && ncact < 40) {
            cact[ncact][0] = (int16_t)(ncact == 0 ? 152 : rnd(0, SCR_W - 16));
            cact[ncact][1] = (int16_t)(ncact == 0 ? 150 : rnd(0, SCR_H - 24));
            ncact++;
        }
        gfx_FillScreen(t < 3 ? C_WHITE : C_SKY);
        gfx_SetColor(C_GREEN);
        gfx_FillRectangle(0, 180 + sy, SCR_W, 60);
        for (i = 0; i < 5; i++)
            gfx_TransparentSprite(spr_wawa24[W_NORMAL], 20 + i * 48 + sx, 120 + sy);
        for (i = 0; i < ncact; i++)
            gfx_TransparentSprite(spr_cactus, cact[i][0] + sx, cact[i][1] + sy);
        /* cristal roto */
        gfx_SetColor(C_BLACK);
        gfx_Line(160, 120, 20, 10);
        gfx_Line(160, 120, 300, 30);
        gfx_Line(160, 120, 40, 230);
        gfx_Line(160, 120, 290, 220);
        gfx_Line(160, 120, 170, 0);
        gfx_Line(160, 120, 0, 140);
        gfx_Circle(160, 120, 18);
        frame_end();
    }
}

/* ---------------- Modo clásico ---------------- */
/* Devuelve true si se superó, false si el jugador volvió al menú */
static bool run_stage(int stage) {
    char header[16], n[8];
    strcpy(header, "STAGE ");
    num_str(n, stage + 1);
    strcat(header, n);
    strcat(header, " / 12");
    for (;;) {
        result_t r;
        if (!prompt(stage, header, -1)) return false;
        wait_release();
        r = minigames[stage].play();
        if (r == RES_QUIT) return false;
        if (r == RES_WIN) {
            stage_clear();
            return true;
        }
        if (!game_over(-1, 0)) return false;
    }
}

static void classic_mode(void) {
    int s;
    g_endless_round = 0;
    for (s = 0; s < NUM_STAGES; s++)
        if (!run_stage(s)) return;
    ending();
}

/* ---------------- Modo endless ---------------- */
/* Minijuegos al azar hasta perder las 3 vidas. Cada ronda hay menos tiempo.
 * "revenge part two" no sale, igual que en el original. */
static const char save_name[] = "WAWASAV";

static int load_best(void) {
    int best = 0;
    uint8_t h = ti_Open(save_name, "r");
    if (h) {
        ti_Read(&best, sizeof best, 1, h);
        ti_Close(h);
    }
    return best;
}

static void save_best(int best) {
    uint8_t h = ti_Open(save_name, "w");
    if (h) {
        ti_Write(&best, sizeof best, 1, h);
        ti_SetArchiveStatus(true, h);
        ti_Close(h);
    }
}

static void lost_life(int lives) {
    int t, i;
    for (t = 0; t < 40; t++) {
        input_read();
        gfx_FillScreen(C_DRED);
        text_center("fallaste!", 80, 3, C_WHITE);
        for (i = 0; i < 3; i++)
            draw_heart(136 + i * 18, 130, i < lives || (i == lives && (t & 4)));
        frame_end();
    }
}

static void endless_mode(void) {
    int lives = 3, score = 0, round = 0, last = -1, idx, best;
    char header[16], n[8];

    for (;;) {
        result_t r;
        round++;
        g_endless_round = round;
        do {
            idx = rnd(0, NUM_STAGES - 2);
        } while (idx == last);
        last = idx;

        strcpy(header, "RONDA ");
        num_str(n, round);
        strcat(header, n);
        if (!prompt(idx, header, lives)) break;
        wait_release();
        r = minigames[idx].play();
        if (r == RES_QUIT) break;
        if (r == RES_WIN) {
            score++;
            stage_clear();
            continue;
        }
        lives--;
        if (lives > 0) {
            lost_life(lives);
            continue;
        }
        best = load_best();
        if (score > best) {
            best = score;
            save_best(best);
        }
        game_over(score, best);
        break;
    }
    g_endless_round = 0;
}

/* ---------------- Créditos ---------------- */
static void credits(void) {
    int t = 0;
    wait_release();
    for (;;) {
        input_read();
        if (t++ > 10 && (in.ok_p || in.quit_p)) return;

        gfx_FillScreen(C_BLACK);
        text_center("CREDITOS", 8, 2, C_YELLOW);
        text_center("Juego original:", 36, 1, C_LGREY);
        text_center("click the wawas", 50, 2, C_WHITE);
        text_center("por aidenkp11", 72, 2, C_WHITE);
        text_center("(Scratch / TurboWarp / Game Jolt)", 94, 1, C_LGREY);
        text_center("Port para TI-84 Plus CE:", 120, 1, C_LGREY);
        text_center("Ruben y Claude", 134, 2, C_WHITE);
        text_center("Port no oficial hecho por fans,", 166, 1, C_LGREY);
        text_center("con el permiso del autor para", 178, 1, C_LGREY);
        text_center("hacer remixes y ports.", 190, 1, C_LGREY);
        gfx_TransparentSprite(spr_wawa24[W_NORMAL], 20, 200);
        gfx_TransparentSprite(spr_wawa24[W_NORMAL], 276, 200);
        text_center("[clear] volver", 226, 1, C_GREY);
        frame_end();
    }
}

/* ---------------- Pantalla de título ---------------- */
#define TITLE_WAWAS 8
static const char *main_items[] = {"JUGAR", "CREDITOS", "SALIR"};
static const char *play_items[] = {"MODO CLASICO", "MODO ENDLESS", "VOLVER"};
static const char *play_desc[] = {
    "El modo original. 12 stages.",
    "Aguanta todo lo que puedas!",
    "",
};

int main(void) {
    int bx[TITLE_WAWAS], by[TITLE_WAWAS], bvx[TITLE_WAWAS], bvy[TITLE_WAWAS];
    int sel = 0, i, t = 0, boing = 0;
    bool play_menu = false;

    srand((unsigned)rtc_Time());
    gfx_Begin();
    gfx_setup();

    for (i = 0; i < TITLE_WAWAS; i++) {
        bx[i] = rnd(0, SCR_W - 16) << 4;
        by[i] = rnd(0, SCR_H - 16) << 4;
        bvx[i] = rnd(0, 1) ? rnd(8, 24) : -rnd(8, 24);
        bvy[i] = rnd(0, 1) ? rnd(8, 24) : -rnd(8, 24);
    }

    wait_release();
    for (;;) {
        const char **items = play_menu ? play_items : main_items;
        int nitems = 3;

        input_read();
        if (in.quit_p) {
            if (!play_menu) break;
            play_menu = false;
            sel = 0;
        }
        if (in.up_p) { sel = (sel + nitems - 1) % nitems; boing = 6; }
        if (in.down_p) { sel = (sel + 1) % nitems; boing = 6; }
        if (in.ok_p) {
            if (play_menu) {
                if (sel == 0) classic_mode();
                else if (sel == 1) endless_mode();
                else { play_menu = false; sel = 0; }
            } else {
                if (sel == 0) { play_menu = true; sel = 0; }
                else if (sel == 1) credits();
                else break;
            }
            wait_release();
        }
        t++;
        if (boing) boing--;

        for (i = 0; i < TITLE_WAWAS; i++) {
            bx[i] += bvx[i];
            by[i] += bvy[i];
            if (bx[i] < 0 || bx[i] > (SCR_W - 16) << 4) bvx[i] = -bvx[i];
            if (by[i] < 0 || by[i] > (SCR_H - 16) << 4) bvy[i] = -bvy[i];
        }

        gfx_FillScreen(C_WHITE);
        for (i = 0; i < TITLE_WAWAS; i++)
            gfx_TransparentSprite(spr_wawa16[W_NORMAL], bx[i] >> 4, by[i] >> 4);

        shadow_text("click the", 0, 8, 3, C_BLUE);
        shadow_text("wawas", 0, 36, 4, C_YELLOW);

        gfx_TransparentSprite(spr_wawa_big, 128, 72 - (boing ? 4 : 0) + ((t / 15) & 1));

        gfx_SetColor(C_BLACK);
        gfx_FillRectangle(70, 140, 180, 58);
        gfx_SetColor(C_GREY);
        gfx_Rectangle(70, 140, 180, 58);
        for (i = 0; i < nitems; i++) {
            int y = 147 + i * 17;
            if (i == sel) {
                gfx_SetColor(C_DGREY);
                gfx_FillRectangle(72, y - 3, 176, 14);
                text_at(">", 78, y, C_YELLOW);
            }
            text_center(items[i], y, 1, i == sel ? C_YELLOW : C_WHITE);
        }
        if (play_menu) text_center(play_desc[sel], 206, 1, C_BLACK);
        text_center("original: aidenkp11 - port fan", 226, 1, C_GREY);
        frame_end();
    }

    gfx_End();
    return 0;
}
