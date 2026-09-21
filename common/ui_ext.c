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
/* This fork's own milestones (tank.h MS_LOCAL_BIT0). They are HIDDEN until
 * earned - nothing shows an empty slot - and they live here rather than on
 * the overview page because that page's badge row is hard-capped at six
 * across the full width. Tapping one says what earned it. */
#define FP_MS_N 4
static const struct { uint32_t bit; const icon_t *icon; const char *name; const char *d[3]; } FP_MS[FP_MS_N] = {
    { MS_CASTLE_GATE, &icon_ms_castle_gate, "THROUGH THE GATE",
      { "IT SWAM THROUGH THE CASTLE'S", "ARCH. NOTHING IN THIS TANK",  "IS SOLID - THEY GO ANYWHERE." } },
    { MS_GLOW_TOSS,   &icon_ms_glow_toss,   "GLOW STICK TOSS",
      { "IT CARRIED A GLOW STICK UP",   "AND LET GO NEAR THE TOP,",    "JUST TO WATCH IT SINK." } },
    { MS_TOTEM_HOLD,  &icon_ms_totem_hold,  "TOTEM BEARER",
      { "IT LIFTED THE TOTEM AND LED",  "A PARADE. THE LIGHTS GO OUT", "WHEN SOMEONE PICKS IT UP." } },
    { MS_BASS_PARTY,  &icon_ms_bass_party,  "AT THE SPEAKER",
      { "IT STAYED FOR A PARTY AT THE", "BASS STACK. EVERY ONE MAKES", "IT KEENER TO LEAD THE NEXT." } },
};
/* the i-th EARNED milestone, or -1: the strip packs left with no gaps */
static int fp_ms_at(const fish_t *f, int slot) {
    int n = 0;
    for (int i = 0; i < FP_MS_N; i++)
        if (f->ms_bits & FP_MS[i].bit) { if (n == slot) return i; n++; }
    return -1;
}
/* the announcement over the live tank asks for these: render.c's own tables
 * stop at the upstream bits (FISH_BADGES[6], MS_NAMES[MS_FISH_COUNT]), so a
 * milestone of this fork's - MS_LOCAL_BIT0 and up - came out with no icon and
 * an empty caption, just the fish and its name. */
const icon_t *ui_local_ms(uint32_t bit, const char **name) {
    for (int i = 0; i < FP_MS_N; i++)
        if (FP_MS[i].bit == bit) { if (name) *name = FP_MS[i].name; return FP_MS[i].icon; }
    return NULL;
}
/* ---- the long lines (2026-09-21) -------------------------------------
 * DOING and LAST can both outrun the column - a long goal word with an
 * urgency on the end, an event with "3 MINUTES AGO" after it. They scroll
 * instead of being cut off: out, a pause, back, a pause, forever. The walk
 * is a pure function of the tank clock, so nothing has to be remembered
 * between frames; a swipe parks it and hands the line to the finger for
 * FP_SCRUB_HOLD_S, then it picks the walk back up. */
/* The pixel font has no '<' or '>' (nor '$', '^', '(' ...), so a chevron
 * drawn as text is drawn as nothing at all - which is what happened the
 * first time. render.c's settings page has the same problem and solves it
 * the same way: draw the triangle. `dir` -1 points left, +1 right. */
static void fp_chevron(uint16_t *fb, int stride, int cx, int cy, int dir, int h, uint32_t rgb) {
    for (int i = 0; i < h; i++) {
        int run = i < h / 2 ? i : h - 1 - i;              /* a wedge, thickest in the middle */
        for (int k = 0; k <= run; k++)
            render_rect(fb, stride, cx + dir * (run - k) - (dir < 0 ? 1 : 0), cy + i, 2, 1, rgb);
    }
}
#define FP_SCROLL_PXS    22.0f          /* how fast it walks */
#define FP_SCROLL_PAUSE  1.6f           /* and how long it rests at each end */
static float g_fp_scrub[2];             /* where the finger left each line */
static float g_fp_scrub_until;          /* ... on the tank clock */
static float fp_marquee_off(float clock, float over, int slot) {
    float travel = over / FP_SCROLL_PXS;
    float period = 2 * (travel + FP_SCROLL_PAUSE);
    float u = fmodf(clock + slot * 0.9f, period);
    if (u < FP_SCROLL_PAUSE) return 0;                       /* resting at the start */
    u -= FP_SCROLL_PAUSE;
    if (u < travel) return over * (u / travel);              /* walking out */
    u -= travel;
    if (u < FP_SCROLL_PAUSE) return over;                    /* resting at the end */
    return over * (1.0f - (u - FP_SCROLL_PAUSE) / travel);   /* walking back */
}
/* one line, clipped to `w`: drawn shifted, then the overflow painted out
 * either side. The page's background is flat, so painting over is enough and
 * render.h needs no clip rectangle it does not already have. */
