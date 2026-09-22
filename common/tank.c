/* tank.c — reflex layer. See tank.h. Faithful port of the browser prototype's
 * updateFish/targetForGoal/wall handling, with prototype px values scaled by
 * ~0.55 for the 448-wide tank. */
#include "tank.h"
#include "tank_events.h"
#include <math.h>
#include <stddef.h>

#define TAU 6.2831853f
static void veg_sync(tank_t *t);   /* veg_growth[] = per-bed mean of veg_h[][] */

const char *const GOAL_NAMES[GOAL_COUNT] = {
    "seek_food", "flee_shadow", "visit_bubbles", "follow_friend",
    "explore", "rest", "dart_play", "inspect_reef",
};

/* ---- the event bus (tank_events.h): one listener, synchronous ---- */
const char *const TANK_EVENT_NAMES[TEV_COUNT] = {
    "tap", "feed", "light_on", "light_off", "wipe", "snip", "eat", "spook", "investigate", "bubbles",
    "welcome", "wheel_tick", "confirm", "glow_play", "totem_lift", "glow_catch", "glow_rally",
};
const float FISH_SPEED_MUL[FISH_SPEED_N] = { 0.65f, 1.0f, 1.65f };
const char *const FISH_SPEED_NAMES[FISH_SPEED_N] = { "SLOWER", "NORMAL", "FASTER" };
static float s_glow_cool[N_FISH_MAX];   /* glow sticks: seconds until this fish may take one again (not saved) */
/* ---- play (tank.h): all of it transient, none of it saved ---- */
static float s_glow_want[N_FISH_MAX];   /* seconds this fish stays keen for a stick */
static float s_roll_t;                  /* cadence of the idle "fancy a play?" roll */
static int   s_catch_to = -1;           /* the fish a stick is in the air towards */
static int   s_catch_stick = -1;        /* which stick */
static float s_catch_s;                 /* how long the pass stays catchable */
static int   s_rally;                   /* passes landed in this rally */
static float s_glow_watch[N_FISH_MAX];  /* seconds left following its own dropped stick down */
static int   s_glow_watched[N_FISH_MAX];/* ... and which stick it is watching */
static float s_after_s;                 /* the after-hours window, seconds left */
static int   s_totem_invited = -1;      /* a fish asked to go and get the totem */
static float s_totem_invite_s;
static int   s_decor_new = -1;          /* a piece just placed: worth a look */
static float s_decor_new_s;
static float s_totem_cool;              /* the quiet after a totem parade (not saved) */
static tank_event_fn s_ev_fn; static void *s_ev_ud;
void tank_events_set(tank_event_fn fn, void *ud) { s_ev_fn = fn; s_ev_ud = ud; }
/* the last moment per fish (tank.h): tank_emit has no tank_t, so tank_tick
 * leaves the clock here for it. One tank at a time, like the rest of tank.c's
 * module state. */
static float s_emit_clock;
static struct { int8_t ev; float clock; } s_fish_ev[N_FISH_MAX];
void tank_emit(int ev, int fish) {
    if (fish >= 0 && fish < N_FISH_MAX) { s_fish_ev[fish].ev = (int8_t)ev; s_fish_ev[fish].clock = s_emit_clock; }
    if (s_ev_fn) s_ev_fn(ev, fish, s_ev_ud);
}
bool tank_last_event(int fish, int *ev, float *seconds_ago) {
    if (fish < 0 || fish >= N_FISH_MAX || s_fish_ev[fish].ev < 0) return false;
    if (ev) *ev = s_fish_ev[fish].ev;
    if (seconds_ago) *seconds_ago = s_emit_clock - s_fish_ev[fish].clock;
    return true;
}

const char *const STAGE_NAMES[4] = { "fry", "juv", "adult", "elder" };
const char *const TRAINED_NAMES[N_TRAINED_NAMES] = { "mira", "bolt", "kelp", "nori" };

int tank_reflex_overrides = 0;   /* starvation-ignored episodes (diagnostic) */

/* deterministic xorshift32 */
static uint32_t xr(tank_t *t) {
    uint32_t x = t->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return t->rng = x;
}
float tank_randf(tank_t *t, float lo, float hi) {
    return lo + (hi - lo) * (float)(xr(t) & 0xffffff) / 16777215.0f;
}
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
static float norm_ang(float a) {
    while (a >  3.14159265f) a -= TAU;
    while (a < -3.14159265f) a += TAU;
    return a;
}
static float lerpf(float a, float b, float p) { return a + (b - a) * p; }

float tank_dist(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

int tank_nearest_food(const tank_t *t, const fish_t *f, float *dist_out) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!t->food[i].alive) continue;
        float d = tank_dist(f->x, f->y, t->food[i].x, t->food[i].y);
        if (d < bd) { bd = d; best = i; }
    }
    if (dist_out) *dist_out = bd;
    return best;
}

int tank_nearest_friend(const tank_t *t, int fish_idx, float *dist_out) {
    const fish_t *f = &t->fish[fish_idx];
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < t->n_fish; i++) {
        if (i == fish_idx) continue;
        float d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y);
        if (d < bd) { bd = d; best = i; }
    }
    if (dist_out) *dist_out = bd;
    return best;
}

/* ---- roster: six presets (the prototype's four + two), colors chosen for
 * contrast on AMOLED black. A preset is a look + temperament; bold/social are
 * rolled per tank (starting pair) or inherited (arrivals). ---- */
typedef struct {
    const char *name; uint32_t color, fin, accent;
    float size, curiosity, lazy, turn_rate;
} preset_t;
static const preset_t ROSTER[] = {
    /* name    color     fin       accent    size  cur  lazy  turn */
    { "mira", 0x38dcc7, 0x1d9f98, 0xffbd59, 1.08f, 7.5f, 0.2f, 3.2f },
    { "bolt", 0xff725c, 0xb83a43, 0xffe08a, 0.94f, 6.0f, 0.2f, 4.2f },
    { "kelp", 0x78d67d, 0x3f8b55, 0xa799ff, 0.86f, 4.0f, 0.1f, 3.2f },
    { "nori", 0xa799ff, 0x6b5ed6, 0x78d67d, 1.00f, 5.0f, 0.7f, 2.4f },
    { "pip",  0xffd166, 0xc98a1e, 0x38dcc7, 0.90f, 6.5f, 0.3f, 3.8f },
    { "sol",  0xf48fb1, 0xb0456f, 0xffe08a, 1.04f, 5.5f, 0.4f, 2.9f },
};
#define ROSTER_N ((int)(sizeof ROSTER / sizeof ROSTER[0]))
int tank_roster_count(void) { return ROSTER_N; }
const char *tank_roster_name(int preset) { return preset >= 0 && preset < ROSTER_N ? ROSTER[preset].name : "?"; }

/* the keeper's palettes (setup.c): the six roster bodies + a blue and a
 * silver; the roster's five accents + white, the stress red and a dark ink */
const uint32_t LOOK_BODY[LOOK_N]   = { 0x38dcc7, 0xff725c, 0x78d67d, 0xa799ff, 0xffd166, 0xf48fb1, 0x4da3ff, 0xe8f1f2 };
const uint32_t LOOK_ACCENT[LOOK_N] = { 0xffbd59, 0xffe08a, 0xa799ff, 0x78d67d, 0x38dcc7, 0xffffff, 0xf25b65, 0x1a2a30 };

void tank_set_name(tank_t *t, int slot, const char *name) {
    if (slot < 0 || slot >= N_FISH_MAX) return;
    fish_t *f = &t->fish[slot];
    if (!name || !*name) name = tank_roster_name(f->preset);
    int n = 0;
    /* canonical lowercase (2026-09-15): the roster, the director and the
     * trained names are lowercase, the display uppercases at draw, and the
     * wheel used to write capitals into the slots it spun ("FeZ", "LArRY") */
    while (name[n] && n < FISH_NAME_MAX) {
        char c = name[n];
        f->name[n] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
        n++;
    }
    f->name[n] = 0;
}
static uint32_t fin_for(uint32_t body) {
    for (int i = 0; i < ROSTER_N; i++) if (ROSTER[i].color == body) return ROSTER[i].fin;
    /* a body colour of the keeper's own: the fin is that body at 58% */
    uint32_t r = (body >> 16 & 255) * 58 / 100, g = (body >> 8 & 255) * 58 / 100, b = (body & 255) * 58 / 100;
    return (r << 16) | (g << 8) | b;
}
void tank_set_look(tank_t *t, int slot, uint32_t body, uint32_t accent) {
    if (slot < 0 || slot >= N_FISH_MAX) return;
    fish_t *f = &t->fish[slot];
    if (body)   { f->color = body; f->fin = fin_for(body); }
    if (accent) f->accent = accent;
    if (f->accent == f->color)                        /* stripes the body's own colour would vanish */
        for (int i = 0; i < LOOK_N; i++) if (LOOK_ACCENT[i] != f->color) { f->accent = LOOK_ACCENT[i]; break; }
}

void tank_make_fish(tank_t *t, int slot, int preset, float sociable, float bold, stage_t stage) {
    fish_t *f = &t->fish[slot];
    const preset_t *p = &ROSTER[preset];
    f->preset = preset; tank_set_name(t, slot, p->name);
    f->model_name = TRAINED_NAMES[slot % N_TRAINED_NAMES];   /* names carry no signal */
    f->x = tank_randf(t, 90, TANK_W - 90); f->y = tank_randf(t, 80, TANK_H - 90);
    f->heading = tank_randf(t, 0, TAU);
    f->speed = 0; f->target_speed = 0; f->wander = f->x * 0.05f;
    f->base_size = p->size; f->size = p->size; f->turn_rate = p->turn_rate;
    f->hunger = tank_randf(t, 3, 6); f->energy = tank_randf(t, 5, 8);
    f->stress = tank_randf(t, 0, 2); f->curiosity = p->curiosity;
    f->sociable = sociable; f->bold = bold; f->lazy = p->lazy;
    f->bold0 = bold; f->sociable0 = sociable; f->drift_acc = 0;
    f->stage = stage;
    f->starve_flagged = false;
    f->trust = 5.0f;
    f->goal.id = GOAL_EXPLORE; f->goal.urgency = 3; f->goal.confidence = 1; f->goal.runner_up = GOAL_COUNT;
    f->goal_age = 0; f->ask_age = 99; f->dart_timer = 0; f->dart_x = f->x; f->dart_y = f->y; f->hesitate = 0;
    f->bored = 0; f->goal_prev = GOAL_COUNT; f->zone_last = -1; f->explore_set = false;
    /* its own order of first visits - from the slot, not the tank's RNG, so a
     * seeded run (the selftests) draws the same numbers it did before */
    for (int z = 0; z < 6; z++) f->zone_seen[z] = -(float)((slot * 37 + z * 13) % 60);
    f->eaten = 0; f->eaten_player = 0; f->at_bubbles = false;
    /* its own spot by the reef: bolder fish rest a little further out */
    f->rest_dx = 8 + slot * 24 + bold * 14; f->rest_dy = -slot * 9 - tank_randf(t, 0, 10);   /* a body apart (was 9 px per slot) */
    f->sig = 0xffffffffu; f->ms_bits = 0; f->parties = 0; f->ms_seen = 0;
    f->color = p->color; f->fin = p->fin; f->accent = p->accent;
    f->parent_a = f->parent_b = -1;
}

void tank_set_bubble_x(tank_t *t, float x) {
    /* clear of the reef rock (the fish's other landmark) and of the glass;
       grass is fine - the beds cover most of the width and the default spot
       already rises through one */
    float lo = t->reef_x + 50, hi = TANK_W - 30;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    float dx = x - t->bubble_x;
    t->bubble_x = x;
    for (int i = 0; i < MAX_BUBBLE; i++) if (t->bubble[i].column) t->bubble[i].x += dx;   /* the column moves as one */
}

static void place_near_reef(tank_t *t, fish_t *f) {
    f->x = t->reef_x + tank_randf(t, -10, 30); f->y = t->reef_y - 30 + tank_randf(t, -10, 10);
    f->heading = tank_randf(t, -0.6f, 0.6f);
}

void tank_new_population(tank_t *t) {
    /* two random presets */
    int a = (int)tank_randf(t, 0, ROSTER_N - 0.001f);
    int b = (int)tank_randf(t, 0, ROSTER_N - 1.001f); if (b >= a) b++;
    /* personalities rolled with a guaranteed contrast so the pair reads as
     * two characters at a glance (docs/progression-next.md, Act 1) */
    float ba, bb, sa, sb;
    do { ba = tank_randf(t, 0.1f, 0.9f); bb = tank_randf(t, 0.1f, 0.9f); } while (fabsf(ba - bb) < 0.45f);
    do { sa = tank_randf(t, 0.1f, 0.9f); sb = tank_randf(t, 0.1f, 0.9f); } while (fabsf(sa - sb) < 0.3f);
    /* both start as FRY: the founding pair grows up on the keeper's watch
     * (progression halved the stage clocks to match - 0.5/3/24 tended hours) */
    tank_make_fish(t, 0, a, sa, ba, STAGE_FRY);
    tank_make_fish(t, 1, b, sb, bb, STAGE_FRY);
    t->n_fish = 2;
}

int tank_add_fish(tank_t *t, int parent_a, int parent_b) {
    if (t->n_fish >= N_FISH_MAX || t->n_fish >= ROSTER_N) return -1;
    int used[ROSTER_N] = {0};
    for (int i = 0; i < t->n_fish; i++) used[t->fish[i].preset] = 1;
    int free_n = 0, free_idx[ROSTER_N];
    for (int i = 0; i < ROSTER_N; i++) if (!used[i]) free_idx[free_n++] = i;
    if (!free_n) return -1;
    int preset = free_idx[(int)tank_randf(t, 0, free_n - 0.001f)];
    int ia = (parent_a >= 0 && parent_a < t->n_fish) ? parent_a : 0;
    int ib = (parent_b >= 0 && parent_b < t->n_fish) ? parent_b : (t->n_fish > 1 ? 1 : 0);
    const fish_t *pa = &t->fish[ia], *pb = &t->fish[ib];
    float bold = clampf((pa->bold + pb->bold) * 0.5f + tank_randf(t, -0.15f, 0.15f), 0.05f, 0.95f);
    float soc  = clampf((pa->sociable + pb->sociable) * 0.5f + tank_randf(t, -0.15f, 0.15f), 0.05f, 0.95f);
    int slot = t->n_fish;
    tank_make_fish(t, slot, preset, soc, bold, STAGE_FRY);
    /* its look is the family's, not the preset's (2026-09-14): the body from
       one parent, the markings from the other - a coin decides which is
       which; tank_set_look keeps the markings off a matching body */
    if (tank_randf(t, 0, 1) < 0.5f) { int x = ia; ia = ib; ib = x; pa = &t->fish[ia]; pb = &t->fish[ib]; }
    tank_set_look(t, slot, pa->color, pb->accent);
    t->fish[slot].parent_a = (int8_t)ia; t->fish[slot].parent_b = (int8_t)ib;
    place_near_reef(t, &t->fish[slot]);
    t->fish[slot].hunger = 4; t->fish[slot].trust = 4;
    t->n_fish++;
    return slot;
}

void tank_init(tank_t *t, uint32_t seed) {
    t->rng = seed ? seed : 0xC0FFEE;
    t->n_fish = 0;                 /* progression_boot restores or calls tank_new_population */
    for (int i = 0; i < MAX_FOOD; i++) t->food[i].alive = false;
    t->bubble_x = BUBBLE_X_DEFAULT; t->bubble_y = TANK_H * 0.5f;   /* matches gen_traces.py; setup may move x */
    for (int i = 0; i < MAX_BUBBLE; i++) {
        bubble_t *b = &t->bubble[i];
        b->column = i < 10;
        b->x = b->column ? t->bubble_x + tank_randf(t, -10, 10) : tank_randf(t, 12, TANK_W - 12);
        b->y = tank_randf(t, 0, TANK_H);
        b->vy = tank_randf(t, 14, 30);
        b->wobble = tank_randf(t, 0, TAU);
    }
    t->reef_x   = TANK_W * 0.15f; t->reef_y   = TANK_H * 0.85f;
    t->clock = 0; t->night = false; t->idle_s = 0;
    t->light_idle_s = LIGHT_IDLE_S; t->light_auto = false; t->light_manual_off = false;
    t->fish_speed = 1;                                   /* as it always was */
    t->light_override = false; t->light_on = true;
    t->hold_active = false; t->hold_time = 0; t->hold_approached = false;
    t->tap_count = 0; t->tap_burst_t = 99; t->startled = false;
    t->startle_cooldown = 0;
    t->feed_spot_x = -1; t->player_feedings = 0; t->feed_open = false; t->hold_approaches = 0; t->greet_timer = 0;
    t->courting = false; t->court_a = t->court_b = -1;
    t->court_cool = 30; t->court_active = 0;
    t->ravenous = false; t->trickle_off = false;
    t->stage_fish = -1; t->hold_light = false;
    t->drag_active = false; t->drag_has_prev = false; t->drag_dist = 0;
    for (int b = 0; b < VEG_BEDS_MAX; b++) {
        /* a fresh tank's canopy has a natural profile: fronds within +-0.04
           of VEG_START, seeded per slot so it's the same tank every boot */
        for (int i = 0; i < VEG_FRONDS_MAX; i++) {
            uint32_t h = (uint32_t)((b * 17 + i + 1) * 2654435761u);
            t->veg_h[b][i] = VEG_START + ((int)(h >> 8 & 255) - 128) / 128.0f * 0.04f;
        }
    }
    veg_sync(t);
    t->slash_armed = t->slash_engaged = t->slash_cut = false;
    t->slash_h = t->slash_v = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) t->algae[i] = 0;
    t->algae_acc = 0; t->trims = 0; t->cells_cleaned = 0;
    t->algae_colonies = 0; t->trim_px = 0;
    t->sd_balance = t->sd_earned = 0; t->sd_unlocks = 0; t->sd_stowed = 0;
    for (int i = 0; i < N_FISH_MAX; i++) t->sd_paid_fish[i] = 0;
    t->sd_colonies_paid = t->sd_inches_paid = 0;
    t->snail_x = -1; t->snail_y = -1; t->snail_heading = 0; t->snail_cell = -1; t->snail_graze = 0;
    t->snail_grazed = 0;
    for (int i = 0; i < SD_ITEM_COUNT; i++) { t->decor_x[i] = 0; t->decor_z[i] = DECOR_Z_MIDDLE; }
    t->decor_z[SD_IDX_CASTLE] = DECOR_Z_FRONT;      /* the castle has no AMONG; it starts swim-through */
    t->bass_drop_at = 0;
    for (int i = 0; i < GLOW_N; i++) { t->glow[i].x = t->glow[i].y = 0; t->glow[i].carrier = -1; t->glow[i].held_s = 0; t->glow[i].vx = t->glow[i].vy = t->glow[i].spin = t->glow[i].ang = 0; }
    for (int i = 0; i < N_FISH_MAX; i++) s_glow_cool[i] = 0;
    t->totem_carrier = -1; t->totem_held_s = 0; t->totem_phase = TOTEM_OFF;
    t->diver_stick = -1;
    t->totem_planted = false; t->totem_party_x = t->totem_party_ang = 0; s_totem_cool = 0;
    t->disco_drop = t->disco_spin = t->disco_show_s = 0;
    for (int i = 0; i < N_FISH_MAX; i++) { s_fish_ev[i].ev = -1; s_fish_ev[i].clock = 0; }   /* a fresh tank has no history */
    s_emit_clock = 0;
    t->tank_ms_bits = 0; t->tank_ms_seen = 0; t->ask_rr = 0; t->advisor_asks = 0;
    tank_scatter_food(t, 2);
}

