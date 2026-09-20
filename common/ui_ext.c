/* ui_ext.c - see ui_ext.h. Public render.h primitives only, on purpose. */
#include "ui_ext.h"
#include "render.h"
#include "progression.h"
#include "tank_events.h"
#include "icons.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define INK    0x031015
#define PANEL  0x04141a
#define INNER  0x1c2f36
#define DIM    0x2a3f45
#define TEAL   0x9fd8e2
#define FAINT  0x3f6a72
#define WHITE  0xffffff

/* ---- the battery bolt ------------------------------------------------- */
/* 9 x 18, drawn as one horizontal run per row so it needs only render_rect */
#define BOLT_W 9
#define BOLT_H 18
static const char *const BOLT[BOLT_H] = {
    "....#####",
    "...#####.",
    "..#####..",
    "..####...",
    ".#####...",
    ".####....",
    "#####....",
    "#########",
    "########.",
    "....####.",
    "...####..",
    "...###...",
    "..####...",
    "..###....",
    ".###.....",
    ".##......",
    "##.......",
    "#........",
};
#define BOLT_X (TANK_W - BOLT_W - 30)        /* inside the panel's curved bezel, where the pill sits */
#define BOLT_Y 6

int ui_battery_level(float frac) {
    if (frac > 0.75f) return 3;
    if (frac > 0.50f) return 2;
    if (frac > 0.25f) return 1;
    return 0;
}

/* one pass over the glyph; `inset` shifts it, `alpha` < 255 blends (the halo) */
static void bolt_pass(uint16_t *fb, int stride, int ox, int oy, uint32_t rgb, int alpha) {
    for (int r = 0; r < BOLT_H; r++) {
        const char *row = BOLT[r];
        int c = 0;
        while (c < BOLT_W) {
            if (row[c] != '#') { c++; continue; }
            int run = 0;
            while (c + run < BOLT_W && row[c + run] == '#') run++;
            if (alpha >= 255) render_rect(fb, stride, ox + c, oy + r, run, 1, rgb);
            else              render_rect_blend(fb, stride, ox + c, oy + r, run, 1, rgb, alpha);
            c += run;
        }
    }
}

void ui_battery_bolt(uint16_t *fb, int stride, float frac, bool charging, float clock) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    static const uint32_t COL[4] = { 0xf25b65, 0xff9a3c, 0xffe14a, 0x78d67d };   /* red, orange, yellow, green */
    int lvl = ui_battery_level(frac);
    uint32_t rgb = charging ? 0x38dcc7 : COL[lvl];
    /* the emptier it is the faster it breathes: a red bolt is urgent, a green
       one barely moves. Charging gets its own steady, slower pulse. */
    float rate = charging ? 1.4f : 1.1f + (3 - lvl) * 0.9f;
    float pulse = 0.68f + 0.32f * sinf(clock * rate);
    int halo = (int)(52 * pulse);                     /* the glow, four offsets around the glyph */
    bolt_pass(fb, stride, BOLT_X - 1, BOLT_Y,     rgb, halo);
    bolt_pass(fb, stride, BOLT_X + 1, BOLT_Y,     rgb, halo);
    bolt_pass(fb, stride, BOLT_X,     BOLT_Y - 1, rgb, halo);
    bolt_pass(fb, stride, BOLT_X,     BOLT_Y + 1, rgb, halo);
    bolt_pass(fb, stride, BOLT_X,     BOLT_Y,     rgb, 255);
    if (charging) return;
    if (lvl == 0) {                                   /* red: a bar under it fills as it empties, so the
                                                         last few percent are readable, not just "red" */
        int w = (int)(BOLT_W * (1.0f - frac / 0.25f) + 0.5f);
        if (w > 0) render_rect_blend(fb, stride, BOLT_X, BOLT_Y + BOLT_H + 2, w, 2, rgb, (int)(200 * pulse));
    }
}