static void fp_line(uint16_t *fb, int stride, int x, int y, int w, const char *text, float clock, int slot) {
    int tw = render_text_w(text, 2);
    if (tw <= w) { render_text(fb, stride, x, y, 2, WHITE, text); return; }
    float over = (float)(tw - w);
    float off = clock < g_fp_scrub_until ? g_fp_scrub[slot] : fp_marquee_off(clock, over, slot);
    if (off < 0) off = 0;
    if (off > over) off = over;
    render_text(fb, stride, x - (int)(off + 0.5f), y, 2, WHITE, text);
    render_rect(fb, stride, x - 200, y - 2, 200, 18, INK);           /* what hangs off the left */
    render_rect(fb, stride, x + w, y - 2, TANK_W - (x + w), 18, INK); /* ... and off the right */
    /* a hint that there is more, on the side there is more of */
    if (off < over - 0.5f) fp_chevron(fb, stride, x + w - 2, y + 2, +1, 11, FAINT);
    if (off > 0.5f)        fp_chevron(fb, stride, x - 6, y + 2, -1, 11, FAINT);
}
void ui_fish_page_swipe(float dx, float clock) {
    for (int i = 0; i < 2; i++) {
        g_fp_scrub[i] -= dx;                                  /* drag left, the text comes left */
        if (g_fp_scrub[i] < 0) g_fp_scrub[i] = 0;
        if (g_fp_scrub[i] > 400) g_fp_scrub[i] = 400;
    }
    g_fp_scrub_until = clock + FP_SCRUB_HOLD_S;
}
/* the panel's arrows (2026-09-21): every other page that opens a detail
 * panel lets you walk along it, so this one does too. A milestone panel
 * walks the ones this fish has EARNED; a level's panel walks the levels.
 * Both wrap, and neither shows an arrow when there is only one thing. */