#define TAP_WINDOW      0.5f    /* taps closer than this form a burst */
#define STARTLE_RADIUS  140.0f  /* fish this close to an aggressive tap bolt */
#define STARTLE_COOLDOWN 6.0f   /* calm seconds before the spook wears off */
#define HOLD_RADIUS     1000.0f /* a resting finger is seen from anywhere on the
                                 * glass (2026-09-04, was 160: a fish only came
                                 * when you held next to it, which hid the
                                 * trust tell) */
#define HOLD_ATTRACT_S  2.7f    /* hold_time before fish approach; platforms
                                 * report holds ~0.3 s in, so ≈3 s of contact */
#define HOLD_HUNGER_VETO 7.5f   /* this hungry, a fish ignores the finger */
#define HOLD_APPROACH_FROM 60.0f /* a hold-approach must START at least this far out:
                                 * a fish already under the finger earns nothing */
#define HOLD_APPROACH_AT   30.0f /* ... and come in this close */

/* ---- hunger economy (2026-09-01) ----
 * The prototype's per-second metabolism (0.15 + 0.12*bold: fed to starving
 * in under a minute) shipped unchanged into a tank the keeper tends for a
 * few minutes at a time - and the trickle that fed the prototype's single
 * fish was scaled per FRAME, so at the device's 25 fps it dropped 2.4x fewer
 * pellets than the 60 fps sim and could never keep up with four fish: the
 * school lived at hunger 9-10, begging under the surface (Strato: "ravenous
 * too often, hovering near the top, breaking the experience").
 * Now: a meal lasts ~5-6 minutes of awake time, and the tank's own trickle is
 * HUNGER-GATED and per-second - it drops a pellet only while somebody is
 * really hungry (a few seconds' wait), so an untended tank cycles between
 * "peckish" and "just fed" on its own and never reaches the ravenous
 * threshold (8.5, progression.c) awake. Ravenous begging is once again the
 * after-sleep / long-absence event it was designed to be (tank_tick_sleep's
 * 0.8/h), and the keeper's pellets are what make a fish FULL - play, bubbles,
 * the reef, a friend: the fed-fish repertoire. The advisor sees only banded
 * hunger, so its training distribution is untouched. */
#define HUNGER_PER_S       0.010f   /* fed -> starving in ~17 min (shy fish) */
#define HUNGER_BOLD_PER_S  0.008f   /* bold fish burn hotter: ~9 min */
#define HUNGER_DART_PER_S  0.012f   /* play costs extra */
#define TRICKLE_HUNGER     7.5f     /* the tank feeds itself only past this (someone) */
#define TRICKLE_RATE       0.15f    /* pellets/s while the gate is open (~7 s wait) */
#define CURIOSITY_SPEND_PER_S 0.25f /* spent at the bubbles / the reef: 9 -> 3 in ~24 s */
#define CURIOSITY_SPEND_RADIUS 60.0f

/* ---- upkeep: the vegetation keeps growing, algae films the glass ----
 * Vegetation is a comfort system, not scenery: fish swim slower inside the
 * canopy and their stress (already in the advisor's schema) answers to it -
 * cover calms (a canopy is a place to hide), a tank being smothered (two
 * beds past VEG_SMOTHER) presses hard, a completely scalped tank is a mild
 * unease that lifts as soon as one tuft regrows. Rates are per real second;
 * tank_tick runs them awake, tank_tick_sleep runs them faster (an untended
 * dark tank is where the garden gets away from you). */
#define VEG_GROW_AWAKE_S    108000.0f /* nubs -> full canopy in ~30 h awake */
#define VEG_GROW_SLEEP_S    36000.0f  /* ~10 h of drowse */
#define VEG_SWORD_GROW      1.25f     /* the sword plant grows a bit faster than the
                                       * grass (nubs -> full in ~24 h awake / ~8 h asleep) */
#define VEG_SEG_PX          3.2f      /* render.c VEG_SEG_DY: px of height per segment */
#define VEG_SLOW            0.60f     /* cruise speed factor inside a canopy */
#define ALGAE_STEP_AWAKE_S  240.0f    /* one film growth step per 4 min awake */
#define ALGAE_STEP_SLEEP_S  120.0f
#define ALGAE_COVER_CAP     0.30f     /* growth stops claiming new cells here */
#define WIPE_RADIUS      20.0f  /* squeegee half-width around the drag path */
#define WIPE_ENGAGE_PX   18.0f  /* stroke travel before a drag starts wiping
                                 * (a rolly fingertip tap stays under this) */
#define SLASH_PX         16.0f  /* sideways travel before a stroke is scissors (a
                                 * rolly fingertip tap stays under this; one
                                 * frond pitch is 12 px, so a flick takes 1-2) */
#define SLASH_RATIO      1.5f   /* ... and it must be this much more h than v */
#define SLASH_START_PX   10.0f  /* a slash must START this close to a FROND's tip
                                 * (upward) - not the bed's box: a cleaning scrub
                                 * begun mid-glass over a tall bed used to arm the
                                 * scissors (Strato, 2026-09-04) */
#define SLASH_START_SIDE_PX 16.0f /* ... and this close to a spine SIDEWAYS: the pad
                                 * of a fingertip is ~6 mm = 75 px on this glass, so
                                 * a landing a pitch beside a bed's outer frond has
                                 * that frond under the finger (a stroke begun at
                                 * the glass, heading in, used to arm nothing) */
#define SLASH_REACH_PX    8.0f  /* the fingertip's reach past its REPORTED point: a
                                 * stroke cuts the frond up to this far beyond where
                                 * it lands and where it lifts. The touch point is
                                 * the pad's centre; the outer frond of a bed stands
                                 * 24 px from the glass and the centre stops short of
                                 * it, so "the last blade never trips" (Strato,
                                 * 2026-09-14). Two thirds of the 12 px pitch: a flick
                                 * still takes only the fronds it visibly covers. */

static int popcount32u(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

/* canopy geometry: one source of truth for render, the trim hit test and the
 * in-canopy slowdown. Bed 0 is the reef bed (milestone lushness widens its
 * base, as ever); growth adds fronds (out, wider) and segments (up, toward
 * the surface). Height IS growth (2026-09-04): every bed's tallest frond
 * stands g of the way from the floor to the surface - at growth 1 it
 * touches the tank ceiling, whichever bed it is - so "85% grown" and "85%
 * of the tank's height" are the same thing for the comfort band below. At
 * VEG_NUB the fronds are ~5 segment green stubble. */
static int veg_segs(float h) { return 2 + (int)(h * (VEG_SEGS_FULL - 2)); }
/* frond pitch per bed: grass at 12 px; the sword plant's broad leaves at 14 */
static int veg_pitch(int b) { return b == 3 ? 14 : 12; }
static void veg_bed_base(const tank_t *t, int b, float *bx0, int *n) {
    int lush = popcount32u(t->tank_ms_bits); if (lush > 4) lush = 4;
    int base_n;
    if (b == 0)      { *bx0 = t->reef_x - 24 - lush * 6; base_n = 5 + lush; }
    else if (b == 1) { *bx0 = TANK_W * 0.84f; base_n = 4; }
    else if (b == 2) { *bx0 = TANK_W * 0.62f; base_n = 2; }
    else             { *bx0 = tank_decor_x(t, 0) - PLANT_HALF_W; *n = 4; return; }   /* the sword plant
                                                              (2026-09-15): four broad leaves, by default on
                                                              the open floor between the reef bed's widest
                                                              reach (~199) and bed 2 (272) - the keeper's
                                                              to move (tank_decor_set) */
    /* the frond count is fixed per bed now (it used to widen with growth):
       with fronds cut one at a time, a bed's outer fronds can't be allowed
       to vanish because its MEAN height dropped */
    int nn = base_n + 6;
    if (nn > VEG_FRONDS_MAX) nn = VEG_FRONDS_MAX;
    while (nn > 1 && *bx0 + nn * 12 > TANK_W - 8) nn--;   /* beds stop at the glass */
    *n = nn;
}
int tank_veg_beds(const tank_t *t) { return tank_bit_live(t, SD_ITEM_PLANT) ? VEG_BEDS_MAX : VEG_BEDS; }
veg_kind_t tank_veg_kind(const tank_t *t, int b) { (void)t; return b == 3 ? VEG_KIND_SWORD : VEG_KIND_GRASS; }
void tank_veg_bed(const tank_t *t, int b, float *x0, float *x1, float *top_y, int *fronds) {
    float bx0; int n;
    veg_bed_base(t, b, &bx0, &n);
    float hmax = 0;
    for (int i = 0; i < n; i++) if (t->veg_h[b][i] > hmax) hmax = t->veg_h[b][i];
    if (x0) *x0 = bx0 - 6;
    if (x1) *x1 = bx0 + n * veg_pitch(b) + 6;
    if (top_y) *top_y = TANK_H - 16 - veg_segs(hmax) * VEG_SEG_PX - 4;
    if (fronds) *fronds = n;
}
int tank_nursery_bed(const tank_t *t) {
    int best = -1;
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (t->veg_growth[b] >= VEG_NURSERY && (best < 0 || t->veg_growth[b] > t->veg_growth[best])) best = b;
    return best;
}
/* where the pair circles: low inside the nursery bed, a flat loop that weaves
 * the fronds (body centre ~14 px off the floor line) */
static void court_site(const tank_t *t, float *cx, float *cy, float *rx) {
    int b = tank_nursery_bed(t);
    if (b < 0) { *cx = t->reef_x + 26; *cy = TANK_H - 78; *rx = 26; return; }   /* legacy spot */
    float x0, x1; tank_veg_bed(t, b, &x0, &x1, NULL, NULL);
    *cx = (x0 + x1) * 0.5f; *cy = TANK_H - 16 - 14;
    float half = (x1 - x0) * 0.5f - 8; *rx = half < 16 ? 16 : half > 30 ? 30 : half;
}
int tank_veg_frond(const tank_t *t, int b, int i, float *x) {
    float bx0; int n;
    veg_bed_base(t, b, &bx0, &n);
    if (x) *x = bx0 + i * veg_pitch(b);
    return veg_segs(t->veg_h[b][i]);
}
/* veg_growth[b] is the bed's mean frond height - the comfort band, the save
 * and the firmware log read it; recomputed after anything moves a frond */
static void veg_sync(tank_t *t) {
    for (int b = 0; b < VEG_BEDS_MAX; b++) {
        float bx0; int n; veg_bed_base(t, b, &bx0, &n);
        float sum = 0;
        for (int i = 0; i < n; i++) sum += t->veg_h[b][i];
        t->veg_growth[b] = sum / n;
    }
}
static void veg_grow(tank_t *t, float dg) {
    for (int b = 0; b < tank_veg_beds(t); b++) {
        float dgb = tank_veg_kind(t, b) == VEG_KIND_SWORD ? dg * VEG_SWORD_GROW : dg;
        for (int i = 0; i < VEG_FRONDS_MAX; i++)
            t->veg_h[b][i] = fminf(1, t->veg_h[b][i] + dgb);
    }
    veg_sync(t);
}
void tank_veg_sync(tank_t *t) { veg_sync(t); }
void tank_veg_set(tank_t *t, int b, float g) {
    for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[b][i] = g;
    veg_sync(t);
}

/* is (x,y) inside bed b's canopy? */
/* is (x,y) within `side` px of some frond of bed b sideways, and no more than
 * `up` px above its tip? The slash arms only from here. */
static bool veg_near_frond(const tank_t *t, int b, float x, float y, float side, float up) {
    float x0, x1; int n;
    tank_veg_bed(t, b, &x0, &x1, NULL, &n);
    if (x < x0 - side || x > x1 + side) return false;
    for (int i = 0; i < n; i++) {
        float fx; int segs = tank_veg_frond(t, b, i, &fx);
        float tip = TANK_H - 16 - segs * VEG_SEG_PX;
        if (fabsf(x - fx) <= side && y >= tip - up) return true;
    }
    return false;
}
static bool veg_inside(const tank_t *t, int b, float x, float y) {
    float x0, x1, ty;
    tank_veg_bed(t, b, &x0, &x1, &ty, NULL);
    return x >= x0 && x <= x1 && y >= ty;
}

/* the scissors: cut every frond whose spine the stroke segment (x0,y0)-(x1,y1)
 * crosses, to the height where it crosses (only ever DOWN, never below nubs).
 * The segment reaches SLASH_REACH_PX past its far end - the finger is a pad,
 * not a point, and the frond just ahead of the reported point at lift is
 * under it (mid-stroke the next segment crosses that frond anyway, at the
 * same height); `landing` (the retroactive first segment of a stroke) reaches
 * the same distance back past its start, for the frond under the finger as
 * it touched down. A frond in the reach is cut at the height of that end.
 * Returns the number of fronds cut. */
static int veg_cut(tank_t *t, float x0, float y0, float x1, float y1, bool landing) {
    int cuts = 0;
    float lo = x0 < x1 ? x0 : x1, hi = x0 < x1 ? x1 : x0;
    if (x1 >= x0) hi += SLASH_REACH_PX; else lo -= SLASH_REACH_PX;
    if (landing) { if (x1 >= x0) lo -= SLASH_REACH_PX; else hi += SLASH_REACH_PX; }
    float dx = x1 - x0;
    for (int b = 0; b < tank_veg_beds(t); b++) {
        float bx0; int n; veg_bed_base(t, b, &bx0, &n);
        for (int i = 0; i < n; i++) {
            float fx = bx0 + i * veg_pitch(b);
            if (fx < lo || fx > hi) continue;
            float u = fabsf(dx) > 0.001f ? clampf((fx - x0) / dx, 0, 1) : 0;
            float cy = y0 + (y1 - y0) * u;
            float hf = (TANK_H - 16 - cy) / ((VEG_SEGS_FULL - 1) * VEG_SEG_PX);
            if (hf < VEG_NUB) hf = VEG_NUB;
            if (hf >= t->veg_h[b][i]) continue;    /* the stroke passed above the tip */
            t->trim_px += (t->veg_h[b][i] - hf) * (VEG_SEGS_FULL - 1) * VEG_SEG_PX;   /* the inches (sand dollars) */
            t->veg_h[b][i] = hf;
            cuts++;
            int puffs = 2;                         /* cut leaves drift up */
            for (int k = 0; k < MAX_BUBBLE && puffs > 0; k++) {
                if (t->bubble[k].column) continue;
                t->bubble[k].x = fx + tank_randf(t, -4, 4);
                t->bubble[k].y = cy - tank_randf(t, 0, 6);
                puffs--;
            }
        }
    }
    if (cuts) veg_sync(t);
    return cuts;
}

float tank_algae_cover(const tank_t *t) {
    int covered = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) covered += t->algae[i] > 0;
    return (float)covered / ALGAE_CELLS;
}

/* one film step: thicken a covered cell, or claim a fresh one (preferring
 * cells next to existing film, then the glass edges - the way a real tank
 * fouls from the corners in) until the dapple cap is reached */
void tank_grow_algae(tank_t *t, int steps) {
    for (int s = 0; s < steps; s++) {
        int covered = 0;
        for (int i = 0; i < ALGAE_CELLS; i++) covered += t->algae[i] > 0;
        bool claim = covered < (int)(ALGAE_CELLS * ALGAE_COVER_CAP);
        for (int try = 0; try < 8; try++) {
            int cx = (int)tank_randf(t, 0, ALGAE_COLS - 0.001f);
            int cy = (int)tank_randf(t, 0, ALGAE_ROWS - 0.001f);
            uint8_t *cell = &t->algae[cy * ALGAE_COLS + cx];
            if (*cell) { *cell = (uint8_t)(*cell > 210 ? 255 : *cell + 45); break; }
            if (!claim) continue;
            bool near = false;
            for (int dy = -1; dy <= 1 && !near; dy++)
                for (int dx = -1; dx <= 1 && !near; dx++) {
                    int nx = cx + dx, ny = cy + dy;
                    if (nx >= 0 && nx < ALGAE_COLS && ny >= 0 && ny < ALGAE_ROWS)
                        near = t->algae[ny * ALGAE_COLS + nx] > 0;
                }
            bool edge = cx == 0 || cy == 0 || cx == ALGAE_COLS - 1 || cy == ALGAE_ROWS - 1;
            float p = near ? 1.0f : edge ? 0.5f : 0.10f;
            if (tank_randf(t, 0, 1) < p) { *cell = 90; break; }
        }
    }
}

/* label the film's connected patches (8-neighbour): lab[i] = 1.. per cell
 * with film, 0 without; returns the patch count. A 28 x 23 grid, an
 * iterative flood fill - a few thousand steps, once per wiping frame. */
static int algae_label(const uint8_t *algae, uint8_t *lab) {
    int n = 0;
    int16_t stack[ALGAE_CELLS];
    for (int i = 0; i < ALGAE_CELLS; i++) lab[i] = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) {
        if (!algae[i] || lab[i]) continue;
        if (n >= 255) break;                               /* the label is a byte; the cap is 30% cover anyway */
        n++; int sp = 0; stack[sp++] = (int16_t)i; lab[i] = (uint8_t)n;
        while (sp) {
            int c = stack[--sp], cx = c % ALGAE_COLS, cy = c / ALGAE_COLS;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || nx >= ALGAE_COLS || ny < 0 || ny >= ALGAE_ROWS) continue;
                    int j = ny * ALGAE_COLS + nx;
                    if (algae[j] && !lab[j]) { lab[j] = (uint8_t)n; stack[sp++] = (int16_t)j; }
                }
        }
    }
    return n;
}
/* wipe the film in a WIPE_RADIUS band around the segment (x0,y0)-(x1,y1).
 * A patch whose last cell goes under the keeper's stroke is a COLONY removed
 * (tank_t.algae_colonies, the sand dollars' chore count, 2026-09-15). */