/* ---- a fish's own page ------------------------------------------------ */
#define FP_COL0   24
#define FP_COL1   236
#define FP_COLW   188
#define FP_ROW0   84
#define FP_ROWDY  42     /* a 24 px icon band plus a gap */
#define FP_BAR_H  8
#define FP_CLOSE_X 324
#define FP_CLOSE_Y 312
#define FP_CLOSE_W 92
#define FP_CLOSE_H 30
#define FP_MODAL_X 48
#define FP_MODAL_Y 96
#define FP_MODAL_W 352
#define FP_MODAL_H 168

/* the eight levels, in the order they are drawn: two columns of four */
enum { FP_HUNGER, FP_ENERGY, FP_STRESS, FP_CURIOUS, FP_TRUST, FP_BOLD, FP_SOCIAL, FP_BORED, FP_N };
/* Each level carries the stats card's OWN art, so the two screens read as one
 * thing (Strato, 2026-09-20). The card's convention is kept exactly: the four
 * needs it meters get their single 24 px icon on the left, and the three it
 * draws as sliders get their 16 px pole pair either side of the bar. Boredom
 * is not on the card and has no icon, so it gets none here. */
static const struct {
    const char   *label;
    uint32_t      rgb;
    const icon_t *lo, *hi;       /* hi = NULL: one icon on the left, the card's meter look */
    const char   *d[3];          /* what the bar means, <= 30 chars a line */
} FP_STAT[FP_N] = {
    { "HUNGER",    0xffbd59, &icon_hunger,   NULL,          { "0 IS FULL, 10 IS STARVING.",   "IT CLIMBS ON ITS OWN AND",     "DROPS WHEN THE FISH EATS." } },
    { "ENERGY",    0x78d67d, &icon_energy,   NULL,          { "HOW MUCH GO IT HAS LEFT.",     "DARTING AND PLAYING SPEND IT,", "RESTING PUTS IT BACK." } },
    { "STRESS",    0xf25b65, &icon_stress,   NULL,          { "FEAR, CROWDING AND STARTLES.", "QUICK TAPS AND A BARE TANK",   "RAISE IT. COVER CALMS IT." } },
    { "CURIOSITY", 0x6db9ff, &icon_cautious, &icon_curious, { "THE PULL TO GO AND LOOK.",     "SPENT AT THE BUBBLES AND THE", "REEF, REBUILT WHILE CALM." } },
    { "TRUST",     0xffd166, &icon_trust,    NULL,          { "HOW SAFE YOU ARE TO IT.",      "A RESTING FINGER RAISES IT,",  "QUICK TAPS COST IT." } },
    { "BOLD",      0xffffff, &icon_shy,      &icon_bold,    { "SHY AT 0, FEARLESS AT 10.",    "IT DRIFTS OVER DAYS FROM",     "HOW THE TANK TREATS IT." } },
    { "SOCIAL",    0x38dcc7, &icon_solo,     &icon_social,  { "SOLITARY AT 0, A SCHOOLER",    "AT 10. IT DRIFTS TOO, AND",    "DECIDES WHO IT FOLLOWS." } },
    { "BORED",     0xc58cff, NULL,           NULL,          { "HOW STALE ITS PASTIME IS.",    "EATING AND NIGHT REST NEVER",  "BORE. HIGH SENDS IT OFF." } },
};
static int g_fp_modal = -1;          /* the level whose explanation is up, or -1 */

static float fp_value(const fish_t *f, int i) {      /* every level on one 0..10 scale */
    switch (i) {
    case FP_HUNGER:  return f->hunger;
    case FP_ENERGY:  return f->energy;
    case FP_STRESS:  return f->stress;
    case FP_CURIOUS: return f->curiosity;
    case FP_TRUST:   return f->trust;
    case FP_BOLD:    return f->bold * 10.0f;         /* stored 0..1, like the card's sliders */
    case FP_SOCIAL:  return f->sociable * 10.0f;
    default:         return f->bored;
    }
}
static void fp_slot(int i, int *x, int *y) {
    *x = (i < 4) ? FP_COL0 : FP_COL1;
    *y = FP_ROW0 + (i % 4) * FP_ROWDY;
}