#define FP_ARROW_W 44
static int fp_ms_count(const fish_t *f) {
    int n = 0;
    for (int i = 0; i < FP_MS_N; i++) if (f->ms_bits & FP_MS[i].bit) n++;
    return n;
}
static int fp_ms_slot_of(const fish_t *f, int m) {          /* which earned slot `m` occupies */
    int n = 0;
    for (int i = 0; i < FP_MS_N; i++) {
        if (!(f->ms_bits & FP_MS[i].bit)) continue;
        if (i == m) return n;
        n++;
    }
    return -1;
}
static int g_fp_modal = -1;          /* the level (0..FP_N-1) or milestone (FP_N+) whose panel is up, or -1 */

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
    case TEV_GLOW_PLAY:   return "DROPPED A GLOW STICK";
    case TEV_TOTEM_LIFT:  return "LIFTED THE TOTEM";
    case TEV_GLOW_CATCH:  return "CAUGHT A GLOW STICK";
    case TEV_GLOW_RALLY:  return "A LONG GLOW STICK RALLY";
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
      if (age >= 3600) snprintf(line, sizeof line, "%s - %dH %dM", stage, age / 3600, (age % 3600) / 60);
      else             snprintf(line, sizeof line, "%s - %dM", stage, age / 60);
      render_text(fb, stride, 92, 42, 2, TEAL, line); }
    for (int i = 0, slot = 0; i < FP_MS_N; i++) {     /* earned milestones only - no empty slots */
        if (!(f->ms_bits & FP_MS[i].bit)) continue;
        render_icon(fb, stride, FP_MS_X + slot * FP_MS_DX, FP_MS_Y, FP_MS[i].icon, 255);
        slot++;
    }
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
    { const int LX = FP_COL0 + 72, LW = TANK_W - 20 - (FP_COL0 + 72);
      char goal[32], line[64];
      fp_goal_words(f, goal, sizeof goal);
      snprintf(line, sizeof line, "%s, URGENCY %d", goal, (int)(f->goal.urgency + 0.5f));
      int ev; float ago; char l2[64], when[16];
      if (tank_last_event(fish, &ev, &ago)) {
          fp_ago_words(ago, when, sizeof when);
          snprintf(l2, sizeof l2, "%s, %s", fp_event_words(ev), when);
      } else snprintf(l2, sizeof l2, "NOTHING YET");
      fp_line(fb, stride, LX, 258, LW, line, clock, 0);
      fp_line(fb, stride, LX, 282, LW, l2, clock, 1);
      /* the labels go on AFTER: a scrolling line paints out everything left
         of its column, and that is where the labels sit */
      render_text(fb, stride, FP_COL0, 258, 2, FAINT, "DOING");
      render_text(fb, stride, FP_COL0, 282, 2, FAINT, "LAST"); }

    render_text(fb, stride, FP_COL0, 306, 2, FAINT, "TAP A BAR TO LEARN MORE");   /* ends at x 298, clear of CLOSE */
    render_button(fb, stride, FP_CLOSE_X, FP_CLOSE_Y, FP_CLOSE_W, FP_CLOSE_H, INNER, TEAL, "CLOSE", 2);

    if (g_fp_modal < 0) return;
    for (int y = 0; y < TANK_H; y++)                       /* the page out of reach under the explanation */
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
    const int X = FP_MODAL_X, Y = FP_MODAL_Y, W = FP_MODAL_W, H = FP_MODAL_H;
    render_rect(fb, stride, X, Y, W, H, PANEL);
    render_rect_edge(fb, stride, X, Y, W, H, TEAL);
    render_rect_edge(fb, stride, X + 1, Y + 1, W - 2, H - 2, INNER);
    if (g_fp_modal >= FP_N) {                          /* a milestone's panel */
        const int m = g_fp_modal - FP_N;
        render_icon(fb, stride, X + (W - 32) / 2, Y + 8, FP_MS[m].icon, 255);
        render_text(fb, stride, X + (W - render_text_w(FP_MS[m].name, 3)) / 2, Y + 44, 3, WHITE, FP_MS[m].name);
        for (int i = 0; i < 3; i++)
            render_text(fb, stride, X + (W - render_text_w(FP_MS[m].d[i], 2)) / 2, Y + 72 + i * 20, 2, TEAL, FP_MS[m].d[i]);
        if (FP_MS[m].bit == MS_BASS_PARTY && f->parties > 0) {
            char n[32]; snprintf(n, sizeof n, "%d SO FAR", f->parties);
            render_text(fb, stride, X + (W - render_text_w(n, 2)) / 2, Y + 134, 2, WHITE, n);
        }
        /* on the TAP TO CLOSE line, where the sides are clear - centred
           vertically they landed in the middle of the description */
        if (fp_ms_count(f) > 1) {
            fp_chevron(fb, stride, X + 22, Y + H - 26, -1, 16, TEAL);
            fp_chevron(fb, stride, X + W - 22, Y + H - 26, +1, 16, TEAL);
        }
        render_text(fb, stride, X + (W - render_text_w("TAP TO CLOSE", 2)) / 2, Y + H - 20, 2, FAINT, "TAP TO CLOSE");
        return;
    }
    const char *label = FP_STAT[g_fp_modal].label;
    render_text(fb, stride, X + (W - render_text_w(label, 3)) / 2, Y + 18, 3, FP_STAT[g_fp_modal].rgb, label);
    for (int i = 0; i < 3; i++) {
        const char *d = FP_STAT[g_fp_modal].d[i];
        render_text(fb, stride, X + (W - render_text_w(d, 2)) / 2, Y + 58 + i * 24, 2, TEAL, d);
    }
    fp_chevron(fb, stride, X + 22, Y + H - 32, -1, 16, TEAL);
    fp_chevron(fb, stride, X + W - 22, Y + H - 32, +1, 16, TEAL);
    render_text(fb, stride, X + (W - render_text_w("TAP TO CLOSE", 2)) / 2, Y + H - 26, 2, FAINT, "TAP TO CLOSE");
}