static void wipe_algae(tank_t *t, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    uint8_t lab[ALGAE_CELLS]; int patches = -1;             /* labelled lazily: only a stroke that clears something pays the fill */
    int wiped = 0;
    for (int cy = 0; cy < ALGAE_ROWS; cy++)
        for (int cx = 0; cx < ALGAE_COLS; cx++) {
            uint8_t *cell = &t->algae[cy * ALGAE_COLS + cx];
            if (!*cell) continue;
            float px = cx * ALGAE_CELL + ALGAE_CELL * 0.5f;
            float py = cy * ALGAE_CELL + ALGAE_CELL * 0.5f;
            float u = len2 > 1 ? clampf(((px - x0) * dx + (py - y0) * dy) / len2, 0, 1) : 0;
            if (tank_dist(px, py, x0 + dx * u, y0 + dy * u) < WIPE_RADIUS) {
                if (patches < 0) patches = algae_label(t->algae, lab);
                *cell = 0;
                t->cells_cleaned++; wiped++;
            }
        }
    if (wiped && patches > 0) {
        bool alive[256] = { false };
        for (int i = 0; i < ALGAE_CELLS; i++) if (t->algae[i] && lab[i]) alive[lab[i]] = true;
        for (int p = 1; p <= patches; p++) {
            bool touched = false;                          /* only a patch this stroke reached can have gone */
            for (int i = 0; i < ALGAE_CELLS && !touched; i++) touched = lab[i] == p && !t->algae[i];
            if (touched && !alive[p]) t->algae_colonies++;
        }
    }
}

/* ---- the snail (2026-09-15, the shop's algae control) ----
 * A rule-based creature on the GLASS: it crawls toward the nearest film cell
 * and grazes it thin, cell by cell; with the glass clean it ambles along the
 * edge. The model never sees it (schema v4 is frozen) - it reaches the fish
 * the way the film does, through cover and calm. Its grazing is not the
 * keeper's chore: cells_cleaned and the colonies never count it. */