static const char *FP_STAGE[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };

/* "seek_food" -> "SEEK FOOD" for the pixel font */
static void fp_goal_words(const fish_t *f, char *out, size_t n) {
    const char *g = (f->goal.id >= 0 && f->goal.id < GOAL_COUNT) ? GOAL_NAMES[f->goal.id] : "resting";
    size_t i = 0;
    for (; g[i] && i + 1 < n; i++) {
        char ch = g[i];
        out[i] = ch == '_' ? ' ' : (char)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    }
    out[i] = 0;
}

/* the last moment, as something a keeper would say */
static const char *fp_event_words(int ev) {
    switch (ev) {
    case TEV_TAP:         return "A TAP ON THE GLASS";
    case TEV_FEED:        return "YOU DROPPED FOOD IN";
    case TEV_LIGHT_ON:    return "THE LIGHT CAME ON";
    case TEV_LIGHT_OFF:   return "THE LIGHT WENT OUT";
    case TEV_WIPE:        return "YOU WIPED THE GLASS";
    case TEV_SNIP:        return "YOU TRIMMED THE GRASS";
    case TEV_EAT:         return "ATE A PELLET";
    case TEV_SPOOK:       return "TOOK FRIGHT AND FLED";
    case TEV_INVESTIGATE: return "CAME TO YOUR FINGER";
    case TEV_BUBBLES:     return "PLAYED IN THE BUBBLES";
    case TEV_WELCOME:     return "ARRIVED IN THE TANK";
    case TEV_CONFIRM:     return "SETTLED IN";
    default:              return "NOTHING YET";
    }
}
static void fp_ago_words(float s, char *out, size_t n) {
    if (s < 60)        snprintf(out, n, "%dS AGO", (int)s);
    else if (s < 3600) snprintf(out, n, "%dM AGO", (int)(s / 60));
    else               snprintf(out, n, "%dH AGO", (int)(s / 3600));
}