int ui_fish_page_tap(const tank_t *t, int fish, float x, float y) {
    if (g_fp_modal >= 0) {
        const int X = FP_MODAL_X, W = FP_MODAL_W, Y = FP_MODAL_Y, H = FP_MODAL_H;
        int dir = 0;                                                  /* the arrows walk it along */
        if (y >= Y + 20 && y < Y + H - 20) {
            if (x >= X && x < X + FP_ARROW_W) dir = -1;
            else if (x >= X + W - FP_ARROW_W && x < X + W) dir = 1;
        }
        if (dir && g_fp_modal >= FP_N && fish >= 0 && fish < t->n_fish) {
            const fish_t *f = &t->fish[fish];
            int n = fp_ms_count(f);
            if (n > 1) {
                int slot = fp_ms_slot_of(f, g_fp_modal - FP_N);
                int m = fp_ms_at(f, ((slot + dir) % n + n) % n);
                if (m >= 0) { g_fp_modal = FP_N + m; return UI_FP_KEPT; }
            }
        } else if (dir) {
            g_fp_modal = (g_fp_modal + dir + FP_N) % FP_N;
            return UI_FP_KEPT;
        }
        g_fp_modal = -1; return UI_FP_KEPT;                          /* anywhere else closes it */
    }
    if (x >= FP_CLOSE_X - 12 && y >= FP_CLOSE_Y - 8) return UI_FP_CLOSE;
    if (fish >= 0 && fish < t->n_fish && y >= FP_MS_Y - 8 && y < FP_MS_Y + 40 && x >= FP_MS_X - 8) {
        int slot = (int)((x - (FP_MS_X - 8)) / FP_MS_DX);            /* an earned milestone's badge */
        int m = fp_ms_at(&t->fish[fish], slot);
        if (m >= 0) { g_fp_modal = FP_N + m; return UI_FP_KEPT; }
    }
    for (int i = 0; i < FP_N; i++) {                                 /* a generous band around each bar */
        int bx, by; fp_slot(i, &bx, &by);
        if (x >= bx - 10 && x < bx + FP_COLW + 10 && y >= by - 6 && y < by + 40) {   /* label, icons and bar are all the button */
            g_fp_modal = i; return UI_FP_KEPT;
        }
    }
    return UI_FP_NONE;
}

void ui_fish_page_leave(void) { g_fp_modal = -1; }

/* ---- the dev page (2026-09-21) --------------------------------------
 * Seven taps on the settings page's version line open this. The grid is two
 * columns of four; the eighth cell is CLOSE, so the thumb always finds a way
 * out in the same place the other pages put it. */
#define DV_COLS   2
#define DV_X0     24
#define DV_DX     212
#define DV_W      188
#define DV_Y0     72
#define DV_DY     54
#define DV_H      44
#define DV_ROWS   4
/* the order on the glass, and what each one asks the platform for */
/* No LIGHT button: it called tank_toggle_light, which latches light_override
 * on for good, and light_override outranks the double-tap's light_manual_off
 * in tank_tick - so one press killed the double-tap until the tank was reset.
 * The double-tap is the way to work the light and always was. */
#define DV_N 7
static const struct { const char *label; int act; } DV_BTN[DV_N] = {
    { "+1000 SAND",  UI_DEV_DOLLARS },    { "BROKE",      UI_DEV_BROKE },
    { "BUY IT ALL",  UI_DEV_UNLOCK_ALL }, { "GROW A FISH", UI_DEV_GROW },
    { "BASS PARTY",  UI_DEV_PARTY },      { "ADD A FRY",  UI_DEV_FRY },
    { "BATTERY",     UI_DEV_BATTERY },
};
static void dv_cell(int i, int *x, int *y) {
    *x = DV_X0 + (i % DV_COLS) * DV_DX;
    *y = DV_Y0 + (i / DV_COLS) * DV_DY;
}
void ui_dev_page(const tank_t *t, uint16_t *fb, int stride, const char *status) {
    render_rect(fb, stride, 0, 0, TANK_W, TANK_H, INK);
    render_text(fb, stride, DV_X0, 14, 3, WHITE, "DEV");
    render_text(fb, stride, DV_X0 + 64, 20, 2, FAINT, "NOT FOR THE KEEPER");
    /* what the tank is worth and holds right now, so a button's effect shows */
    char line[48];
    snprintf(line, sizeof line, "SAND %d    FISH %d", (int)t->sd_balance, t->n_fish);
    render_text(fb, stride, DV_X0, 46, 2, TEAL, line);
    for (int x = DV_X0; x < TANK_W - DV_X0; x++) render_rect_blend(fb, stride, x, 66, 1, 1, DIM, 200);
    for (int i = 0; i < DV_N; i++) {
        int bx, by; dv_cell(i, &bx, &by);
        render_button(fb, stride, bx, by, DV_W, DV_H, INNER, TEAL, DV_BTN[i].label, 2);
    }
    /* the last action's line sits between the grid and CLOSE, never under it */
    if (status && *status) render_text(fb, stride, DV_X0, 288, 2, FAINT, status);
    render_button(fb, stride, FP_CLOSE_X, FP_CLOSE_Y, FP_CLOSE_W, FP_CLOSE_H, INNER, TEAL, "CLOSE", 2);
}
int ui_dev_page_tap(float x, float y) {
    if (x >= FP_CLOSE_X - 12 && y >= FP_CLOSE_Y - 8) return UI_DEV_CLOSE;
    for (int i = 0; i < DV_N; i++) {
        int bx, by; dv_cell(i, &bx, &by);
        if (x >= bx - 6 && x < bx + DV_W + 6 && y >= by - 6 && y < by + DV_H + 6) return DV_BTN[i].act;
    }
    return UI_DEV_NONE;
}

