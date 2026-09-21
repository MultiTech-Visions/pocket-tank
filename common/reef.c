/* reef.c - the reef builder's grid, catalogue and backdrop (see reef.h). */
#include "reef.h"
#include "render.h"
#include <string.h>

/* Coral reads as saturated colour against dark water, so these are picked
 * bright and spread round the wheel rather than sampled from a photograph -
 * a tank built out of them should look like a reef somebody BUILT. */
const uint32_t REEF_PALETTE[REEF_COLOURS + 1] = {
    0x000000,                                   /* 0: empty, never drawn */
    0xff5f8d, 0xff3fa8, 0xc23ff0, 0x7a5cff,     /* pinks into violet */
    0x3f7dff, 0x38b8ff, 0x2fe6d8, 0x2fd98a,     /* blues into teal and green */
    0x7ae03a, 0xd8e63a, 0xffc22e, 0xff8a28,     /* greens into yellow and orange */
    0xff5236, 0xf2f0e6, 0x9a7bd8,               /* red, bone, and a dusty lilac */
};
const char *const REEF_COLOUR_NAMES[REEF_COLOURS + 1] = {
    "EMPTY", "ROSE", "MAGENTA", "ORCHID", "VIOLET", "COBALT", "SKY", "TURQUOISE",
    "JADE", "LIME", "CHARTREUSE", "AMBER", "TANGERINE", "CORAL", "BONE", "LILAC",
};

/* 4x4 masks, bit (y*4 + x). Written out in binary so the shape is visible
 * in the source - a catalogue you cannot read is a catalogue nobody edits. */
#define M(a,b,c,d) (uint16_t)(((a) << 0) | ((b) << 4) | ((c) << 8) | ((d) << 12))
const reef_shape_t REEF_SHAPES[REEF_SHAPE_N] = {
    { "STUD",   M(0x1, 0x0, 0x0, 0x0) },        /* one cell */
    { "DUO",    M(0x3, 0x0, 0x0, 0x0) },        /* two across */
    { "TRIO",   M(0x7, 0x0, 0x0, 0x0) },
    { "BAR",    M(0xF, 0x0, 0x0, 0x0) },        /* the long one */
    { "BLOCK",  M(0x3, 0x3, 0x0, 0x0) },        /* 2x2 */
    { "SLAB",   M(0x7, 0x7, 0x0, 0x0) },        /* 3x2 */
    { "BOULDER",M(0x7, 0x7, 0x7, 0x0) },        /* 3x3 */
    { "ELL",    M(0x1, 0x1, 0x3, 0x0) },
    { "JAY",    M(0x2, 0x2, 0x3, 0x0) },
    { "TEE",    M(0x7, 0x2, 0x0, 0x0) },
    { "ESS",    M(0x6, 0x3, 0x0, 0x0) },
    { "ZED",    M(0x3, 0x6, 0x0, 0x0) },
    { "PLUS",   M(0x2, 0x7, 0x2, 0x0) },
    { "FAN",    M(0x5, 0x7, 0x2, 0x0) },        /* two fingers off a stem */
    { "BRANCH", M(0x5, 0x2, 0x2, 0x2) },        /* a tall stem with arms */
    { "ARCH",   M(0x7, 0x5, 0x5, 0x0) },        /* something to swim through */
};
#undef M

uint16_t reef_shape_rot(uint16_t mask, int rot) {
    rot &= 3;
    for (int r = 0; r < rot; r++) {
        uint16_t out = 0;
        for (int y = 0; y < REEF_SHAPE_W; y++)
            for (int x = 0; x < REEF_SHAPE_W; x++)
                if (mask & (1u << (y * REEF_SHAPE_W + x)))
                    out |= (uint16_t)(1u << (x * REEF_SHAPE_W + (REEF_SHAPE_W - 1 - y)));
        mask = out;
    }
    return mask;
}
void reef_shape_bounds(uint16_t mask, int *x0, int *y0, int *w, int *h) {
    int lx = REEF_SHAPE_W, ly = REEF_SHAPE_W, hx = -1, hy = -1;
    for (int y = 0; y < REEF_SHAPE_W; y++)
        for (int x = 0; x < REEF_SHAPE_W; x++)
            if (mask & (1u << (y * REEF_SHAPE_W + x))) {
                if (x < lx) lx = x; if (x > hx) hx = x;
                if (y < ly) ly = y; if (y > hy) hy = y;
            }
    if (hx < 0) { *x0 = *y0 = 0; *w = *h = 0; return; }
    *x0 = lx; *y0 = ly; *w = hx - lx + 1; *h = hy - ly + 1;
}