#define SNAIL_PX_S        4.0f     /* crawl toward a patch (6 was "a bit fast" - Strato) */
#define SNAIL_AMBLE_PX_S  3.0f     /* nothing to eat: the edge walk */
#define SNAIL_GRAZE_PER_S 45.0f    /* film units per second on a cell (a fresh 90 cell in 2 s) */
#define SNAIL_SLEEP_CELLS_PER_H 20 /* the night shift, coarse (tank_tick_sleep) */
#define SNAIL_MARGIN      30.0f    /* it keeps inside the visible window: the panel's corners are
                                    * rounded and the bezel curve hides the outer ~24 px (Strato,
                                    * 2026-09-15: "stuck in the bottom left corner, I can barely
                                    * see it" - the film seeds from the corners and it went there) */
#define SNAIL_REACH       16.0f    /* it grazes a cell from this close (one cell): a corner cell
                                    * under the bezel is eaten from the visible edge */
static int snail_nearest_cell(const tank_t *t) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < ALGAE_CELLS; i++) {
        if (!t->algae[i]) continue;
        float cx = (i % ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f, cy = (i / ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f;
        float d = tank_dist(t->snail_x, t->snail_y, cx, cy);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
bool tank_snail_upright(const tank_t *t) { return t->snail_cell < 0 && t->snail_y >= SNAIL_FLOOR_Y - 1; }
#define SNAIL_TAP_RADIUS 48.0f     /* generous round the 32 px sprite: a fingertip on this 322 ppi
                                    * panel covers ~60 px, and the snail is small and low on the glass
                                    * (Strato, 2026-09-16: "challenging to tap the little guy" at 30;
                                    * the fish take 38 and are tested first) */
bool tank_snail_hit(const tank_t *t, float x, float y) {
    if (!tank_bit_live(t, SD_ITEM_SNAIL) || t->snail_x < 0) return false;
    return tank_dist(t->snail_x, t->snail_y, x, y) <= SNAIL_TAP_RADIUS;
}
static void snail_tick(tank_t *t, float dt) {
    if (!tank_bit_live(t, SD_ITEM_SNAIL)) return;
    if (t->snail_x < 0) tank_snail_place(t);
    if (t->snail_cell < 0 || !t->algae[t->snail_cell]) { t->snail_cell = (int16_t)snail_nearest_cell(t); t->snail_graze = 0; }
    if (t->snail_cell >= 0) {
        float cx = (t->snail_cell % ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f;
        float cy = (t->snail_cell / ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f;
        float tx = clampf(cx, SNAIL_MARGIN, TANK_W - SNAIL_MARGIN), ty = clampf(cy, SNAIL_MARGIN, TANK_H - SNAIL_MARGIN);
        float d = tank_dist(t->snail_x, t->snail_y, tx, ty);      /* to the nearest point it may stand on */
        if (d > 3 && tank_dist(t->snail_x, t->snail_y, cx, cy) > SNAIL_REACH) {
            t->snail_heading = atan2f(ty - t->snail_y, tx - t->snail_x);
            float step = fminf(d, SNAIL_PX_S * dt);
            t->snail_x += cosf(t->snail_heading) * step; t->snail_y += sinf(t->snail_heading) * step;
        } else {                                            /* grazing: the film thins under it */
            t->snail_graze += dt;
            int v = t->algae[t->snail_cell] - (int)(SNAIL_GRAZE_PER_S * dt + 0.5f);
            t->algae[t->snail_cell] = (uint8_t)(v < 0 ? 0 : v);
            if (v <= 0) t->snail_grazed++;                  /* a cell eaten clean: its card's tally */
        }
    } else if (t->snail_y < SNAIL_FLOOR_Y - 1) {            /* clean glass: down to the floor, flat on the glass, head down.
                                                               A hair off straight down keeps the sideways facing it had
                                                               (cos's sign) for the floor walk it lands in. */
        t->snail_heading = 1.5708f + (cosf(t->snail_heading) < 0 ? 0.001f : -0.001f);
        t->snail_y = fminf(SNAIL_FLOOR_Y, t->snail_y + SNAIL_PX_S * dt);
    } else {                                                /* the floor walk, upright: along the bottom, a rest now and
                                                               then, a turn at each end (the facing lives in snail_heading) */
        t->snail_y = SNAIL_FLOOR_Y;
        bool resting = fmodf(t->clock, 24.0f) < 5.0f;
        if (!resting) {
            float dir = cosf(t->snail_heading) < 0 ? -1.0f : 1.0f;
            t->snail_x += dir * SNAIL_AMBLE_PX_S * dt;
            if (t->snail_x <= SNAIL_MARGIN)          { t->snail_x = SNAIL_MARGIN;          t->snail_heading = 0; }
            if (t->snail_x >= TANK_W - SNAIL_MARGIN) { t->snail_x = TANK_W - SNAIL_MARGIN; t->snail_heading = 3.14159f; }
        }
    }
    t->snail_x = clampf(t->snail_x, SNAIL_MARGIN, TANK_W - SNAIL_MARGIN);
    t->snail_y = clampf(t->snail_y, SNAIL_MARGIN, SNAIL_FLOOR_Y);
}
/* asleep: the snail keeps working, coarsely - the nearest cells go, one by one */
static void snail_sleep(tank_t *t, float seconds) {
    if (!tank_bit_live(t, SD_ITEM_SNAIL)) return;
    if (t->snail_x < 0) tank_snail_place(t);
    int cells = (int)(seconds / 3600.0f * SNAIL_SLEEP_CELLS_PER_H);
    for (int k = 0; k < cells; k++) {
        int c = snail_nearest_cell(t);
        if (c < 0) break;
        t->algae[c] = 0; t->snail_grazed++;
        t->snail_x = clampf((c % ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f, SNAIL_MARGIN, TANK_W - SNAIL_MARGIN);
        t->snail_y = clampf((c / ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f, SNAIL_MARGIN, TANK_H - SNAIL_MARGIN);
    }
    t->snail_cell = -1;
}
void tank_snail_place(tank_t *t) {
    t->snail_x = SNAIL_MARGIN + 10; t->snail_y = SNAIL_FLOOR_Y;   /* on the floor, bottom left, facing right */
    t->snail_heading = 0; t->snail_cell = -1; t->snail_graze = 0;
}
void tank_plant_place(tank_t *t) {
    tank_veg_set(t, 3, VEG_START);                          /* a young plant; it grows from here */
}
void tank_castle_place(tank_t *t) {
    t->decor_x[SD_IDX_CASTLE] = 0;                  /* the default spot ... */
    t->decor_z[SD_IDX_CASTLE] = DECOR_Z_FRONT;      /* ... and the fish swim through */
}
/* the decor's spot and layer (see tank.h): one table row per shop item -
 * half the footprint (0 = not placeable), the default centre, whether the
 * piece HANGS from the surface instead of standing on the sand, and how many
 * depths it offers (the castle has no AMONG, so the bar shows two tiles). */
static const struct { float half_w, x_default; bool hangs; uint8_t z_count; } DECOR[SD_ITEM_COUNT] = {
    [SD_IDX_PLANT]  = { PLANT_HALF_W,  PLANT_X_DEFAULT,  false, DECOR_Z_N },
    [SD_IDX_SNAIL]  = { 0, 0, false, 0 },                      /* the snail goes where the film is */
    [SD_IDX_CASTLE] = { CASTLE_HALF_W, CASTLE_X_DEFAULT, false, 2 },
    [SD_IDX_LASER]  = { LASER_HALF_W,  LASER_X_DEFAULT,  true,  DECOR_Z_N },
    [SD_IDX_BASS]   = { BASS_HALF_W,   BASS_X_DEFAULT,   false, DECOR_Z_N },
    [SD_IDX_GLOW]   = { GLOW_HALF_W,   GLOW_X_DEFAULT,   false, DECOR_Z_N },
    [SD_IDX_TOTEM]  = { TOTEM_HALF_W,  TOTEM_X_DEFAULT,  false, DECOR_Z_N },
    [SD_IDX_DISCO]  = { DISCO_HALF_W,  DISCO_X_DEFAULT,  true,  DECOR_Z_N },
    [SD_IDX_CHEST]  = { CHEST_HALF_W,  CHEST_X_DEFAULT,  false, DECOR_Z_N },
    [SD_IDX_DIVER]  = { DIVER_HALF_W,  DIVER_X_DEFAULT,  false, DECOR_Z_N },   /* where he turns round */
};
static const uint32_t SD_BIT[SD_ITEM_COUNT] = { SD_ITEM_PLANT, SD_ITEM_SNAIL, SD_ITEM_CASTLE,
                                                SD_ITEM_LASER, SD_ITEM_BASS, SD_ITEM_GLOW, SD_ITEM_TOTEM,
                                                SD_ITEM_DISCO, SD_ITEM_CHEST, SD_ITEM_DIVER };
uint32_t tank_item_bit(int item) { return (item >= 0 && item < SD_ITEM_COUNT) ? SD_BIT[item] : 0; }
bool  tank_decor_placeable(int item) { return item >= 0 && item < SD_ITEM_COUNT && DECOR[item].half_w > 0; }
bool  tank_decor_hangs(int item) { return tank_decor_placeable(item) && DECOR[item].hangs; }
float tank_decor_half_w(int item) { return tank_decor_placeable(item) ? DECOR[item].half_w : 0; }
int   tank_decor_z_count(int item) { return tank_decor_placeable(item) ? DECOR[item].z_count : DECOR_Z_N; }
int   tank_decor_z_at(int item, int i) {            /* a two-depth piece: BEHIND, then IN FRONT */
    if (tank_decor_z_count(item) == 2) return i <= 0 ? DECOR_Z_BACK : DECOR_Z_FRONT;
    return i < 0 ? 0 : i >= DECOR_Z_N ? DECOR_Z_N - 1 : i;
}
int   tank_decor_z_index(int item, int z) {
    if (tank_decor_z_count(item) == 2) return z == DECOR_Z_BACK ? 0 : 1;
    return z;
}
float tank_decor_x(const tank_t *t, int item) {
    if (!tank_decor_placeable(item)) return 0;
    float x = t->decor_x[item] > 0 ? t->decor_x[item] : DECOR[item].x_default;
    /* clamped here rather than at the placement page alone, so a piece that
       GREW - the bass stack went from one cabinet to five (2026-09-21) - can
       never hang off the glass on a save that was legal when it was made. */
    float half = (float)DECOR[item].half_w;
    return clampf(x, DECOR_MARGIN + half, TANK_W - DECOR_MARGIN - half);
}
int tank_decor_z(const tank_t *t, int item) { return tank_decor_placeable(item) ? t->decor_z[item] : DECOR_Z_MIDDLE; }
float tank_decor_top_y(const tank_t *t, int item) {
    switch (item) {
    case SD_IDX_PLANT: { float top; tank_veg_bed(t, 3, NULL, NULL, &top, NULL); return top; }   /* the leaves' reach */
    case SD_IDX_LASER:  return 0;                                 /* hung under the surface: the beams reach the floor */
    case SD_IDX_CASTLE: return TANK_H - 16 - CASTLE_SPIRE_H;
    case SD_IDX_BASS:   return TANK_H - 16 - 30;
    case SD_IDX_GLOW:   return TANK_H - 16 - 16;
    case SD_IDX_TOTEM:  return TANK_H - 16 - TOTEM_H;
    case SD_IDX_DISCO:  return 0;                                 /* hung: the rays reach down from it */
    default: return TANK_H - 16;
    }
}
void tank_decor_set(tank_t *t, int item, float x, int z) {
    if (item < 0 || item >= SD_ITEM_COUNT) return;      /* explicit, so the writes below are provably in range */
    if (!tank_decor_placeable(item)) return;
    float half = tank_decor_half_w(item), lo = DECOR_MARGIN + half, hi = TANK_W - DECOR_MARGIN - half;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    if (z < 0) z = 0;
    if (z >= DECOR_Z_N) z = DECOR_Z_N - 1;
    if (tank_decor_z_count(item) == 2 && z == DECOR_Z_MIDDLE) z = DECOR_Z_FRONT;   /* no AMONG */
    t->decor_x[item] = x; t->decor_z[item] = (uint8_t)z;
    if (item == SD_IDX_GLOW && tank_bit_live(t, SD_ITEM_GLOW)) tank_glow_place(t);   /* the pile follows the finger */
}
/* ---- the glow sticks (SD_ITEM_GLOW): the fish play with them -----------
 * See the note in tank.h. The model is never told; it asks for DART_PLAY and
 * this is what that turns into when there is a stick within reach. */
/* the castle's silhouette, in the castle's own local x, matching render.c:
 * the pointed left tower (cone, apex 108 above the floor), the short far-left
 * tower (cone, apex 72), the crenellated right tower (flat rampart at 70) and
 * the gate wall's walk (flat at 58). Checked tallest-first where they overlap. */
float tank_castle_top_y(const tank_t *t, float x, bool *slide) {
    if (slide) *slide = false;
    if (!tank_bit_live(t, SD_ITEM_CASTLE)) return GLOW_REST_Y;
    const float FY = TANK_H - 16.0f;
    float lx = x - tank_decor_x(t, SD_IDX_CASTLE);
    if (lx >= -66 && lx <= -30) {                      /* the pointed tower: a cone, nothing stays on it */
        if (slide) *slide = true;
        return FY - 78 - 30 * (1.0f - fabsf(lx + 48) / 18.0f);
    }
    if (lx >= -90 && lx <= -56) {                      /* the short far-left tower: a cone too */
        if (slide) *slide = true;
        return FY - 46 - 26 * (1.0f - fabsf(lx + 73) / 17.0f);
    }
    if (lx >= 56 && lx <= 86) return FY - 70;          /* the right tower's crenellated rampart */
    if (lx >= -44 && lx <= 44) return FY - 58;         /* the gate wall's walk, between the merlons */
    return GLOW_REST_Y;
}
void tank_glow_place(tank_t *t) {
    static const float dx[GLOW_N]  = { -8, -2, 4, 9 };
    static const float ang[GLOW_N] = { -0.35f, 0.55f, -0.12f, 0.75f };
    float gx = tank_decor_x(t, SD_IDX_GLOW);
    for (int i = 0; i < GLOW_N; i++) {
        /* exactly ON the sand line, so a freshly laid pile reads as resting
           and a fish can take one at once (they used to sit a hair above it,
           which glow_tick correctly treated as still falling) */
        t->glow[i].x = gx + dx[i]; t->glow[i].y = tank_castle_top_y(t, gx + dx[i], NULL);   /* the sand, or the castle if the pile sits on it */
        t->glow[i].ang = ang[i]; t->glow[i].spin = 0; t->glow[i].vx = t->glow[i].vy = 0;
        t->glow[i].held_s = 0; t->glow[i].carrier = -1;
    }
}
/* the totem is a two-fin job: whoever is carrying it does not go picking
 * sticks up as well. A stick thrown TO it still lands - catch is welcome. */
static bool totem_hands_full(const tank_t *t, int i) {
    return t->totem_phase != TOTEM_OFF && !t->totem_planted && t->totem_carrier == i;
}
/* is this fish already holding one? (a fish carries at most a single stick) */
static bool glow_busy(const tank_t *t, int fish) {
    for (int g = 0; g < GLOW_N; g++) if (t->glow[g].carrier == fish) return true;
    return false;
}
static void glow_tick(tank_t *t, float dt) {
    if (!tank_bit_live(t, SD_ITEM_GLOW)) { s_rally = 0; s_catch_to = -1; s_catch_stick = -1; return; }
    for (int i = 0; i < N_FISH_MAX; i++) {
        if (s_glow_cool[i] > 0) s_glow_cool[i] -= dt;
        if (s_glow_want[i] > 0) s_glow_want[i] -= dt;
        if (s_glow_watch[i] > 0 && (s_glow_watch[i] -= dt) <= 0) s_glow_watched[i] = -1;
    }
    if (s_catch_s > 0 && (s_catch_s -= dt) <= 0) {          /* the pass went wide: the rally is over */
        s_catch_to = -1; s_catch_stick = -1; s_rally = 0;
    }
    /* the slow roll, so sticks get played with when nobody is watching. A
       bored, curious fish is the likeliest; a stressed one never is. Lights
       out doubles it - that is when a glow stick is worth having. */
    if ((s_roll_t += dt) >= GLOW_ROLL_S) {
        s_roll_t = 0;
        for (int i = 0; i < t->n_fish; i++) {
            const fish_t *f = &t->fish[i];
            if (s_glow_cool[i] > 0 || s_glow_want[i] > 0 || glow_busy(t, i)) continue;
            if (f->stress > 5.0f || f->hunger > 7.0f || totem_hands_full(t, i)) continue;
            /* temperament leans it, it does not decide it: a tank of quiet,
               incurious fish still plays, just less. Without the floor one
               roster in four never touched a stick in twenty minutes. */
            float keen = 0.40f + 0.60f * ((f->bored / 10.0f) * 0.5f + f->curiosity / 10.0f * 0.3f + f->sociable * 0.2f);
            if (tank_randf(t, 0, 1) < GLOW_ROLL_P * keen * (tank_afterhours(t) ? 3.2f : t->night ? 2.0f : 1.0f))
                s_glow_want[i] = GLOW_WANT_S;
        }
    }
    /* a party at the speaker: everybody wants one in their fin */
    if (tank_bass_party(t))
        for (int i = 0; i < t->n_fish; i++)
            if (!glow_busy(t, i) && s_glow_cool[i] <= 0 && s_glow_want[i] <= 0 && !totem_hands_full(t, i))
                s_glow_want[i] = GLOW_WANT_S;
    for (int g = 0; g < GLOW_N; g++) {
        glow_t *s = &t->glow[g];
        if (s->carrier == GLOW_CARRIER_DIVER) {             /* in the diver's glove */
            float dvx, dvy; int dvd;
            tank_diver_state(t, &dvx, &dvy, &dvd, NULL);
            s->x = dvx + dvd * 13.0f;                       /* held out in front of him */
            s->y = dvy - DIVER_H + 16.0f;
            s->ang = 0.5f;
            s->vx = s->vy = 0;
            s->held_s += dt;
            continue;                                       /* diver_tick decides when it goes */
        }
        if (s->carrier >= 0) {                              /* being carried */
            if (s->carrier >= t->n_fish) { s->carrier = -1; s->held_s = 0; continue; }
            const fish_t *f = &t->fish[s->carrier];
            s->x = f->x + cosf(f->heading) * 9.0f;          /* just under the mouth, turned the way it swims */
            s->y = f->y + 5.0f;
            s->ang = f->heading + 0.5f;
            s->held_s += dt;
            /* a pass: someone keen and empty-finned nearby, and this fish
               feels like sharing. The stick is thrown AT them and stays
               catchable for a moment - miss it and it just sinks. */
            /* the trip up comes FIRST: a fish only looks for someone to pass
               to once it has got the stick high, or has been carrying it long
               enough to have given up on that. Passing early was ending half
               the carries down in the weeds, and the long fall is the point. */
            bool up_there = s->y <= GLOW_RELEASE_Y || s->held_s > GLOW_CARRY_MAX_S * 0.5f;
            if (s_catch_stick != g && up_there && s->held_s > GLOW_THROW_MIN_S && s_catch_to < 0) {
                int mate = -1; float best = GLOW_THROW_REACH;
                for (int i = 0; i < t->n_fish; i++) {
                    if (i == s->carrier || glow_busy(t, i) || s_glow_cool[i] > 0) continue;
                    if (s_glow_want[i] <= 0 && t->fish[i].sociable < 0.35f) continue;
                    float d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y);
                    if (d < best) { best = d; mate = i; }
                }
                if (mate >= 0 && tank_randf(t, 0, 1) < GLOW_THROW_P * dt * 4.0f) {
                    const fish_t *m = &t->fish[mate];
                    float dx = m->x - s->x, dy = m->y - s->y, d = sqrtf(dx * dx + dy * dy);
                    if (d < 1.0f) d = 1.0f;
                    int who = s->carrier;
                    s->carrier = -1; s->held_s = 0;
                    s->vx = dx / d * 90.0f; s->vy = dy / d * 90.0f;
                    s->spin = tank_randf(t, -3.0f, 3.0f);
                    s_glow_cool[who] = 1.0f;                /* a beat before it can take one again */
                    s_catch_to = mate; s_catch_stick = g; s_catch_s = GLOW_CATCH_S;
                    s_glow_want[mate] = GLOW_WANT_S;        /* it has seen it coming */
                    continue;
                }
            }
            bool high    = s->y <= GLOW_RELEASE_Y;          /* got it up to the top: the whole point */
            /* Lights-out used to end a carry. It is exactly the wrong call:
               the dark is the best time to have one, so only a real fright
               makes a fish let go now. */
            bool rattled = t->startled || f->stress > 7.5f;
            if ((s->held_s > GLOW_CARRY_MIN_S && high) || s->held_s > GLOW_CARRY_MAX_S || rattled) {
                int who = s->carrier;
                s->carrier = -1; s->held_s = 0; s->vy = 0;
                s->vx = cosf(f->heading) * tank_randf(t, 5.0f, 16.0f);   /* let go by a moving fish: it keeps
                                                                            that sideways throw, and THAT is
                                                                            what walks the pile down the tank */
                s->spin = tank_randf(t, -1.4f, 1.4f);       /* it tumbles on the way down */
                s_glow_cool[who] = t->night ? GLOW_PLAY_COOL_NIGHT_S : GLOW_PLAY_COOL_S;
                if (high && !rattled) { t->fish[who].ms_bits |= MS_GLOW_TOSS; tank_emit(TEV_GLOW_PLAY, who); }
                if (!rattled) { s_glow_watch[who] = GLOW_WATCH_S; s_glow_watched[who] = g; }   /* follow it down */
            }
            continue;
        }
        if (s_catch_stick == g && s_catch_to >= 0 && s_catch_to < t->n_fish) {   /* in the air, towards someone */
            const fish_t *m = &t->fish[s_catch_to];
            if (tank_dist(s->x, s->y, m->x, m->y) <= GLOW_CATCH_REACH) {         /* caught */
                s->carrier = (int8_t)s_catch_to; s->held_s = 0; s->vx = s->vy = 0; s->spin = 0;
                int who = s_catch_to;
                s_glow_want[who] = 0; s_glow_cool[who] = 0;
                s_rally++;
                s_catch_to = -1; s_catch_stick = -1; s_catch_s = 0;
                tank_emit(s_rally >= GLOW_RALLY_GEM ? TEV_GLOW_RALLY : TEV_GLOW_CATCH, who);
                continue;
            }
        }
        bool on_cone; float top = tank_castle_top_y(t, s->x, &on_cone);
        /* `|| on_cone` matters: a stick that comes to within a whisker of a
           tower's point would otherwise count as RESTING and sit balanced on
           it forever. Nothing rests on a cone - while it is over one it is
           always falling, so it gets shed and carries on down. */
        if (s->y < top - 0.25f || on_cone) {                /* falling - to the sand, or onto the castle */
            /* A stick the diver has LOBBED is still going up: his air is
               under it, so it is not pulled to terminal speed yet - it slows,
               hangs at the top of its arc, and only then starts the long fall
               everyone watches. Without this the sink easing ate the throw in
               a third of a second and it never left the sand. */
            if (s->lob_s > 0) {
                s->lob_s -= dt;
                s->vy += GLOW_LOB_GRAV * dt;
                if (s->vy > 0 && s->lob_s > 0.2f) s->lob_s = 0.2f;   /* over the top: hand it back to gravity */
            } else
            s->vy += (GLOW_SINK_PX_S - s->vy) * (dt * 3.0f < 1.0f ? dt * 3.0f : 1.0f);
            s->y += s->vy * dt;
            s->x += s->vx * dt;
            {   /* the glass: a stick still carrying speed comes back off it */
                float lo = DECOR_MARGIN, hi = TANK_W - DECOR_MARGIN;
                if (s->x < lo || s->x > hi) {
                    s->x = s->x < lo ? lo : hi;
                    s->vx = -s->vx * GLOW_BOUNCE;
                    s->spin = -s->spin;
                }
                if (s->y < 12.0f && s->vy < 0) { s->y = 12.0f; s->vy = 0; }   /* and the surface */
            }
            s->vx -= s->vx * (dt * 0.7f);                                /* the water takes the throw out of it */
            s->x += sinf(t->clock * 1.7f + g * 1.3f) * dt * 7.0f;        /* ... and it wanders as it sinks */
            s->ang += s->spin * dt;
            if (s->y >= top) {
                if (on_cone) {                              /* a tower's point: it is shed, and keeps falling */
                    /* Pick a way off ONCE and keep it. The two pointed towers
                       sit side by side, so re-deciding every tick pushed the
                       stick left off one and right off the other, and it sat
                       ping-ponging in the valley between them forever. */
                    if (s->vx > -1.0f && s->vx < 1.0f) {
                        float apex = tank_decor_x(t, SD_IDX_CASTLE) + (s->x - tank_decor_x(t, SD_IDX_CASTLE) < -56 ? -73.0f : -48.0f);
                        s->vx = (s->x < apex ? -1.0f : 1.0f) * tank_randf(t, 20.0f, 34.0f);
                        s->spin = tank_randf(t, -2.2f, 2.2f);   /* it tumbles off */
                    }
                    s->y = top - 0.5f;
                } else {                                    /* a rampart, a wall walk, or the open sand */
                    s->y = top; s->vy = 0; s->vx = 0; s->spin = 0;
                    s->ang = tank_randf(t, -0.6f, 0.6f);
                    s->x = clampf(s->x, DECOR_MARGIN, TANK_W - DECOR_MARGIN);
                }
            }
            continue;
        }
        for (int i = 0; i < t->n_fish; i++) {               /* lying there: can a playing fish take it? */
            const fish_t *f = &t->fish[i];
            if (s_glow_cool[i] > 0) continue;
            /* keen OR already playing. The goal test alone almost never
               lined up with being beside a stick, which is how a pile could
               sit untouched all day with four fish in the tank. */
            if (s_glow_want[i] <= 0 && f->goal.id != GOAL_DART_PLAY) continue;
            if (glow_busy(t, i) || totem_hands_full(t, i)) continue;
            if (tank_dist(f->x, f->y, s->x, s->y) > GLOW_REACH) continue;
            s->carrier = (int8_t)i; s->held_s = 0;
            s_glow_want[i] = 0;                             /* it got one */
            break;
        }
    }
}

/* the totem event (tank.h): lift, march, party at the speaker, march home */
static bool totem_lead(const tank_t *t, float *x, float *y, float *stress);
bool tank_totem_carry(const tank_t *t, float *x, float *y) {
    if (t->totem_phase == TOTEM_OFF || t->totem_planted) return false;
    float lx, ly;
    if (!totem_lead(t, &lx, &ly, NULL)) return false;
    if (x) *x = lx;
    if (y) *y = ly - TOTEM_H * 0.55f;                   /* held aloft, over whoever has it */
    return true;
}
bool tank_totem_pose(const tank_t *t, float *x, float *y, float *ang, bool *carried) {
    float cx, cy;
    if (tank_totem_carry(t, &cx, &cy)) {
        if (x) *x = cx;
        if (y) *y = cy;
        if (ang) *ang = 0;
        if (carried) *carried = true;
        return true;
    }
    if (t->totem_planted) {                             /* slammed into the sand by the speaker */
        if (x) *x = t->totem_party_x;
        if (y) *y = (float)(TANK_H - 16 - TOTEM_H);
        if (ang) *ang = t->totem_party_ang;
        if (carried) *carried = false;
        return true;
    }
    return false;                                       /* home, upright, where the keeper put it */
}
bool tank_bass_party(const tank_t *t) { return t->totem_phase == TOTEM_HOLD || t->totem_phase == TOTEM_PLANTED; }
bool tank_club_possible(const tank_t *t) {
    return tank_bit_live(t, SD_ITEM_BASS) && tank_bit_live(t, SD_ITEM_TOTEM);
}
bool tank_club_audible(const tank_t *t) {
    return !t->club_off && tank_club_possible(t) && tank_bass_party(t);
}

/* the castle's gate: fish swim freely through everything in this tank, so
 * "through the gate" is purely a question of where the fish is - the opening
 * is CASTLE_ARCH_R either side of the castle's centre, from the sand up to
 * the top of the vault. */
bool tank_in_castle_gate(const tank_t *t, int fish) {
    if (!tank_bit_live(t, SD_ITEM_CASTLE) || fish < 0 || fish >= t->n_fish) return false;
    const fish_t *f = &t->fish[fish];
    float lx = f->x - tank_decor_x(t, SD_IDX_CASTLE), floor_y = TANK_H - 16.0f;
    return fabsf(lx) <= CASTLE_ARCH_R && f->y <= floor_y && f->y >= floor_y - (CASTLE_ARCH_S + CASTLE_ARCH_R);
}
static void gate_tick(tank_t *t) {
    for (int i = 0; i < t->n_fish; i++)
        if (!(t->fish[i].ms_bits & MS_CASTLE_GATE) && tank_in_castle_gate(t, i))
            t->fish[i].ms_bits |= MS_CASTLE_GATE;
}

/* ---- the disco ball (tank.h) ---- */
void tank_disco_state(const tank_t *t, float *x, float *y, float *drop, float *spin) {
    if (x) *x = tank_decor_x(t, SD_IDX_DISCO);
    if (y) *y = DISCO_TOP_Y + (DISCO_MID_Y - DISCO_TOP_Y) * t->disco_drop;
    if (drop) *drop = t->disco_drop;
    if (spin) *spin = t->disco_spin;
}
bool tank_disco_hit(const tank_t *t, float x, float y) {
    if (!tank_bit_live(t, SD_ITEM_DISCO)) return false;
    float bx, by; tank_disco_state(t, &bx, &by, NULL, NULL);
    return tank_dist(x, y, bx, by) <= DISCO_R + 12.0f;          /* the ball, plus a fingertip */
}
void tank_disco_toggle(tank_t *t) {
    if (!tank_bit_live(t, SD_ITEM_DISCO)) return;
    t->disco_show_s = t->disco_show_s > 0 ? 0.0f : DISCO_SHOW_S;   /* tap on, tap off */
}
static void disco_tick(tank_t *t, float dt) {
    if (!tank_bit_live(t, SD_ITEM_DISCO)) { t->disco_drop = 0; t->disco_show_s = 0; return; }
    if (t->disco_show_s > 0) t->disco_show_s -= dt;              /* the keeper's show times out */
    bool want = tank_bass_party(t) || t->disco_show_s > 0;        /* a party always wins */
    float target = want ? 1.0f : 0.0f;
    float step = dt / DISCO_DROP_S;
    if (t->disco_drop < target) t->disco_drop = t->disco_drop + step > target ? target : t->disco_drop + step;
    if (t->disco_drop > target) t->disco_drop = t->disco_drop - step < target ? target : t->disco_drop - step;
    if (t->disco_drop > 0.02f) {                                 /* it only turns once it is on its way down */
        t->disco_spin += DISCO_SPIN_RPS * dt * t->disco_drop;
        if (t->disco_spin > 1.0f) t->disco_spin -= 1.0f;
    }
}

/* Where the totem's carrier IS, whoever they are. The diver can lead the
 * parade now (TOTEM_CARRIER_DIVER), so nothing downstream may assume the
 * carrier is a row in t->fish. *stress comes back 0 for him: he does not
 * spook, and a parade he is leading ends on a fright in the tank, not his. */
static bool totem_lead(const tank_t *t, float *x, float *y, float *stress) {
    if (t->totem_carrier == TOTEM_CARRIER_DIVER) {
        if (!tank_bit_live(t, SD_ITEM_DIVER)) return false;
        float dx, dy; int dd; tank_diver_state(t, &dx, &dy, &dd, NULL);
        if (x) *x = dx + dd * 4.0f;                 /* shouldered, beside the helmet not through it */
        if (y) *y = dy - DIVER_H * 0.72f;           /* and high enough that its foot is in his glove */
        if (stress) *stress = 0.0f;
        return true;
    }
    if (t->totem_carrier < 0 || t->totem_carrier >= t->n_fish) return false;
    const fish_t *f = &t->fish[t->totem_carrier];
    if (x) *x = f->x;
    if (y) *y = f->y;
    if (stress) *stress = f->stress;
    return true;
}
static void totem_end(tank_t *t) {
    if (t->totem_phase != TOTEM_OFF) t->light_manual_off = t->totem_light_was;   /* the light goes back how it was */
    t->totem_phase = TOTEM_OFF; t->totem_planted = false;
    t->totem_carrier = -1; t->totem_held_s = 0;
    s_totem_cool = TOTEM_COOL_S;
}
/* one fish takes up the totem: the parade begins and the lights drop for it.
 * totem_tick reaches it through the social gate, tank_totem_force straight. */
static void totem_lift(tank_t *t, int i) {
    /* both hands on the pole: a stick already held is let go here, and
       glow_tick will not hand out another until the parade is over */
    for (int g = 0; g < GLOW_N; g++)
        if (t->glow[g].carrier == i) { t->glow[g].carrier = -1; t->glow[g].held_s = 0; }
    if (i == TOTEM_CARRIER_DIVER && t->diver_stick >= 0) {
        t->glow[t->diver_stick].carrier = -1; t->diver_stick = -1;
    }
    t->totem_carrier = (int8_t)i; t->totem_held_s = 0;
    t->totem_phase = TOTEM_WALK; t->totem_planted = false;
    /* the lights go out for it: a parade is a night-time thing */
    t->totem_light_was = t->light_manual_off;
    t->light_manual_off = true;
    if (i >= 0 && i < t->n_fish) t->fish[i].ms_bits |= MS_TOTEM_HOLD;   /* the diver earns no badges */
    tank_emit(TEV_TOTEM_LIFT, i >= 0 ? i : -1);
}
static void totem_tick(tank_t *t, float dt) {
    if (!tank_bit_live(t, SD_ITEM_TOTEM)) { if (t->totem_phase != TOTEM_OFF) totem_end(t); return; }
    if (s_totem_cool > 0) s_totem_cool -= dt;
    if (t->totem_phase != TOTEM_OFF) {
        float lx, ly, lstress;
        if (!totem_lead(t, &lx, &ly, &lstress)) { totem_end(t); return; }
        if (t->totem_carrier == TOTEM_CARRIER_DIVER) ly = TOTEM_PLANT_Y;   /* he walks the floor: depth is never the question */
        /* Lights-out does NOT end it any more - the dark is when the party is
           worth having. Only a real fright breaks it up. */
        if (t->startled || lstress > 7.5f) { totem_end(t); return; }
        t->totem_held_s += dt;
        bool speaker = tank_bit_live(t, SD_ITEM_BASS);
        float bx = tank_decor_x(t, SD_IDX_BASS), home = tank_decor_x(t, SD_IDX_TOTEM);
        switch (t->totem_phase) {
        case TOTEM_WALK:
            if (!speaker) {                              /* no speaker: a parade, then home */
                if (t->totem_held_s > TOTEM_PARADE_S) { t->totem_phase = TOTEM_HOME; t->totem_held_s = 0; }
            } else if (fabsf(lx - bx) < TOTEM_ARRIVE_PX || t->totem_held_s > TOTEM_WALK_MAX_S) {
                t->totem_phase = TOTEM_HOLD; t->totem_held_s = 0;   /* they made it: the party starts */
            }
            break;
        case TOTEM_HOLD:
            if (t->totem_held_s > TOTEM_HOLD_S) {        /* down it goes, with gusto */
                t->totem_planted = true;
                t->totem_party_x = clampf(lx, DECOR_MARGIN + TOTEM_HALF_W, TANK_W - DECOR_MARGIN - TOTEM_HALF_W);
                t->totem_party_ang = tank_randf(t, 0.12f, 0.26f) * (tank_randf(t, -1, 1) < 0 ? -1.0f : 1.0f);
                t->totem_phase = TOTEM_PLANTED; t->totem_held_s = 0;
                for (int i = 0; i < t->n_fish; i++) {         /* everyone who stayed for it was AT the party */
                    fish_t *g = &t->fish[i];
                    if (fabsf(g->x - bx) > 130.0f) continue;
                    if (g->parties < 30000) g->parties++;
                    g->ms_bits |= MS_BASS_PARTY;
                }
            }
            break;
        case TOTEM_PLANTED:
            if (t->totem_held_s > TOTEM_PLANTED_S) {     /* picked back up for the march home */
                t->totem_planted = false;
                t->totem_phase = TOTEM_HOME; t->totem_held_s = 0;
            }
            break;
        default:                                          /* TOTEM_HOME */
            /* it is PUT BACK, not dropped: the carrier has to reach the
               keeper's spot AND sink to planting depth. The cap is long and
               only catches a fish the model has taken elsewhere for good. */
            if ((fabsf(lx - home) < TOTEM_HOME_PX && fabsf(ly - TOTEM_PLANT_Y) < TOTEM_HOME_Y_PX) ||
                t->totem_held_s > TOTEM_HOME_MAX_S) totem_end(t);
            break;
        }
        return;
    }
    if (s_totem_invited >= 0 && s_totem_invited < t->n_fish && s_totem_invite_s > 0) {
        /* somebody was ASKED to get it: the bar, the cooldown and the hour
           all stand aside, but it still has to swim over and reach it */
        int i = s_totem_invited;
        const fish_t *f = &t->fish[i];
        float tx2 = tank_decor_x(t, SD_IDX_TOTEM), ty2 = TANK_H - 16 - TOTEM_H * 0.5f;
        /* an invited fish is AIMING at it, so it does not have to brush the
           pole the way a passer-by would - it reaches from further out */
        if (f->stress < 7.0f && tank_dist(f->x, f->y, tx2, ty2) <= TOTEM_REACH * 1.9f) {
            s_totem_cool = 0; s_totem_invited = -1; s_totem_invite_s = 0;
            totem_lift(t, i);
            return;
        }
    }
    if (s_totem_cool > 0 || t->night) return;            /* it is lifted by day; the party may run into the dark */
    float tx = tank_decor_x(t, SD_IDX_TOTEM), ty = TANK_H - 16 - TOTEM_H * 0.5f;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        /* a first-timer has to be really sociable; a fish that has been to
           parties before is keener, down to TOTEM_SOCIAL_FLOOR */
        float bar = TOTEM_SOCIAL_MIN - f->parties * TOTEM_PARTY_BONUS;
        if (bar < TOTEM_SOCIAL_FLOOR) bar = TOTEM_SOCIAL_FLOOR;
        if (f->sociable < bar || f->stress > 5.0f) continue;
        if (tank_dist(f->x, f->y, tx, ty) > TOTEM_REACH) continue;
        totem_lift(t, i);
        break;
    }
}
/* the lift itself, so the dev page can call for one without repeating it */
void tank_totem_force(tank_t *t) {
    if (!tank_bit_live(t, SD_ITEM_TOTEM) || t->totem_phase != TOTEM_OFF) return;
    int best = -1; float top = -1.0f;                 /* the most sociable fish gets it */
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].sociable > top) { top = t->fish[i].sociable; best = i; }
    if (best >= 0) { s_totem_cool = 0; totem_lift(t, best); }
}

/* ---- play: the keeper's nudge, and a new thing to look at (tank.h) ---- */
/* one stick leaves the sand at `ang`, `speed` px/s. Lifting it a hair off
 * whatever it was resting on is what puts it back in the falling branch. */
static void glow_launch(tank_t *t, int g, float ang, float speed) {
    glow_t *s = &t->glow[g];
    s->carrier = -1; s->held_s = 0;
    s->vx = cosf(ang) * speed;
    s->vy = sinf(ang) * speed;
    s->y -= 1.5f;
    s->spin = tank_randf(t, -4.0f, 4.0f);
}
int tank_glow_nudge(tank_t *t, float x, float y) {
    if (!tank_bit_live(t, SD_ITEM_GLOW)) return 0;
    int near[GLOW_N], n = 0;                             /* what is lying within reach of the tap */
    for (int g = 0; g < GLOW_N; g++) {
        const glow_t *s = &t->glow[g];
        if (s->carrier < 0 && s->vy <= 0.01f && tank_dist(s->x, s->y, x, y) <= GLOW_PILE_R) near[n++] = g;
    }
    if (n == 0) return 0;
    if (n > 1) {                                         /* a PILE: it goes everywhere */
        for (int i = 0; i < n; i++) {
            /* out and up, fanned so they do not all take the same line; the
               water and the walls take it out of them on the way down */
            float ang = -1.9f + (float)i / (float)(n - 1) * 1.4f + tank_randf(t, -0.25f, 0.25f);
            glow_launch(t, near[i], ang, tank_randf(t, GLOW_POP_MIN, GLOW_POP_MAX));
        }
    } else {                                             /* one on its own: it hops */
        glow_launch(t, near[0], tank_randf(t, -2.5f, -0.6f), tank_randf(t, GLOW_HOP_MIN, GLOW_HOP_MAX));
    }
    int called = 0;
    for (int n = 0; n < GLOW_NUDGE_N; n++) {             /* the nearest calm, empty-finned fish */
        int best = -1; float bd = GLOW_NUDGE_REACH;
        for (int i = 0; i < t->n_fish; i++) {
            if (glow_busy(t, i) || s_glow_want[i] > 0 || t->fish[i].stress > 6.0f) continue;
            if (totem_hands_full(t, i)) continue;              /* its fins are on the totem */
            float d = tank_dist(t->fish[i].x, t->fish[i].y, x, y);
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0) break;
        s_glow_want[best] = GLOW_WANT_S;
        s_glow_cool[best] = 0;                           /* asked for, so the cooldown is forgiven */
        called++;
    }
    return called;
}
bool tank_afterhours(const tank_t *t) { return t->night && s_after_s > 0; }
bool tank_totem_nudge(tank_t *t, float x, float y) {
    if (!tank_bit_live(t, SD_ITEM_TOTEM) || t->totem_phase != TOTEM_OFF) return false;
    float tx = tank_decor_x(t, SD_IDX_TOTEM), ty = TANK_H - 16 - TOTEM_H * 0.5f;
    if (tank_dist(x, y, tx, ty) > TOTEM_H * 0.5f + TOTEM_TAP_REACH) return false;
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < t->n_fish; i++) {
        if (t->fish[i].stress > 6.0f) continue;
        float d = tank_dist(t->fish[i].x, t->fish[i].y, tx, ty);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return true;                     /* the tap WAS on the totem; nobody is up for it */
    s_totem_invited = best; s_totem_invite_s = TOTEM_INVITE_S;
    return true;
}
void tank_decor_noticed(tank_t *t, int item) {
    if (!tank_decor_placeable(item)) return;
    s_decor_new = item; s_decor_new_s = DECOR_LOOK_S;
    (void)t;
}
int tank_glow_rally(const tank_t *t, int *catcher) {
    (void)t;
    if (catcher) *catcher = s_catch_to;
    return s_rally;
}

/* the bass stack (SD_ITEM_BASS): the thump is a picture (render.c reads the
 * clock); the DROP, every BASS_DROP_BEATS beats, shakes BASS_DROP_PUFFS of
 * the free bubbles out of the cone - the flirt's puff, from the sand */
static void bass_tick(tank_t *t) {
    if (!tank_bit_live(t, SD_ITEM_BASS)) return;
    if (t->clock < t->bass_drop_at) return;
    t->bass_drop_at = t->clock + BASS_DROP_BEATS * BASS_BEAT_S;
    float bx = tank_decor_x(t, SD_IDX_BASS), by = TANK_H - 16 - 12;
    int puffs = BASS_DROP_PUFFS;
    for (int i = 0; i < MAX_BUBBLE && puffs > 0; i++) {
        if (t->bubble[i].column) continue;
        t->bubble[i].x = bx + tank_randf(t, -8, 8);
        t->bubble[i].y = by - tank_randf(t, 0, 6);
        puffs--;
    }
}

/* ---- the treasure chest (tank.h) ----------------------------------------
 * A slow cycle: shut, the lid creaks up, it breathes bubbles, it shuts. The
 * keeper's tap opens it now and holds it open until the next tap. */
static void chest_puff(tank_t *t, float cx, float cy) {
    for (int i = 0; i < MAX_BUBBLE; i++) {
        if (t->bubble[i].column) continue;
        t->bubble[i].x = cx + tank_randf(t, -9, 9);
        t->bubble[i].y = cy;
        return;
    }
}
static void chest_tick(tank_t *t, float dt) {
    if (!tank_bit_live(t, SD_ITEM_CHEST)) { t->chest_phase = CHEST_SHUT; t->chest_t = 0; t->chest_held = false; return; }
    t->chest_t += dt;
    switch (t->chest_phase) {
    case CHEST_SHUT:
        if (t->chest_t > CHEST_WAIT_S) { t->chest_phase = CHEST_OPENING; t->chest_t = 0; }
        break;
    case CHEST_OPENING:
        if (t->chest_t > CHEST_OPEN_S) { t->chest_phase = CHEST_OPEN; t->chest_t = 0; }
        break;
    case CHEST_OPEN:
        if ((t->chest_puff_t += dt) > CHEST_PUFF_S) {       /* the stream out of the lid */
            t->chest_puff_t = 0;
            chest_puff(t, tank_decor_x(t, SD_IDX_CHEST), TANK_H - 16 - CHEST_H);
        }
        if (!t->chest_held && t->chest_t > CHEST_HOLD_S) { t->chest_phase = CHEST_CLOSING; t->chest_t = 0; }
        break;
    default:                                                 /* CHEST_CLOSING */
        if (t->chest_t > CHEST_OPEN_S) { t->chest_phase = CHEST_SHUT; t->chest_t = 0; }
        break;
    }
}
/* 0 shut .. 1 wide open, for the lid's angle and the glow */
float tank_chest_open(const tank_t *t) {
    switch (t->chest_phase) {
    case CHEST_OPENING: return clampf(t->chest_t / CHEST_OPEN_S, 0, 1);
    case CHEST_OPEN:    return 1.0f;
    case CHEST_CLOSING: return 1.0f - clampf(t->chest_t / CHEST_OPEN_S, 0, 1);
    default:            return 0.0f;
    }
}
bool tank_chest_tap(tank_t *t, float x, float y) {
    if (!tank_bit_live(t, SD_ITEM_CHEST)) return false;
    float cx = tank_decor_x(t, SD_IDX_CHEST), cy = TANK_H - 16 - CHEST_H * 0.5f;
    if (fabsf(x - cx) > CHEST_W * 0.75f || fabsf(y - cy) > CHEST_H * 1.3f) return false;
    if (t->chest_phase == CHEST_SHUT || t->chest_phase == CHEST_CLOSING) {
        t->chest_phase = CHEST_OPENING; t->chest_t = 0; t->chest_held = true;
        chest_puff(t, cx, TANK_H - 16 - CHEST_H);
    } else {                                                 /* a second tap shuts it */
        t->chest_phase = CHEST_CLOSING; t->chest_t = 0; t->chest_held = false;
    }
    tank_emit(TEV_TAP, -1);
    return true;
}
/* ---- the diver (tank.h) -------------------------------------------------
 * He plods, he stops to look, he breathes. His turning points are the
 * keeper's spot give or take DIVER_RANGE, so MOVING him moves his beat. */
#define DIVER_RANGE 120.0f
/* the stick in his glove goes UP: a lob on his own air into the top third,
 * where it hangs and then falls the whole way down past everyone */
static void diver_lob(tank_t *t) {
    int g = t->diver_stick;
    if (g < 0 || g >= GLOW_N) { t->diver_stick = -1; return; }
    glow_t *s = &t->glow[g];
    float dvx, dvy; int dvd;
    tank_diver_state(t, &dvx, &dvy, &dvd, NULL);
    s->carrier = -1; s->held_s = 0;
    s->y = dvy - DIVER_H + 18.0f;
    s->x = dvx + dvd * 13.0f;
    s->vy = -DIVER_LOB_VY;
    s->vx = dvd * tank_randf(t, 6.0f, 20.0f);        /* a little forward with it */
    s->spin = tank_randf(t, -3.0f, 3.0f);
    s->lob_s = DIVER_LOB_RISE_S;
    t->diver_stick = -1; t->diver_stick_t = 0;
    tank_emit(TEV_GLOW_PLAY, -1);
}
static void diver_tick(tank_t *t, float dt) {
    if (!tank_bit_live(t, SD_ITEM_DIVER)) {
        if (t->diver_stick >= 0) { t->glow[t->diver_stick].carrier = -1; t->diver_stick = -1; }
        return;
    }
    float home = tank_decor_x(t, SD_IDX_DIVER);
    if (t->diver_dir == 0) { t->diver_dir = 1; t->diver_x = home; }      /* first tick: at his spot */
    t->diver_t += dt;
    t->diver_bob += dt;
    bool has_totem = tank_diver_has_totem(t);
    /* ---- the glow sticks: stoop, look at it, lob it ---- */
    if (t->diver_stick >= 0) {
        t->diver_stick_t += dt;
        if (t->diver_stick_t > DIVER_HOLD_S) diver_lob(t);
    } else if (!has_totem && tank_bit_live(t, SD_ITEM_GLOW) && tank_randf(t, 0, 1) < DIVER_GRAB_P * dt) {
        for (int g = 0; g < GLOW_N; g++) {
            glow_t *s = &t->glow[g];
            if (s->carrier != -1 || s->vy != 0 || s->lob_s > 0) continue;   /* lying still, nobody's */
            if (fabsf(s->x - t->diver_x) > DIVER_GRAB_REACH) continue;
            s->carrier = (int8_t)GLOW_CARRIER_DIVER; s->held_s = 0;
            t->diver_stick = (int8_t)g; t->diver_stick_t = 0;
            t->diver_resting = true; t->diver_t = 0;                        /* he stops to do it */
            break;
        }
    }
    /* ---- now and then, the totem is HIS ---- */
    if (!has_totem && t->totem_phase == TOTEM_OFF && tank_bit_live(t, SD_ITEM_TOTEM) &&
        s_totem_cool <= 0 && !t->startled && tank_randf(t, 0, 1) < DIVER_TOTEM_P * dt) {
        float tx = tank_decor_x(t, SD_IDX_TOTEM);
        if (fabsf(t->diver_x - tx) < TOTEM_REACH * 2.2f) {   /* he has to be beside it, like anyone else */
            totem_lift(t, TOTEM_CARRIER_DIVER);
            has_totem = true;
        }
    }
    /* ---- the party: he goes to the speaker and dances ---- */
    bool party = tank_bass_party(t) && tank_bit_live(t, SD_ITEM_BASS);
    t->diver_dancing = false;
    if (party && !has_totem) {
        float bx = tank_decor_x(t, SD_IDX_BASS);
        if (fabsf(t->diver_x - bx) < 46.0f) {
            t->diver_dancing = true;                       /* near enough: he stays and moves to it */
            t->diver_dir = (int8_t)(sinf(t->diver_bob * 1.1f) < 0 ? -1 : 1);
        } else {
            t->diver_dir = (int8_t)(t->diver_x < bx ? 1 : -1);
            t->diver_x += t->diver_dir * DIVER_SPEED * 1.7f * dt;   /* he gets a move on for this */
        }
        if ((t->diver_puff_t += dt) > DIVER_PUFF_S * 0.6f) {
            t->diver_puff_t = 0;
            chest_puff(t, t->diver_x + t->diver_dir * 5, TANK_H - 16 - DIVER_H + 4);
        }
        return;                                            /* dancing beats plodding */
    }
    if (has_totem) {                                   /* his own parade: he leads it somewhere */
        float target;
        if (t->totem_phase == TOTEM_HOME) target = tank_decor_x(t, SD_IDX_TOTEM);
        else if (t->totem_planted) target = t->totem_party_x;
        else if (tank_bit_live(t, SD_ITEM_BASS)) target = tank_decor_x(t, SD_IDX_BASS);
        else target = TANK_W * 0.5f;
        float d = target - t->diver_x;
        if (fabsf(d) > 3.0f) {
            t->diver_dir = (int8_t)(d < 0 ? -1 : 1);
            t->diver_x += t->diver_dir * DIVER_MARCH_SPEED * dt;
        } else if (t->totem_planted) {
            t->diver_dancing = true;                   /* it is in the sand: he dances beside it */
            t->diver_dir = (int8_t)(sinf(t->diver_bob * 1.1f) < 0 ? -1 : 1);
        }
        t->diver_resting = false;
    } else if (t->diver_resting) {
        if (t->diver_t > DIVER_PAUSE_S && t->diver_stick < 0) { t->diver_resting = false; t->diver_t = 0; }
    } else {
        t->diver_x += t->diver_dir * DIVER_SPEED * dt;
        if (t->diver_t > DIVER_WALK_S) { t->diver_resting = true; t->diver_t = 0; }
    }
    /* with the totem up he is not patrolling: totem_tick steers him */
    float lo = home - DIVER_RANGE, hi = home + DIVER_RANGE;
    if (has_totem) { lo = DECOR_MARGIN + DIVER_HALF_W; hi = TANK_W - DECOR_MARGIN - DIVER_HALF_W; }
    if (lo < DECOR_MARGIN + DIVER_HALF_W) lo = DECOR_MARGIN + DIVER_HALF_W;
    if (hi > TANK_W - DECOR_MARGIN - DIVER_HALF_W) hi = TANK_W - DECOR_MARGIN - DIVER_HALF_W;
    if (t->diver_x < lo) { t->diver_x = lo; t->diver_dir = 1; }
    if (t->diver_x > hi) { t->diver_x = hi; t->diver_dir = -1; }
    if ((t->diver_puff_t += dt) > DIVER_PUFF_S) {            /* a string from the helmet */
        t->diver_puff_t = 0;
        chest_puff(t, t->diver_x + t->diver_dir * 5, TANK_H - 16 - DIVER_H + 4);
    }
}
void tank_diver_state(const tank_t *t, float *x, float *y, int *dir, bool *resting) {
    if (x) *x = t->diver_dir == 0 ? tank_decor_x(t, SD_IDX_DIVER) : t->diver_x;
    if (y) *y = TANK_H - 16 + sinf(t->diver_bob * 1.3f) * DIVER_BOB;
    if (dir) *dir = t->diver_dir >= 0 ? 1 : -1;
    if (resting) *resting = t->diver_resting;
}
bool tank_diver_dancing(const tank_t *t) { return t->diver_dancing; }
int  tank_diver_stick(const tank_t *t) { return t->diver_stick; }
bool tank_diver_has_totem(const tank_t *t) {
    return t->totem_phase != TOTEM_OFF && t->totem_carrier == TOTEM_CARRIER_DIVER;
}
bool tank_diver_tap(tank_t *t, float x, float y) {
    if (!tank_bit_live(t, SD_ITEM_DIVER)) return false;
    float dx2, dy2; tank_diver_state(t, &dx2, &dy2, NULL, NULL);
    if (fabsf(x - dx2) > DIVER_HALF_W + 10 || y < dy2 - DIVER_H - 8 || y > dy2 + 8) return false;
    t->diver_dir = (int8_t)-(t->diver_dir >= 0 ? 1 : -1);    /* he turns round and carries on */
    t->diver_resting = false; t->diver_t = 0;
    chest_puff(t, dx2, dy2 - DIVER_H + 4);
    tank_emit(TEV_TAP, -1);
    return true;
}

void tank_touch_hold(tank_t *t, float x, float y) {
    tank_handled(t);
    t->hold_active = true; t->hold_x = x; t->hold_y = y;
}

void tank_touch_drag(tank_t *t, float x, float y) {
    tank_handled(t);
    t->drag_active = true;
    if (!t->drag_has_prev) {
        /* stroke start: a slash must BEGIN on a plant - within SLASH_START_PX
         * of an actual frond (beside its spine, no higher than its tip). A
         * cleaning scrub that starts mid-glass, even straight above a tall
         * bed, and then works down through the grass arms nothing: it keeps
         * wiping algae and the fronds stand. */
        t->slash_armed = false;
        for (int b = 0; b < tank_veg_beds(t) && !t->slash_armed; b++)
            t->slash_armed = veg_near_frond(t, b, x, y, SLASH_START_SIDE_PX, SLASH_START_PX);
        t->slash_engaged = t->slash_cut = false; t->wipe_sounded = false;
        t->slash_x0 = x; t->slash_y0 = y; t->slash_h = t->slash_v = 0;
    }
    if (t->drag_has_prev) {
        float sdx = x - t->drag_px, sdy = y - t->drag_py;
        t->drag_dist += tank_dist(x, y, t->drag_px, t->drag_py);
        if (t->drag_dist >= WIPE_ENGAGE_PX) {      /* a real stroke, not a tap */
            wipe_algae(t, t->drag_px, t->drag_py, x, y);
            if (!t->wipe_sounded) { t->wipe_sounded = true; tank_emit(TEV_WIPE, -1); }
        }
        /* the slash (2026-09-04, per frond): an armed stroke becomes scissors
         * once it has travelled SLASH_PX sideways, mostly sideways - deliberate
         * work, so a tap or a missed poke at a fish never shears the garden.
         * From then on every sideways segment cuts the fronds it crosses at
         * the height it crosses them (the travel before engagement is cut
         * retroactively as one straight segment from the stroke start), so
         * a flick takes one or two fronds at the finger's height and a sweep
         * along the floor mows the bed to nubs. */
        if (t->slash_armed) {
            t->slash_h += fabsf(sdx); t->slash_v += fabsf(sdy);
            int cuts = 0;
            if (!t->slash_engaged) {
                if (t->slash_h >= SLASH_PX && t->slash_h > SLASH_RATIO * t->slash_v) {
                    t->slash_engaged = true;
                    cuts += veg_cut(t, t->slash_x0, t->slash_y0, x, y, true);
                }
            } else if (fabsf(sdx) >= fabsf(sdy))   /* only the sideways segments cut */
                cuts += veg_cut(t, t->drag_px, t->drag_py, x, y, false);
            if (cuts) tank_emit(TEV_SNIP, -1);
            if (cuts && !t->slash_cut) { t->slash_cut = true; t->trims++; }
        }
    }
    t->drag_px = x; t->drag_py = y; t->drag_has_prev = true;
}

void tank_feed(tank_t *t, float x, int n) {
    tank_handled(t);
    x = clampf(x, 25, TANK_W - 25);
    int dropped = 0;
    for (int i = 0; i < MAX_FOOD && n > 0; i++) {
        if (t->food[i].alive) continue;
        t->food[i].alive = true; t->food[i].from_player = true;
        t->food[i].x = clampf(x + tank_randf(t, -14, 14), 20, TANK_W - 20);
        t->food[i].y = tank_randf(t, 6, 18);
        t->food[i].age = 0;
        n--; dropped++;
    }
    if (dropped) tank_emit(TEV_FEED, -1);        /* the plink is for pellets, not for the tap (a full tank drops none) */
    t->feed_spot_x = t->feed_spot_x < 0 ? x : t->feed_spot_x + (x - t->feed_spot_x) * 0.3f;
    t->feed_open = true;                         /* a meal once somebody eats from it */
}

void tank_touch_tap(tank_t *t, float x, float y) {
    tank_handled(t);
    if (y < FEED_ZONE_Y) { tank_feed(t, x, 3); return; }     /* surface tap = feed */
    /* the pile: a tap on it calls somebody over to play, and is spent doing
       that - it is an object, like the ball, not a knock on the glass, so it
       neither counts toward the startle nor flips the light */
    if (tank_chest_tap(t, x, y)) return;                                 /* the chest: open it now */
    if (tank_diver_tap(t, x, y)) return;                                 /* the diver: turn him round */
    if (tank_totem_nudge(t, x, y)) { tank_emit(TEV_TAP, -1); return; }   /* the totem: somebody fetches it */
    if (tank_glow_nudge(t, x, y)) { tank_emit(TEV_TAP, -1); return; }
    if (t->tap_burst_t > TAP_WINDOW) t->tap_count = 0;
    t->tap_count++; t->tap_burst_t = 0; t->tap_x = x; t->tap_y = y;
    tank_emit(TEV_TAP, -1);
    if (t->startled) {                              /* chasing: keep them spooked */
        t->startle_x = x; t->startle_y = y; t->startle_cooldown = STARTLE_COOLDOWN;
        for (int i = 0; i < t->n_fish; i++) t->fish[i].stress = fminf(10, t->fish[i].stress + 0.6f);
    } else if (t->tap_count >= 3) {                 /* aggressive: engage */
        tank_emit(TEV_SPOOK, -1);
        t->startled = true; t->startle_x = x; t->startle_y = y; t->startle_cooldown = STARTLE_COOLDOWN;
        for (int i = 0; i < t->n_fish; i++) {
            fish_t *f = &t->fish[i];
            if (tank_dist(f->x, f->y, x, y) < STARTLE_RADIUS) {
                f->stress = fminf(10, f->stress + 2.5f);
                f->trust = fmaxf(0, f->trust - 0.4f);
            }
        }
    }
}

/* per-frame bookkeeping for the touch state machine */
static void touch_tick(tank_t *t, float dt) {
    t->tap_burst_t += dt;
    /* two taps then a pause: the light (MANUAL, the default; in settings'
       AUTO the idle rule owns it - 2026-09-15: the old always-on override
       rode in the save and froze a tank in permanent day) */
    if (!t->startled && t->tap_count == 2 && t->tap_burst_t > TAP_WINDOW) {
        /* the keeper's double-tap always wins. light_override outranks
           light_manual_off in tank_tick, so anything that had set it - the
           director, the sim's key, the dev page's old LIGHT button - used to
           leave the double-tap pressing a dead switch for good. Taking the
           override off here hands the light back, every time. */
        t->light_override = false;
        if (!t->light_auto) t->light_manual_off = !t->light_manual_off;
        t->tap_count = 0;
    }
    if (t->tap_count >= 3 && t->tap_burst_t > TAP_WINDOW) t->tap_count = 0;
    if (t->startled) {
        t->startle_cooldown -= dt;
        if (t->startle_cooldown <= 0) { t->startled = false; t->tap_count = 0; }
    }
    if (t->hold_active) {                           /* calm presence earns trust */
        float was = t->hold_time;
        t->hold_time += dt;
        bool draw_begins = was < HOLD_ATTRACT_S && t->hold_time >= HOLD_ATTRACT_S;
        for (int i = 0; i < t->n_fish; i++) {
            fish_t *f = &t->fish[i];
            float d = tank_dist(f->x, f->y, t->hold_x, t->hold_y);
            if (d < HOLD_RADIUS) f->trust = fminf(10, f->trust + dt * 0.02f);
            /* a hold-approach = a fish that was out in the tank when the
             * settled hold began its draw and then came all the way in.
             * Per FISH (2026-09-15: it was gated on the per-hold flag below,
             * so only the first arrival - always the fastest, highest-trust
             * pair - could ever get it; the other two came in hold after
             * hold and were never credited), and only for a fish that
             * actually travelled (same day: a fish already sitting under the
             * finger used to be credited at 1.5 s without moving). The
             * tank's hold_approaches counter still counts the hold once. */
            if (draw_begins) f->hold_far = d >= HOLD_APPROACH_FROM;
            if (f->hold_far && d < HOLD_APPROACH_AT) {
                f->ms_bits |= MS_FIRST_HOLD_APPROACH;
                if (!t->hold_approached) { t->hold_approached = true; t->hold_approaches++; tank_emit(TEV_INVESTIGATE, i); }
            }
        }
    } else {
        t->hold_time = 0; t->hold_approached = false;
        for (int i = 0; i < t->n_fish; i++) t->fish[i].hold_far = false;
    }
    if (t->greet_timer > 0) t->greet_timer -= dt;
    /* courtship episodes: while progression says an arrival is close, the
     * pair circles the reef for a few seconds every minute or so - frequent
     * enough to notice across a couple of check-ins, rare enough to feel
     * like something glimpsed rather than an indicator */
    if (t->courting && !t->night) {
        if (t->court_active > 0) t->court_active -= dt;
        else if ((t->court_cool -= dt) <= 0) {
            t->court_active = tank_randf(t, 6, 9);
            t->court_cool = tank_randf(t, 40, 90);
            int puffs = 2;                          /* a flirt of bubbles, from the grass */
            float sx, sy, sr; court_site(t, &sx, &sy, &sr);
            for (int i = 0; i < MAX_BUBBLE && puffs > 0; i++) {
                if (t->bubble[i].column) continue;
                t->bubble[i].x = sx + tank_randf(t, -10, 10);
                t->bubble[i].y = sy - 6;
                puffs--;
            }
        }
    } else t->court_active = 0;
    /* a lifted finger ends the wipe/slash stroke (platform re-asserts while down) */
    if (!t->drag_active) {
        t->drag_has_prev = false; t->drag_dist = 0;
        t->slash_armed = t->slash_engaged = t->slash_cut = false;
        t->slash_h = t->slash_v = 0;
    }
    t->drag_active = false;
}

void tank_handled(tank_t *t) { t->idle_s = 0; }

void tank_toggle_light(tank_t *t) {
    /* the first toggle takes over from the idle rule AND flips the light
       (the on/off cue comes from tank_tick, where the flip lands) */
    if (!t->light_override) { t->light_override = true; t->light_on = t->night; }
    else t->light_on = !t->light_on;
}

void tank_light_auto(tank_t *t) { t->light_override = false; }

void tank_scatter_food(tank_t *t, int n) {
    for (int i = 0; i < MAX_FOOD && n > 0; i++) {
        if (t->food[i].alive) continue;
        t->food[i].alive = true; t->food[i].from_player = false;
        t->food[i].x = tank_randf(t, 25, TANK_W - 25);
        t->food[i].y = tank_randf(t, 6, 20);
        t->food[i].age = 0;
        n--;
    }
}

/* see tank.h: dark-screen physiology only, rates per real hour of sleep */
#define SLEEP_HUNGER_PER_H 0.8f    /* fed -> ravenous over ~7 h of sleep */
#define SLEEP_ENERGY_PER_H 2.0f
#define SLEEP_STRESS_PER_H 2.0f
void tank_tick_sleep(tank_t *t, float seconds) {
    if (seconds <= 0) return;
    float h = seconds / 3600.0f;
    t->clock += seconds;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        f->hunger = clampf(f->hunger + SLEEP_HUNGER_PER_H * h, 0, 10);
        f->energy = clampf(f->energy + SLEEP_ENERGY_PER_H * h, 0, 10);
        f->stress = clampf(f->stress - SLEEP_STRESS_PER_H * h, 0, 10);
        f->speed = 0; f->target_speed = 0; f->bored = 0;  /* a night's sleep is a fresh start */
        f->goal_age += seconds; f->ask_age += seconds;  /* wake re-asks the advisor at once */
    }
    t->idle_s = 0;                                      /* the wake press is handling: lights up */
    for (int i = 0; i < MAX_FOOD; i++)                  /* overnight pellets go stale */
        if (t->food[i].alive && (t->food[i].age += seconds) > 45) t->food[i].alive = false;
    /* the garden grows fastest in a dark, untended tank: waking to a taller
     * canopy and film on the glass is the morning chore */
    veg_grow(t, seconds / VEG_GROW_SLEEP_S);
    snail_sleep(t, seconds);
    /* film steps go through the same accumulator the awake tick uses: the
     * device drowses in 60 s slices (firmware DROWSE_TICK_US) and
     * (int)(30 / 120) is 0 - the truncation that had quietly stopped every
     * bit of algae from forming overnight (2026-09-04). */
    t->algae_acc += seconds;
    int steps = (int)(t->algae_acc / ALGAE_STEP_SLEEP_S);
    if (steps > 600) steps = 600;                       /* bounded; the cap rules anyway */
    t->algae_acc -= steps * ALGAE_STEP_SLEEP_S;
    tank_grow_algae(t, steps);
}

/* the schema's 3 x 2 zone grid (advisor_core.c encodes the same), 0..5 */
static int zone_of(float x, float y) {
    int col = (int)(x / (TANK_W / 3.0f)); if (col > 2) col = 2; if (col < 0) col = 0;
    int row = (int)(y / (TANK_H / 2.0f)); if (row > 1) row = 1; if (row < 0) row = 0;
    return row * 3 + col;
}

/* ---- goal → target point + cruise speed (prototype targetForGoal, x0.55) ---- */
typedef struct { float x, y, speed; bool valid; } target_t;

/* `goal` is normally f->goal.id; the hesitation glance asks for the runner-up's
 * target, in which case nothing is mutated (no dart burst is started). */
/* the nearest glow stick nobody is holding, or -1. Both the play redirect
 * and the party's fetch detour ask it. */
static int nearest_free_glow(const tank_t *t, float x, float y, float *gx, float *gy) {
    int best = -1; float bd = 1e9f;
    for (int g = 0; g < GLOW_N; g++) {
        const glow_t *s = &t->glow[g];
        if (s->carrier >= 0) continue;
        float d = tank_dist(x, y, s->x, s->y);
        if (d < bd) { bd = d; best = g; if (gx) *gx = s->x; if (gy) *gy = s->y; }
    }
    return best;
}
static target_t target_for_goal(tank_t *t, int idx, goal_id_t goal, bool glance) {
    fish_t *f = &t->fish[idx];
    float tm = t->clock;
    target_t tg = {
        f->x + cosf(f->heading + sinf(f->wander) * 0.8f) * 50,
        f->y + sinf(f->heading + cosf(f->wander * 0.7f) * 0.45f) * 39,
        lerpf(12, 23, f->bold) * (1 - f->lazy * 0.35f), true,
    };
    /* The totem event (tank.h). Only goals that are ALREADY sociable or idle
       are redirected: a hungry, frightened or resting fish is the model's
       call and is left exactly alone. Where "with the others" IS depends on
       which part of the event is running. */
    float lead_x, lead_y;
    if (t->totem_phase != TOTEM_OFF && totem_lead(t, &lead_x, &lead_y, NULL) &&
        (goal == GOAL_FOLLOW_FRIEND || goal == GOAL_EXPLORE || goal == GOAL_DART_PLAY || goal == GOAL_VISIT_BUBBLES)) {
        bool leader = idx == t->totem_carrier;       /* never true while the DIVER has it:
                                                        then every fish is a follower */
        float bx = tank_decor_x(t, SD_IDX_BASS), home = tank_decor_x(t, SD_IDX_TOTEM);
        float ang = f->wander + idx * 1.7f;
        if (t->totem_phase == TOTEM_HOLD && leader) {          /* circling the speaker, totem up */
            tg.x = bx + cosf(tm * 0.8f) * 46;
            tg.y = TANK_H - 16 - 62 + sinf(tm * 0.8f) * 22;
        } else if (tank_bass_party(t) && !leader) {            /* the dance floor, around the stack */
            float gx2, gy2;
            if (s_glow_want[idx] > 0 && tank_bit_live(t, SD_ITEM_GLOW) && !totem_hands_full(t, idx) &&
                nearest_free_glow(t, f->x, f->y, &gx2, &gy2) >= 0) {
                tg.x = gx2; tg.y = gy2;                        /* fetch one, then come back and dance with it */
            } else {
                tg.x = bx + cosf(tm * 1.1f + idx * 2.1f) * (34 + idx * 7);
                tg.y = TANK_H - 16 - 54 + sinf(tm * 1.4f + idx * 1.3f) * (26 + idx * 4);
            }
        } else if (t->totem_phase == TOTEM_PLANTED && leader) { /* it is in the sand; the leader dances too */
            tg.x = t->totem_party_x + cosf(tm * 1.2f) * 30;
            tg.y = TANK_H - 16 - 50 + sinf(tm * 1.5f) * 20;
        } else if (leader) {                                   /* walking: out to the speaker, or back home */
            tg.speed = TOTEM_WALK_SPEED;                       /* a march, not a wander */
            if (t->totem_phase == TOTEM_HOME) {                /* all the way down onto its spot */
                tg.x = home; tg.y = TOTEM_PLANT_Y;
            } else {
                /* with a stack it is a march to the speaker; without one the
                   parade wants to be SEEN, so it takes the middle of the tank
                   instead of wandering wherever the fish was already going */
                tg.x = tank_bit_live(t, SD_ITEM_BASS) ? bx : TANK_W * 0.5f;
                tg.y = TANK_H - 16 - 44;
            }
        } else {                                               /* everyone else falls in around the leader */
            tg.x = lead_x + cosf(ang) * 46;
            tg.y = lead_y + sinf(ang) * 30;
        }
        tg.x = clampf(tg.x, 23, TANK_W - 23);
        tg.y = clampf(tg.y, 32, TANK_H - 23);
        return tg;
    }
    /* Play, the ball's show, and a piece that just went in - the same deal as
       the totem event above: only a goal that is already sociable or idle is
       steered, and the model still owns every goal it sets. In priority
       order, because a stick in the air beats anything else worth seeing. */
    if (goal == GOAL_FOLLOW_FRIEND || goal == GOAL_EXPLORE || goal == GOAL_DART_PLAY || goal == GOAL_VISIT_BUBBLES ||
        (goal == GOAL_REST && tank_afterhours(t))) {
        float px = 0, py = 0; bool go = false;
        int held = -1;
        for (int g = 0; g < GLOW_N; g++) if (t->glow[g].carrier == idx) { held = g; break; }
        if (held >= 0) {                                    /* carrying one: UP, that is the whole point */
            px = f->x + cosf(f->wander * 0.6f) * 34;        /* a lazy weave, not a straight line */
            py = GLOW_LIFT_Y;
            go = true;
        } else if (s_glow_watch[idx] > 0 && s_glow_watched[idx] >= 0 && s_glow_watched[idx] < GLOW_N) {
            const glow_t *w = &t->glow[s_glow_watched[idx]];   /* just dropped it: follow it down, watching */
            px = w->x + cosf(tm * 0.9f) * 16;
            py = w->y - 12;
            go = true;
        } else if (s_catch_to == idx && s_catch_stick >= 0 && s_catch_stick < GLOW_N) {
            const glow_t *s2 = &t->glow[s_catch_stick];     /* one is on its way: meet it */
            px = s2->x; py = s2->y; go = true;
        } else if (s_glow_want[idx] > 0 && !totem_hands_full(t, idx)) {  /* keen: the nearest stick lying about */
            go = nearest_free_glow(t, f->x, f->y, &px, &py) >= 0;
        } else if (s_totem_invited == idx && s_totem_invite_s > 0) {   /* asked to fetch the totem */
            px = tank_decor_x(t, SD_IDX_TOTEM);
            py = TANK_H - 16 - TOTEM_H * 0.5f;
            go = true;
        } else if (t->disco_show_s > 0) {                   /* the keeper started the ball: come and look */
            float dx, dy, dr, sp; tank_disco_state(t, &dx, &dy, &dr, &sp);
            if (dr > 0.25f) {                               /* once it is actually on its way down */
                px = dx + cosf(tm * 0.9f + idx * 2.3f) * (30 + idx * 6);
                py = dy + 26 + sinf(tm * 1.2f + idx * 1.7f) * 16;
                go = true;
            }
        } else if (s_decor_new >= 0 && s_decor_new_s > 0) { /* something new went in: swim round it */
            px = tank_decor_x(t, s_decor_new) + cosf(tm * 0.7f + idx * 2.0f) * (34 + idx * 8);
            py = TANK_H - 16 - 44 + sinf(tm * 0.9f + idx * 1.5f) * 26;
            go = true;
        }
        if (go) {
            tg.x = clampf(px, 23, TANK_W - 23);
            tg.y = clampf(py, 32, TANK_H - 23);
            tg.speed = lerpf(14, 26, f->bold);
            return tg;
        }
    }
    switch (goal) {
    case GOAL_SEEK_FOOD: {
        float d; int i = tank_nearest_food(t, f, &d);
        if (i >= 0) {
            /* hunger governs pursuit aggression regardless of which brain set
             * the goal: a peckish fish saunters (~0.75x), a starving one
             * charges (~1.3x) and barely brakes on approach */
            float h = clampf(f->hunger / 10.0f, 0, 1);
            float slow = clampf(d / 60, 0.4f + h * 0.35f, 1);
            tg.x = t->food[i].x; tg.y = t->food[i].y;
            tg.speed = lerpf(29, 62, f->bold) * slow * (0.75f + h * 0.55f);
            return tg;   /* skip the margin clamp: pellets rest at the floor */
        }
        tg.valid = false;
        break;
    }
    case GOAL_FLEE_SHADOW: {
        /* the roaming shadow was removed 2026-09-13; the token stays in the
         * frozen schema (the state line always reads `shadow none`, which the
         * model was trained on). If it still says flee, the fish bolts away
         * from the surface centre - the old no-shadow fallback. */
        float sx = TANK_W * 0.5f, sy = -60;
        float away = atan2f(f->y - sy, f->x - sx);
        tg.x = f->x + cosf(away) * 105; tg.y = f->y + sinf(away) * 83;
        tg.speed = lerpf(51, 83, f->bold);
        break;
    }
    case GOAL_VISIT_BUBBLES:
        /* each visitor keeps its own lane: a per-fish phase and radius, so
         * four fish orbit the column instead of stacking on one point */
        tg.x = t->bubble_x + sinf(tm * 1.2f + f->wander + idx * 1.9f) * (26 + idx * 6);
        tg.y = t->bubble_y - 74 + cosf(tm * 0.8f + f->wander + idx * 1.3f) * (34 + idx * 6);
        tg.speed = 23;
        break;
    case GOAL_FOLLOW_FRIEND: {
        float d; int i = tank_nearest_friend(t, idx, &d);
        if (i >= 0) {
            const fish_t *fr = &t->fish[i];
            float ang = atan2f(f->y - fr->y, f->x - fr->x) + sinf(tm + f->wander) * 0.35f;
            tg.x = fr->x + cosf(ang) * 54; tg.y = fr->y + sinf(ang) * 34;   /* a body length off, not on top */
            tg.speed = clampf(d - 40, 10, 42);
        } else tg.valid = false;
        break;
    }
    case GOAL_REST:
        tg.x = t->reef_x + 13 + f->rest_dx;
        tg.y = TANK_H - 50 + f->rest_dy;
        tg.speed = 6 + f->bold * 4;
        break;
    case GOAL_DART_PLAY:
        if (glance) { tg.valid = false; break; }
        if (f->dart_timer <= 0) {
            f->dart_x = tank_randf(t, 38, TANK_W - 38);
            f->dart_y = tank_randf(t, 40, TANK_H - 64);
            f->dart_timer = tank_randf(t, 0.8f, 1.5f);
        }
        tg.x = f->dart_x; tg.y = f->dart_y;
        tg.speed = lerpf(65, 95, f->bold);
        break;
    case GOAL_INSPECT_REEF:
        tg.x = t->reef_x + sinf(tm * 0.65f + f->wander + idx * 1.7f) * (39 + idx * 4);
        tg.y = t->reef_y - 35 + cosf(tm * 0.8f + f->wander + idx * 1.1f) * (15 + idx * 4);
        tg.speed = 15 + f->curiosity * 1.7f;
        break;
    case GOAL_EXPLORE: {
        /* a destination, not a drift (2026-09-14). Before this, explore was
         * the wander target above - 50 px ahead of the nose with a wobble -
         * a random walk that never left the neighbourhood, so an explore
         * decision looked like idling (Strato: "they don't explore the tank
         * very much"). Now the fish picks the zone it has seen least recently
         * (one of the two stalest, so two explorers don't take the same line),
         * cruises to a point inside it, and on arrival picks the next. The
         * model still owns the goal; this is the reflex layer resolving the
         * concrete target, as it does for every other goal. */
        if (glance) { if (!f->explore_set) tg.valid = false; else { tg.x = f->explore_x; tg.y = f->explore_y; } break; }
        if (!f->explore_set || tank_dist(f->x, f->y, f->explore_x, f->explore_y) < 26) {
            int cur = zone_of(f->x, f->y), best = -1, second = -1;
            for (int z = 0; z < 6; z++) {
                if (z == cur) continue;
                if (best < 0 || f->zone_seen[z] < f->zone_seen[best]) { second = best; best = z; }
                else if (second < 0 || f->zone_seen[z] < f->zone_seen[second]) second = z;
            }
            int z = (second >= 0 && (xr(t) % 3) == 0) ? second : best;
            int col = z % 3, row = z / 3;
            f->explore_x = tank_randf(t, col * (TANK_W / 3.0f) + 40, (col + 1) * (TANK_W / 3.0f) - 40);
            f->explore_y = tank_randf(t, row * (TANK_H / 2.0f) + 42, (row + 1) * (TANK_H / 2.0f) - 40);
            f->explore_set = true;
        }
        /* the wander wobble bends the line so it reads as a swim, not a bee-line */
        tg.x = f->explore_x + sinf(f->wander) * 18;
        tg.y = f->explore_y + cosf(f->wander * 0.7f) * 12;
        break;
    }
    default: if (glance) tg.valid = false; break;
    }
    float m = 23;
    tg.x = clampf(tg.x, m, TANK_W - m);
    tg.y = clampf(tg.y, m + 9, TANK_H - m);
    return tg;
}

static float wall_avoidance(const fish_t *f, bool *hit) {
    const float m = 34;
    float vx = 0, vy = 0;
    if (f->x < m)          vx += 1 - f->x / m;
    if (f->x > TANK_W - m) vx -= 1 - (TANK_W - f->x) / m;
    if (f->y < m + 6)      vy += 1 - (f->y - 6) / m;
    if (f->y > TANK_H - m) vy -= 1 - (TANK_H - f->y) / m;
    *hit = (vx != 0 || vy != 0);
    return *hit ? atan2f(vy, vx) : 0;
}

static void eat_nearby_food(tank_t *t, fish_t *f) {
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!t->food[i].alive) continue;
        if (tank_dist(f->x, f->y, t->food[i].x, t->food[i].y) < 12 * f->size + 4) {
            t->food[i].alive = false;
            f->hunger = clampf(f->hunger - 4.3f, 0, 10);
            f->energy = clampf(f->energy + 1.0f, 0, 10);
            f->curiosity = clampf(f->curiosity + 0.5f, 0, 10);   /* was 0.8: a trickle burst
                                                                     re-synced the school's curiosity */
            f->eaten++;
            tank_emit(TEV_EAT, (int)(f - t->fish));
            if (t->food[i].from_player) {
                f->eaten_player++; f->ms_bits |= MS_FIRST_MEAL_FROM_YOU;
                if (t->feed_open) { t->player_feedings++; t->feed_open = false; }   /* the gesture became a meal */
            }
            /* post-meal reflex from the prototype */
            if (f->hunger < 2.2f && f->goal.id == GOAL_SEEK_FOOD)
                f->goal.id = (xr(t) & 1) ? GOAL_VISIT_BUBBLES : GOAL_EXPLORE;
            return;   /* one bite per frame: a fish in a cloud of pellets takes
                         them one at a time, so a tank-mate gets a look in */
        }
    }
}