void ui_fish_page(const tank_t *t, int fish, uint16_t *fb, int stride, float clock) {
    if (fish < 0 || fish >= t->n_fish) return;
    const fish_t *f = &t->fish[fish];
    render_rect(fb, stride, 0, 0, TANK_W, TANK_H, INK);

    /* who it is: the fish as it actually looks, its name, its stage and age */
    render_fish_portrait(fb, stride, 48, 38, 1.05f, f, clock);
    render_text(fb, stride, 92, 12, 3, WHITE, f->name);
    { char line[48]; int age = (int)progression_age_s(t, fish);
      const char *stage = FP_STAGE[f->stage < 4 ? f->stage : 3];
      if (age >= 3600) snprintf(line, sizeof line, "%s - TENDED %dH %dM", stage, age / 3600, (age % 3600) / 60);
      else             snprintf(line, sizeof line, "%s - TENDED %dM", stage, age / 60);
      render_text(fb, stride, 92, 42, 2, TEAL, line); }
    for (int x = 24; x < TANK_W - 24; x++) render_rect_blend(fb, stride, x, 70, 1, 1, DIM, 200);

    /* the eight levels. Every one of them is a button. */
    for (int i = 0; i < FP_N; i++) {
        int x, y; fp_slot(i, &x, &y);
        float v = fp_value(f, i);
        if (v < 0) v = 0;
        if (v > 10) v = 10;
        char num[8]; snprintf(num, sizeof num, "%d", (int)(v + 0.5f));
        render_text(fb, stride, x, y, 2, TEAL, FP_STAT[i].label);
        render_text(fb, stride, x + FP_COLW - render_text_w(num, 2), y, 2, WHITE, num);
        /* the card's art, the card's way round: a pole pair, or one icon left */
        int bx = x, bw = FP_COLW;
        if (FP_STAT[i].lo && FP_STAT[i].hi) {
            render_icon(fb, stride, x, y + 20, FP_STAT[i].lo, 255);
            render_icon(fb, stride, x + FP_COLW - 16, y + 20, FP_STAT[i].hi, 255);
            bx = x + 22; bw = FP_COLW - 44;
        } else if (FP_STAT[i].lo) {
            render_icon(fb, stride, x, y + 16, FP_STAT[i].lo, 255);
            bx = x + 30; bw = FP_COLW - 30;
        }
        const int by = y + 24;
        render_rect(fb, stride, bx, by, bw, FP_BAR_H, 0x0e2229);
        int w = (int)(bw * v / 10.0f + 0.5f);
        if (w > 0) render_rect(fb, stride, bx, by, w, FP_BAR_H, FP_STAT[i].rgb);
        render_rect_edge(fb, stride, bx, by, bw, FP_BAR_H, INNER);
    }

    /* what it is doing, and what last happened to it */
    { char goal[32], line[64];
      fp_goal_words(f, goal, sizeof goal);
      snprintf(line, sizeof line, "%s, URGENCY %d", goal, (int)(f->goal.urgency + 0.5f));
      render_text(fb, stride, FP_COL0, 258, 2, FAINT, "DOING");
      render_text(fb, stride, FP_COL0 + 72, 258, 2, WHITE, line); }
    { int ev; float ago; char line[64], when[16];
      render_text(fb, stride, FP_COL0, 282, 2, FAINT, "LAST");
      if (tank_last_event(fish, &ev, &ago)) {
          fp_ago_words(ago, when, sizeof when);
          snprintf(line, sizeof line, "%s, %s", fp_event_words(ev), when);
      } else snprintf(line, sizeof line, "NOTHING YET");
      render_text(fb, stride, FP_COL0 + 72, 282, 2, WHITE, line); }

    render_text(fb, stride, FP_COL0, 306, 2, FAINT, "TAP A BAR TO LEARN MORE");   /* ends at x 298, clear of CLOSE */
    render_button(fb, stride, FP_CLOSE_X, FP_CLOSE_Y, FP_CLOSE_W, FP_CLOSE_H, INNER, TEAL, "CLOSE", 2);

    if (g_fp_modal < 0) return;
    for (int y = 0; y < TANK_H; y++)                       /* the page out of reach under the explanation */
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
    const int X = FP_MODAL_X, Y = FP_MODAL_Y, W = FP_MODAL_W, H = FP_MODAL_H;
    render_rect(fb, stride, X, Y, W, H, PANEL);
    render_rect_edge(fb, stride, X, Y, W, H, TEAL);
    render_rect_edge(fb, stride, X + 1, Y + 1, W - 2, H - 2, INNER);
    const char *label = FP_STAT[g_fp_modal].label;
    render_text(fb, stride, X + (W - render_text_w(label, 3)) / 2, Y + 18, 3, FP_STAT[g_fp_modal].rgb, label);
    for (int i = 0; i < 3; i++) {
        const char *d = FP_STAT[g_fp_modal].d[i];
        render_text(fb, stride, X + (W - render_text_w(d, 2)) / 2, Y + 58 + i * 24, 2, TEAL, d);
    }
    render_text(fb, stride, X + (W - render_text_w("TAP TO CLOSE", 2)) / 2, Y + H - 26, 2, FAINT, "TAP TO CLOSE");
}

int ui_fish_page_tap(const tank_t *t, int fish, float x, float y) {
    (void)t; (void)fish;
    if (g_fp_modal >= 0) { g_fp_modal = -1; return UI_FP_KEPT; }     /* any tap dismisses the explanation */
    if (x >= FP_CLOSE_X - 12 && y >= FP_CLOSE_Y - 8) return UI_FP_CLOSE;
    for (int i = 0; i < FP_N; i++) {                                 /* a generous band around each bar */
        int bx, by; fp_slot(i, &bx, &by);
        if (x >= bx - 10 && x < bx + FP_COLW + 10 && y >= by - 6 && y < by + 40) {   /* label, icons and bar are all the button */
            g_fp_modal = i; return UI_FP_KEPT;
        }
    }
    return UI_FP_NONE;
}

void ui_fish_page_leave(void) { g_fp_modal = -1; }