/* what each button actually does. One copy, called by both platforms. */
bool ui_dev_apply(tank_t *t, int act, char *status, size_t n) {
    switch (act) {
    case UI_DEV_DOLLARS:
        progression_sd_grant(t, SD_DEV_GRANT);
        snprintf(status, n, "+%d - NOW %d SAND", SD_DEV_GRANT, (int)t->sd_balance);
        return true;
    case UI_DEV_BROKE:
        progression_sd_grant(t, -t->sd_balance);
        snprintf(status, n, "BROKE AGAIN - NOTHING LEFT");
        return true;
    case UI_DEV_UNLOCK_ALL: {
        int bought = 0;
        for (int i = 0; i < SD_ITEM_COUNT; i++) {
            if (t->sd_unlocks & SD_ITEMS[i].bit) continue;
            if (t->sd_balance < SD_ITEMS[i].price) progression_sd_grant(t, SD_ITEMS[i].price - t->sd_balance);
            if (progression_buy(t, i)) bought++;
        }
        snprintf(status, n, bought ? "BOUGHT %d - ALL IN THE TANK" : "NOTHING LEFT TO BUY", bought);
        return true;
    }
    case UI_DEV_GROW: {
        /* the youngest fish takes the next step up, so repeated presses walk
           the whole tank up a stage at a time */
        int who = -1; float young = 0;
        for (int i = 0; i < t->n_fish; i++) {
            float a = progression_age_s(t, i);
            if (who < 0 || a < young) { who = i; young = a; }
        }
        if (who < 0) { snprintf(status, n, "NO FISH TO GROW"); return true; }
        float next = young < STAGE_JUV_AGE ? STAGE_JUV_AGE
                   : young < STAGE_ADULT_AGE ? STAGE_ADULT_AGE
                   : young < STAGE_ELDER_AGE ? STAGE_ELDER_AGE : young + STAGE_ELDER_AGE;
        progression_set_age(t, who, next);
        /* the schema's stage token is lowercase; this page shouts like the rest */
        char stage[12]; snprintf(stage, sizeof stage, "%s", STAGE_NAMES[t->fish[who].stage]);
        for (char *c = stage; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        snprintf(status, n, "%s IS %s NOW", t->fish[who].name, stage);
        return true;
    }
    case UI_DEV_PARTY:
        if (!tank_bit_live(t, SD_ITEM_TOTEM)) { snprintf(status, n, "NO TOTEM - BUY IT ALL FIRST"); return true; }
        if (t->totem_phase != TOTEM_OFF)      { snprintf(status, n, "A PARADE IS ALREADY RUNNING"); return true; }
        tank_totem_force(t);
        snprintf(status, n, "%s HAS THE TOTEM - LIGHTS OUT", t->fish[t->totem_carrier].name);
        return true;
    case UI_DEV_FRY:
        progression_stage_arrival(t);
        snprintf(status, n, "A FRY IS ON ITS WAY");
        return true;
    default:
        return false;                     /* UI_DEV_BATTERY: the platform's own */
    }
}
float ui_dev_battery_next(float frac) {
    const float STEPS[] = { 1.0f, 0.60f, 0.40f, 0.10f };   /* green, yellow, orange, red */
    for (int i = 0; i < 4; i++)
        if (fabsf(frac - STEPS[i]) < 0.005f) return i == 3 ? -1.0f : STEPS[i + 1];
    return STEPS[0];                                        /* not on the walk: start at green */
}