static void update_fish(tank_t *t, int idx, float dt) {
    fish_t *f = &t->fish[idx];
    /* drives (prototype rates, speed rescaled by the same 0.55) */
    f->hunger    = clampf(f->hunger + dt * (HUNGER_PER_S + f->bold * HUNGER_BOLD_PER_S +
                          (f->goal.id == GOAL_DART_PLAY ? HUNGER_DART_PER_S : 0)), 0, 10);
    /* curiosity: exploring feeds it (prototype) and - new 2026-09-01 -
     * satisfying it SPENDS it: a fish that has reached the bubbles or the
     * reef uses curiosity up there, the way rest restores energy and a meal
     * drops hunger. Before this a fed fish had no sink at all (+0.8 per
     * pellet, nothing ever took it back), so after days the whole school sat
     * at curiosity 9 and the advisor - reading "fed, calm, curious" exactly
     * as trained - parked all four at the bubble column for good. The model
     * still owns the goal; it just sees an honest drive, and the curiosity
     * band is in the re-ask signature so it gets to react. */
    {
        float dc = f->goal.id == GOAL_EXPLORE ? 0.08f : -0.025f;
        if (f->goal.id == GOAL_VISIT_BUBBLES || f->goal.id == GOAL_INSPECT_REEF) {
            bool bub = f->goal.id == GOAL_VISIT_BUBBLES;
            float sx = bub ? t->bubble_x : t->reef_x, sy = bub ? t->bubble_y - 74 : t->reef_y - 35;
            bool at = tank_dist(f->x, f->y, sx, sy) < CURIOSITY_SPEND_RADIUS;
            if (at) dc = -CURIOSITY_SPEND_PER_S;
            if (bub && at && !f->at_bubbles) tank_emit(TEV_BUBBLES, idx);   /* arrival at the column: the play cue */
            f->at_bubbles = bub && at;
        } else f->at_bubbles = false;
        f->curiosity = clampf(f->curiosity + dt * dc, 0, 10);
    }
    f->stress    = clampf(f->stress - dt * (f->goal.id == GOAL_REST ? 0.48f : 0.18f), 0, 10);
    /* energy: the prototype's per-second drain (full to empty in ~3 min of
     * cruising) kept the school at energy ~3 - permanently tired, so the
     * advisor never had a fish fit enough to dart and read every content
     * fish as "relaxed": bubbles, reef, follow (2026-09-01 census). Now a
     * fish cruises ~10 min on a full tank and a rest refills it in ~35 s. */
    f->energy    = clampf(f->energy + dt * (f->goal.id == GOAL_REST ? 0.30f
                          : -0.012f - f->speed / 4000.0f), 0, 10);
    /* a canopy is a place to hide: a fish inside one calms faster (below) */
    bool hidden = false;
    for (int b = 0; b < tank_veg_beds(t) && !hidden; b++)
        hidden = t->veg_growth[b] >= VEG_BARE && veg_inside(t, b, f->x, f->y);
    /* vegetation comfort (2026-09-04 rework: fish LIKE cover). Three regimes,
     * each seeking its own equilibrium against the natural decay above:
     *  - SMOTHERED: the second-tallest bed past VEG_SMOTHER (85% of the way to
     *    the surface) - at least two beds crowding the ceiling - is the tank
     *    being overrun: real stress, ramping from nothing at 85% to the full
     *    press at 100% (settles ~6-7 with every bed at the ceiling). One bed
     *    at the ceiling is just a good hiding place, never a stressor.
     *  - BARE: no bed past VEG_BARE - nowhere to hide - is a mild unease
     *    (settles ~1.3) that lifts the moment one tuft regrows.
     *  - otherwise COVER CALMS: stress decays faster the more canopy there is
     *    (up to +0.18/s, doubling the base rate, at a tank full of grass), and
     *    faster again for a fish tucked inside a canopy. */
    {
        float g1 = 0, g2 = 0, gmean = 0;               /* tallest, second tallest */
        int nb = tank_veg_beds(t);
        for (int b = 0; b < nb; b++) {
            float g = t->veg_growth[b];
            gmean += g / nb;
            if (g > g1) { g2 = g1; g1 = g; } else if (g > g2) g2 = g;
        }
        float over = (g2 - VEG_SMOTHER) / (1.0f - VEG_SMOTHER);
        if (over > 0)
            f->stress = clampf(f->stress + fminf(1, over) * 0.15f * (8.0f - f->stress) * dt, 0, 10);
        else if (g1 < VEG_BARE)
            f->stress = clampf(f->stress + 0.15f * (2.5f - f->stress) * dt, 0, 10);
        else
            f->stress = clampf(f->stress - dt * (0.18f * gmean + (hidden ? 0.18f : 0)), 0, 10);
    }

    f->wander += dt * (0.65f + f->curiosity * 0.04f) + sinf(t->clock + f->x * 0.01f) * dt * 0.12f;
    if (f->dart_timer > 0) f->dart_timer -= dt;
    f->goal_age += dt; f->ask_age += dt;
    /* boredom: the same pastime goes stale (a minute to the top); a meal or a
     * night's rest is never boring. A zone the fish has not seen for a while
     * is a small relief - so a real explore across the tank roughly pays for
     * itself, while an orbit at the bubble column or a tail-chase behind a
     * friend only accrues. (A zone boundary under the orbit gives nothing:
     * the zone has to be BORED_ZONE_STALE_S stale.) The new-goal relief is in
     * tank_tick where goals change. The band is in state_signature, so a fish
     * crossing into "bored" is re-asked - and the v4 model reads the value. */
    {
        goal_id_t g = f->goal.id;
        bool leisure = g == GOAL_VISIT_BUBBLES || g == GOAL_FOLLOW_FRIEND || g == GOAL_INSPECT_REEF ||
                       g == GOAL_DART_PLAY || g == GOAL_EXPLORE || (g == GOAL_REST && !t->night);
        f->bored = clampf(f->bored + dt * (leisure ? BORED_PER_S : -BORED_RELIEF_PER_S), 0, 10);
        int z = zone_of(f->x, f->y);
        if (z != f->zone_last) {
            if (f->zone_last >= 0 && t->clock - f->zone_seen[z] > BORED_ZONE_STALE_S)
                f->bored = clampf(f->bored - BORED_NEW_ZONE, 0, 10);
            f->zone_last = (int8_t)z;
        }
        f->zone_seen[z] = t->clock;
    }

    target_t tg = target_for_goal(t, idx, f->goal.id, false);
    /* fish prefer shallow climb/dive angles while cruising; full vertical
     * agility stays available for urgent goals */
    bool agile = f->goal.id == GOAL_FLEE_SHADOW || f->goal.id == GOAL_DART_PLAY;
    float desired = atan2f((tg.y - f->y) * (agile ? 1.0f : 0.72f), tg.x - f->x);
    /* final food approach: let the fish dip to the floor for the pellet
     * instead of hovering above it on wall-avoidance (the "staring" bug) */
    bool final_approach = f->goal.id == GOAL_SEEK_FOOD &&
                          tank_dist(f->x, f->y, tg.x, tg.y) < 40;
    bool hit; float push = wall_avoidance(f, &hit);
    if (hit && !final_approach)
        desired = norm_ang(desired + norm_ang(push - desired) * 0.62f);

    /* hesitation: a low-confidence decision is shown, not hidden - the fish
     * hovers and glances between its new target and the runner-up's for a
     * moment before committing. Never for flight or high urgency. */
    float hes_speed = -1;
    if (f->hesitate > 0) {
        f->hesitate -= dt;
        target_t alt = f->goal.runner_up < GOAL_COUNT
                     ? target_for_goal(t, idx, f->goal.runner_up, true) : (target_t){0, 0, 0, false};
        float phase = sinf(t->clock * 5.0f + f->wander);
        if (alt.valid && phase < 0) desired = atan2f((alt.y - f->y) * 0.72f, alt.x - f->x);
        hes_speed = 5;
    }

    /* touch: spooked fish bolt from the tap site; trusting fish drift to a
     * resting finger (reflex-layer, independent of the advisor's goal) */
    float touch_speed = -1;
    if (t->startled && f->goal.id != GOAL_FLEE_SHADOW) {
        float d = tank_dist(f->x, f->y, t->startle_x, t->startle_y);
        if (d < STARTLE_RADIUS * 1.6f) {
            float away = atan2f(f->y - t->startle_y, f->x - t->startle_x);
            desired = norm_ang(desired + norm_ang(away - desired) * 0.85f);
            touch_speed = lerpf(55, 90, f->bold);
        }
    } else if (idx == t->stage_fish) {
        /* on stage (setup): a lazy figure-of-eight round the page's clear
         * spot, turning at each end so both flanks show; a fish far from it
         * cruises over first. Above every other presentation: the keeper is
         * looking at THIS fish. */
        float ph = t->clock * 1.1f + f->wander;
        float wx = t->stage_x + cosf(ph) * 24, wy = t->stage_y + sinf(ph * 2) * 6;
        float to = atan2f(wy - f->y, wx - f->x);
        desired = norm_ang(desired + norm_ang(to - desired) * 0.85f);
        touch_speed = tank_dist(f->x, f->y, t->stage_x, t->stage_y) > 80 ? 44 : 20;
    } else if (t->hold_active && t->hold_time >= HOLD_ATTRACT_S &&
               f->goal.id != GOAL_FLEE_SHADOW && f->trust >= 4.0f &&
               f->hunger < HOLD_HUNGER_VETO) {
        /* only a settled hold (~3 s of contact) draws fish in - quick taps and
         * card-taps never twitch the school - and a strongly hungry fish has
         * better things to do than visit a finger */
        float d = tank_dist(f->x, f->y, t->hold_x, t->hold_y);
        if (d < HOLD_RADIUS) {
            float w = (f->trust - 4.0f) / 6.0f;          /* 0..1 with trust */
            float to = atan2f(t->hold_y - f->y, t->hold_x - f->x);
            if (d > 28) desired = norm_ang(desired + norm_ang(to - desired) * (0.5f + 0.45f * w));
            /* far fish cross the tank at a cruise, not a drift; close in
             * they slow to arrive and hover */
            float far = d > 120 ? 1.0f : d / 120.0f;
            touch_speed = d > 28 ? lerpf(14, 30, w) + far * lerpf(10, 20, w) : 4;
        }
    } else if (t->greet_timer > 0 && f->trust >= 7.0f && f->goal.id != GOAL_FLEE_SHADOW &&
               f->goal.id != GOAL_SEEK_FOOD) {
        /* light-on greeting: trusting fish come up front to see who's there */
        float gx = TANK_W * 0.5f + (idx - t->n_fish * 0.5f) * 34, gy = 70;
        float d = tank_dist(f->x, f->y, gx, gy);
        if (d > 24) {
            float to = atan2f(gy - f->y, gx - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.7f);
            touch_speed = 26;
        } else touch_speed = 5;
    } else if (t->ravenous && f->goal.id != GOAL_FLEE_SHADOW && f->hunger > 6.5f) {
        /* starving tank: reflex presentation of the famine, like the greet/
         * hold overrides above - the advisor still owns the goal. Two phases:
         * empty water = beg where meals come from (quick darts back and forth
         * under the feed spot, unmistakably "feed me"); live pellets = the
         * DASH - a real starving fish bolts the moment food hits the water,
         * it doesn't wait to think it over (the advisor's seek_food arrives
         * seconds later and takes back a fish that is already eating). A fish
         * that has eaten (hunger <= 6.5) drops out of the frenzy at once. */
        float fd; int fi = tank_nearest_food(t, f, &fd);
        if (fi >= 0) {
            float to = atan2f(t->food[fi].y - f->y, t->food[fi].x - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.92f);
            touch_speed = fd > 14 ? lerpf(85, 115, clampf(f->hunger / 10.0f, 0, 1)) : 30;
        } else {
            float spot = t->feed_spot_x >= 0 ? t->feed_spot_x : TANK_W * 0.5f;
            float wx = spot + sinf(t->clock * (1.6f + f->bold * 0.9f) + idx * 2.1f) * (34 + idx * 6);
            float wy = 30 + idx * 4 + sinf(t->clock * 3.1f + f->wander) * 6;
            float to = atan2f(wy - f->y, wx - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.85f);
            touch_speed = tank_dist(f->x, f->y, wx, wy) > 18
                        ? lerpf(40, 68, clampf(f->hunger / 10.0f, 0, 1)) : 22;
        }
    } else if (t->court_active > 0 && !t->ravenous &&
               (idx == t->court_a || idx == t->court_b) &&
               f->goal.id != GOAL_FLEE_SHADOW && f->hunger < HOLD_HUNGER_VETO) {
        /* courtship circle: the parents-to-be dive into the nursery grass and
         * weave a low loop through it on opposite sides - the arrival tell,
         * performed rather than printed (reflex presentation; the advisor
         * still owns both goals). Down in the fronds, not mid-water: Strato
         * (2026-09-04) - by the reef it read as two friends at the bubbles. */
        float ph = t->clock * 1.7f + (idx == t->court_b ? 3.14159f : 0);
        float cx, cy, rx; court_site(t, &cx, &cy, &rx);
        float wx = cx + cosf(ph) * rx, wy = cy + sinf(ph) * 6;
        float to = atan2f(wy - f->y, wx - f->x);
        desired = norm_ang(desired + norm_ang(to - desired) * 0.85f);
        touch_speed = tank_dist(f->x, f->y, cx, cy) > 90 ? 46 : 30;
    }

    /* separation - personal space. Fish are ~40 px long; the old 16 px
     * radius let four content fish stack on one landmark like a pile of
     * cards (2026-09-01). Now a body length, weighted by how close they are;
     * a fleeing fish keeps its line. */
    if (f->goal.id != GOAL_FLEE_SHADOW)
        for (int i = 0; i < t->n_fish; i++) {
            if (i == idx) continue;
            float d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y), r = 36 * f->size;
            if (d < r) {
                float away = atan2f(f->y - t->fish[i].y, f->x - t->fish[i].x);
                desired = norm_ang(desired + norm_ang(away - desired) * (0.35f + 0.5f * (1 - d / r)));
            }
        }

    /* urgency scales cruise speed a touch (0..9 → 0.8..1.25) */
    float ugain = 0.8f + f->goal.urgency * 0.05f;
    float turn = clampf(norm_ang(desired - f->heading), -f->turn_rate * dt, f->turn_rate * dt);
    f->heading = norm_ang(f->heading + turn);
    float want = touch_speed >= 0 ? touch_speed : hes_speed >= 0 ? hes_speed : tg.speed * ugain;
    f->target_speed = want * (f->energy < 1.2f ? 0.45f : 1);
    /* the keeper's pace, applied to what the fish already wants rather than
       to any decision it made: the same tank, livelier or calmer */
    f->target_speed *= FISH_SPEED_MUL[t->fish_speed < FISH_SPEED_N ? t->fish_speed : 1];
    /* swimming through a canopy is slow going (a fleeing fish crashes through) */
    if (f->goal.id != GOAL_FLEE_SHADOW)
        for (int b = 0; b < tank_veg_beds(t); b++)
            if (veg_inside(t, b, f->x, f->y)) { f->target_speed *= VEG_SLOW; break; }
    f->speed = lerpf(f->speed, f->target_speed, clampf(dt * 2.6f, 0, 1));
    f->x += cosf(f->heading) * f->speed * dt;
    f->y += sinf(f->heading) * f->speed * dt + sinf(t->clock * 1.4f + f->wander) * dt * 1.7f;

    /* hard bounds */
    float m = 15 * f->size;
    if (f->x < m)          { f->x = m;          f->heading = norm_ang(3.14159f - f->heading); }
    if (f->x > TANK_W - m) { f->x = TANK_W - m; f->heading = norm_ang(3.14159f - f->heading); }
    if (f->y < m)          { f->y = m;          f->heading = -f->heading; }
    if (f->y > TANK_H - m) { f->y = TANK_H - m; f->heading = -f->heading; }

    eat_nearby_food(t, f);
}