/* tank.h cannot include this header (reef.h needs tank.h), so the field is
 * declared there with a literal size. This is the wire that stops the two
 * drifting: change the grid and the build stops here, not in somebody's
 * save. */
_Static_assert(sizeof(((tank_t *)0)->reef) == REEF_BYTES,
               "tank_t.reef is not REEF_BYTES: fix the literal in tank.h");

/* ---- the grid: two cells to a byte, low nibble first ---- */
static unsigned s_epoch = 1;
uint8_t reef_get(const tank_t *t, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= REEF_COLS || cy >= REEF_ROWS) return 0;
    int i = cy * REEF_COLS + cx;
    uint8_t b = t->reef[i >> 1];
    return (i & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0f);
}
void reef_set(tank_t *t, int cx, int cy, uint8_t colour) {
    if (cx < 0 || cy < 0 || cx >= REEF_COLS || cy >= REEF_ROWS) return;
    if (colour > REEF_COLOURS) colour = REEF_COLOURS;
    int i = cy * REEF_COLS + cx;
    uint8_t *b = &t->reef[i >> 1];
    uint8_t was = *b;
    *b = (i & 1) ? (uint8_t)((*b & 0x0f) | (uint8_t)(colour << 4))
                 : (uint8_t)((*b & 0xf0) | colour);
    if (*b != was) s_epoch++;
}
bool reef_empty(const tank_t *t) {
    for (int i = 0; i < REEF_BYTES; i++) if (t->reef[i]) return false;
    return true;
}
void reef_clear(tank_t *t) { memset(t->reef, 0, sizeof t->reef); s_epoch++; }
int reef_stamp(tank_t *t, uint16_t mask, int cx, int cy, uint8_t colour) {
    int n = 0;
    for (int y = 0; y < REEF_SHAPE_W; y++)
        for (int x = 0; x < REEF_SHAPE_W; x++) {
            if (!(mask & (1u << (y * REEF_SHAPE_W + x)))) continue;
            int gx = cx + x, gy = cy + y;
            if (gx < 0 || gy < 0 || gx >= REEF_COLS || gy >= REEF_ROWS) continue;
            if (reef_get(t, gx, gy) == colour) continue;
            reef_set(t, gx, gy, colour);
            n++;
        }
    return n;
}
unsigned reef_epoch(const tank_t *t) { (void)t; return s_epoch; }

/* ---- the backdrop ----
 * Only render.h's public primitives are used here, the same discipline
 * ui_ext.c keeps, so render.c needs nothing added for this. The night dim
 * is applied to the colour rather than to the draw, because a flat rect is
 * all that is being asked for. */
static uint32_t reef_shade(uint32_t rgb, int pct, float dim) {
    int r = (int)((rgb >> 16) & 0xff), g = (int)((rgb >> 8) & 0xff), b = (int)(rgb & 0xff);
    if (pct > 0) { r += (255 - r) * pct / 100; g += (255 - g) * pct / 100; b += (255 - b) * pct / 100; }
    else if (pct < 0) { r = r * (100 + pct) / 100; g = g * (100 + pct) / 100; b = b * (100 + pct) / 100; }
    if (dim < 1.0f) { r = (int)(r * dim); g = (int)(g * dim); b = (int)(b * dim); }
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
void reef_draw(const tank_t *t, uint16_t *fb, int stride, float dim) {
    for (int cy = 0; cy < REEF_ROWS; cy++)
        for (int cx = 0; cx < REEF_COLS; cx++) {
            uint8_t col = reef_get(t, cx, cy);
            if (!col) continue;
            int x = cx * REEF_CELL, y = cy * REEF_CELL;
            uint32_t rgb = REEF_PALETTE[col];
            /* a block, lighter where nothing sits on it and darker where it
               meets open water: enough to read as stacked pieces rather than
               a flat field of colour */
            render_rect(fb, stride, x, y, REEF_CELL, REEF_CELL, reef_shade(rgb, 0, dim));
            if (!reef_get(t, cx, cy - 1))
                render_rect(fb, stride, x, y, REEF_CELL, 2, reef_shade(rgb, 28, dim));
            if (!reef_get(t, cx, cy + 1))
                render_rect(fb, stride, x, y + REEF_CELL - 2, REEF_CELL, 2, reef_shade(rgb, -38, dim));
            if (!reef_get(t, cx - 1, cy))
                render_rect(fb, stride, x, y, 2, REEF_CELL, reef_shade(rgb, -16, dim));
            if (!reef_get(t, cx + 1, cy))
                render_rect(fb, stride, x + REEF_CELL - 2, y, 2, REEF_CELL, reef_shade(rgb, -26, dim));
        }
}