/* Coarse state signature (the browser prototype's "blunt" gate): banded
 * drives plus bucketed sightings. Excludes clock bearings and exact drive
 * digits, which only steer - a change here is a reason to re-decide. */
static int band3(float v) { return v < 3.5f ? 0 : v < 7 ? 1 : 2; }
static uint32_t state_signature(const tank_t *t, int idx) {
    const fish_t *f = &t->fish[idx];
    /* 2026-09-11 (the battery pass): the device's LLM core was saturated by
     * this gate, not by the idle ceiling - measured in the sim at 25 fps,
     * 2 fish: food distance flipped 6.3x per fish-minute (every bucket a
     * sinking pellet crossed), the (since removed) roaming shadow's distance
     * 5.8x, the wall flag 4x, against 0.4 for hunger. So the twitchy fields
     * are now events:
     *  food   - only for a fish that could want it (not full): none / in the
     *           tank / within reach. A pellet appearing, or coming close, is a
     *           reason to re-decide; its every metre of sinking is not.
     *  wall   - dropped: it is steering, and the state line still says
     *           near/clear whenever a decision is made for another reason.
     * The model sees the exact distances in its state line either way; this
     * only decides WHEN it is asked. */
    float fd = 1e9f; int fi = tank_nearest_food(t, f, &fd);
    int food = (fi < 0 || band3(f->hunger) == 0) ? 0 : fd < 70 ? 2 : 1;
    return (uint32_t)band3(f->hunger) | (uint32_t)band3(f->energy) << 2 | (uint32_t)band3(f->stress) << 4
         | (uint32_t)food << 6
         | (uint32_t)t->night << 10 | (uint32_t)band3(f->curiosity) << 11
         | (uint32_t)band3(f->bored) << 13;      /* a fish going stale is asked again (2026-09-14) */
}

void tank_tick(tank_t *t, float dt, advisor_fn advise) {
    t->clock += dt;
    s_emit_clock = t->clock;            /* tank_emit stamps the fish's last moment with it */
    /* the light: on while the device is handled, off LIGHT_IDLE_S after the
     * last touch or movement (tank_handled) - a tank left on the desk goes
     * dark and the fish sleep. A setup page or a prompt holds it on; the
     * director's / sim's manual override wins over both. */
    t->idle_s += dt;
    bool was_night = t->night;
    t->night = t->light_override ? !t->light_on
             : t->hold_light ? false
             : !t->light_auto ? t->light_manual_off              /* MANUAL (default): the double-tap's state */
             : t->idle_s > (float)t->light_idle_s;                      /* AUTO: the idle rule */
    if (t->night != was_night) {
        tank_emit(t->night ? TEV_LIGHT_OFF : TEV_LIGHT_ON, -1);
        if (t->night) {                                  /* the rig is in: nobody is going to bed yet */
            bool rig = tank_bit_live(t, SD_ITEM_GLOW) || tank_bit_live(t, SD_ITEM_TOTEM) ||
                       tank_bit_live(t, SD_ITEM_BASS) || tank_bit_live(t, SD_ITEM_LASER) ||
                       tank_bit_live(t, SD_ITEM_DISCO);
            s_after_s = rig ? AFTERHOURS_S : 0;
        } else s_after_s = 0;
    }
    if (s_after_s > 0 && t->night) s_after_s -= dt;
    else if (!t->night) s_after_s = 0;

    touch_tick(t, dt);

    /* food sinks, settles, decays */
    int live_food = 0;
    for (int i = 0; i < MAX_FOOD; i++) {
        food_t *p = &t->food[i];
        if (!p->alive) continue;
        live_food++;
        p->age += dt;
        if (p->y < TANK_H - 14) {
            p->y += 8 * dt;
            p->x += sinf(p->age * 1.5f) * dt * 2;
        }
        if (p->age > 45) p->alive = false;
    }
    /* the tank's own trickle keeps fish alive when nobody is home (never
     * ruined by absence); the keeper's pellets are what progression counts.
     * Hunger-gated (see the hunger economy notes above): a pellet only when
     * somebody is really hungry, at a per-SECOND rate, so it's the same tank
     * at 25 fps and 60 fps and it never over-feeds a school that isn't asking.
     * While the tank is ravenous-begging it holds off, so the keeper's first
     * pellets are the event that ends the wait (progression re-arms it) -
     * but once the keeper HAS fed this episode it helps again: a quick fish
     * can gobble every pellet, and the slow one shouldn't beg out the whole
     * give-up valve for it. */
    float hungriest = 0;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].hunger > hungriest) hungriest = t->fish[i].hunger;
    bool hold = (t->ravenous && !t->ravenous_fed) || t->trickle_off;
    if (!hold && live_food < 2 && hungriest >= TRICKLE_HUNGER &&
        tank_randf(t, 0, 1) < TRICKLE_RATE * dt)
        tank_scatter_food(t, 1);

    /* upkeep: the garden gets away from an idle keeper, slowly */
    veg_grow(t, dt / VEG_GROW_AWAKE_S);
    t->algae_acc += dt;
    if (t->algae_acc >= ALGAE_STEP_AWAKE_S) {
        t->algae_acc -= ALGAE_STEP_AWAKE_S;
        tank_grow_algae(t, 1);
    }
    snail_tick(t, dt);
    bass_tick(t);
    glow_tick(t, dt);
    if (s_totem_invite_s > 0 && (s_totem_invite_s -= dt) <= 0) s_totem_invited = -1;
    if (s_decor_new_s > 0 && (s_decor_new_s -= dt) <= 0) s_decor_new = -1;
    totem_tick(t, dt);
    chest_tick(t, dt);
    diver_tick(t, dt);
    disco_tick(t, dt);
    gate_tick(t);

    /* bubbles rise */
    for (int i = 0; i < MAX_BUBBLE; i++) {
        bubble_t *b = &t->bubble[i];
        b->wobble += dt * 2.5f;
        b->y -= b->vy * dt;
        b->x += sinf(b->wobble) * dt * (b->column ? 9 : 4);
        if (b->y < -6) {
            b->y = TANK_H + tank_randf(t, 4, 24);
            b->x = b->column ? t->bubble_x + tank_randf(t, -10, 10)
                             : tank_randf(t, 12, TANK_W - 12);
        }
    }

    /* advisor: polled every frame (async decisions land the moment they're
     * ready). A (re)decision is REQUESTED need-based, not on a fixed cadence:
     * the coarse signature changed and ADVISOR_MIN_INTERVAL passed, or the
     * idle ceiling hit, or something urgent (starving with food in view).
     * Effective cadence therefore scales with how much is
     * happening, not with how many fish live here. The start index rotates
     * so no slot is structurally favoured when several fish ask at once. */
    if (advise && t->n_fish > 0) {
        for (int k = 0; k < t->n_fish; k++) {
            int i = (t->ask_rr + k) % t->n_fish;
            fish_t *f = &t->fish[i];
            uint32_t sig = state_signature(t, i);
            /* prototype's urgent path: starving with food in view gets
             * asked NOW instead of waiting its turn */
            bool urgent = f->hunger > 8.0f && f->goal.id != GOAL_SEEK_FOOD &&
                          f->goal_age > 0.8f && tank_nearest_food(t, f, 0) >= 0;
            bool changed = sig != f->sig && f->ask_age > ADVISOR_MIN_INTERVAL;
            bool idle = f->ask_age > ADVISOR_IDLE_CEILING;
            bool want = changed || idle || (urgent && f->ask_age > 0.5f);
            goal_t g = advise(t, i, want);
            if (want) {            /* async advisors queue it; rules answer now */
                f->sig = sig; f->ask_age = 0; t->advisor_asks++;
                t->ask_rr = (i + 1) % t->n_fish;
            }
            if (g.id < GOAL_COUNT && g.id != f->goal.id) {
                /* a genuinely new pastime relieves boredom; bouncing back to
                 * the one just left (bubbles -> friend -> bubbles) does not */
                if (g.id != f->goal_prev) f->bored = clampf(f->bored - BORED_NEW_GOAL, 0, 10);
                f->goal_prev = f->goal.id;
                if (g.id == GOAL_EXPLORE) f->explore_set = false;   /* pick a fresh destination */
                f->goal = g;
                f->goal_age = 0;
                /* visible deliberation, scaled by how torn the advisor was */
                bool calm = g.id != GOAL_FLEE_SHADOW && g.urgency < 8 && g.confidence < 0.6f;
                f->hesitate = calm ? (0.6f - g.confidence) * 2.5f : 0;
            } else if (g.id == f->goal.id) {
                f->goal.urgency = g.urgency;
                f->goal.confidence = g.confidence; f->goal.runner_up = g.runner_up;
            }
        }
    }

    /* Starvation instrument (was a TEMPORARY survival-reflex override; retired
     * 2026-08-21 after the retrained model chose seek_food on 92% of starving
     * states with defensible misses - per Strato: the LLM owns decisions).
     * Counts moments a starving fish ignores available food for >4s. Pure
     * diagnostic, never changes a goal. */
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (f->hunger > 8.5f && f->goal.id != GOAL_SEEK_FOOD &&
            f->goal.id != GOAL_FLEE_SHADOW && f->goal_age > 4.0f &&
            !f->starve_flagged && tank_nearest_food(t, f, 0) >= 0) {
            f->starve_flagged = true;          /* count once per episode */
            tank_reflex_overrides++;
        }
        if (f->goal.id == GOAL_SEEK_FOOD || f->hunger < 8.0f) f->starve_flagged = false;
    }

    for (int i = 0; i < t->n_fish; i++) update_fish(t, i, dt);

    /* consume the hold AFTER the fish have seen it (the platform re-asserts
     * every frame the finger stays down). Clearing it at the top of the tick
     * was the old bug that kept the drift-to-finger reflex from ever firing. */
    t->hold_active = false;
}
