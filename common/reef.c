/* reef.c - the reef builder's grid, catalogue and backdrop (see reef.h). */
#include "reef.h"
#include "render.h"
#include <string.h>
#include <math.h>

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

/* ---- the builder ------------------------------------------------------- */
/* Fifteen colours in one column of 34 px needs 510 px and the glass is 368,
 * so the bar is TWO columns - which also leaves room for the rubber under
 * it. The first cut ran the last five colours and the rubber off the
 * bottom edge entirely. */
#define RUI_BAR_COLS   2
#define RUI_DOT_R      11
#define RUI_DOT_DX     28
#define RUI_DOT_DY     28
#define RUI_BAR_W      (RUI_BAR_COLS * RUI_DOT_DX + 6)
#define RUI_BAR_ROWS   ((REEF_COLOURS + RUI_BAR_COLS - 1) / RUI_BAR_COLS)
#define RUI_CAT_X      (RUI_BAR_W + 10)      /* the shape catalogue beside it */
#define RUI_CAT_COLS   4
#define RUI_TILE       84
#define RUI_TILE_PAD   6
#define RUI_MENU_Y     44                    /* the menu's lid */
#define RUI_FOOT_Y     (TANK_H - 24)         /* ... and the band its hint sits in */
#define RUI_HINT_Y     (TANK_H - 22)
#define RUI_FOOT_H     30

static bool  s_menu;
static int   s_shape = -1, s_rot, s_colour = 1;   /* what is in hand */
static int   s_cx = REEF_COLS / 2, s_cy = REEF_ROWS / 2;   /* where the ghost sits */
static float s_scroll;                            /* the catalogue's scroll, in px */
static float s_scroll0;                           /* ... where the drag started */
static bool  s_erase;

void reef_ui_open(tank_t *t) {
    (void)t; s_menu = false; s_shape = -1; s_rot = 0; s_erase = false;
    s_cx = REEF_COLS / 2; s_cy = REEF_ROWS / 2; s_scroll = 0;
}
void reef_ui_close(void) { s_menu = false; s_shape = -1; }
bool reef_ui_menu_up(void) { return s_menu; }
void reef_ui_hand(int *shape, int *rot, int *colour) {
    if (shape) *shape = s_shape;
    if (rot) *rot = s_rot;
    if (colour) *colour = s_erase ? 0 : s_colour;
}
/* the mask in hand, already turned */
static uint16_t rui_mask(void) {
    return s_shape < 0 ? 0 : reef_shape_rot(REEF_SHAPES[s_shape].mask, s_rot);
}
/* keep the piece on the grid whatever the finger does */
static void rui_clamp(void) {
    uint16_t m = rui_mask();
    if (!m) return;
    int x0, y0, w, h; reef_shape_bounds(m, &x0, &y0, &w, &h);
    if (s_cx + x0 < 0) s_cx = -x0;
    if (s_cy + y0 < 0) s_cy = -y0;
    if (s_cx + x0 + w > REEF_COLS) s_cx = REEF_COLS - w - x0;
    if (s_cy + y0 + h > REEF_ROWS) s_cy = REEF_ROWS - h - y0;
}
/* one shape drawn into a box, used by the catalogue tiles and the ghost */
static void rui_shape_tile(uint16_t *fb, int stride, int x, int y, int cell,
                           uint16_t mask, uint32_t rgb, int alpha) {
    for (int gy = 0; gy < REEF_SHAPE_W; gy++)
        for (int gx = 0; gx < REEF_SHAPE_W; gx++)
            if (mask & (1u << (gy * REEF_SHAPE_W + gx))) {
                if (alpha >= 255) render_rect(fb, stride, x + gx * cell, y + gy * cell, cell - 1, cell - 1, rgb);
                else render_rect_blend(fb, stride, x + gx * cell, y + gy * cell, cell - 1, cell - 1, rgb, alpha);
            }
}

void reef_ui_draw(const tank_t *t, uint16_t *fb, int stride, float clock) {
    const uint32_t INK = 0x031015, PANEL = 0x04141a, TEAL = 0x9fd8e2, FAINT = 0x3f6a72, WHITE = 0xffffff;
    if (!s_menu) {
        /* the canvas is the tank itself, already drawn by the caller. The
           grid is shown faintly so you can see what you are building ON. */
        for (int cx = 1; cx < REEF_COLS; cx++)
            for (int y = 0; y < TANK_H; y += 4)
                render_rect_blend(fb, stride, cx * REEF_CELL, y, 1, 2, TEAL, 18);
        for (int cy = 1; cy < REEF_ROWS; cy++)
            for (int x = 0; x < TANK_W; x += 4)
                render_rect_blend(fb, stride, x, cy * REEF_CELL, 2, 1, TEAL, 18);
        uint16_t m = rui_mask();
        if (m) {                                   /* the piece under the finger */
            int pulse = 150 + (int)(70 * (0.5f + 0.5f * sinf(clock * 4.0f)));
            uint32_t rgb = s_erase ? 0xf25b65 : REEF_PALETTE[s_colour];
            rui_shape_tile(fb, stride, s_cx * REEF_CELL, s_cy * REEF_CELL, REEF_CELL, m, rgb, pulse);
            /* its footprint, so the grid it will land on is unambiguous */
            for (int gy = 0; gy < REEF_SHAPE_W; gy++)
                for (int gx = 0; gx < REEF_SHAPE_W; gx++)
                    if (m & (1u << (gy * REEF_SHAPE_W + gx)))
                        render_rect_edge(fb, stride, (s_cx + gx) * REEF_CELL, (s_cy + gy) * REEF_CELL,
                                         REEF_CELL, REEF_CELL, WHITE);
        }
        /* the foot: what to do next */
        render_rect_blend(fb, stride, 0, RUI_HINT_Y - 6, TANK_W, 28, INK, 190);
        const char *hint = m ? "TAP TO PLACE  -  SWIPE UP FOR MORE" : "SWIPE UP FOR PIECES";
        render_text(fb, stride, (TANK_W - render_text_w(hint, 2)) / 2, RUI_HINT_Y, 2, m ? TEAL : WHITE, hint);
        if (m) {                                   /* turn it, or put it down */
            render_button(fb, stride, 8, RUI_HINT_Y - 44, 74, RUI_FOOT_H, PANEL, TEAL, "TURN", 2);
            render_button(fb, stride, TANK_W - 82, RUI_HINT_Y - 44, 74, RUI_FOOT_H, PANEL, TEAL, "DROP", 2);
        }
        return;
    }
    /* ---- the menu ---- */
    render_rect_blend(fb, stride, 0, 0, TANK_W, TANK_H, INK, 232);
    render_text(fb, stride, RUI_CAT_X, 14, 3, WHITE, "BUILD");
    render_button(fb, stride, TANK_W - 96, 8, 88, 28, PANEL, TEAL, "DONE", 2);
    /* the colour bar: circles down the left, the chosen one ringed */
    for (int i = 1; i <= REEF_COLOURS; i++) {
        int slot = i - 1;
        int cy = RUI_MENU_Y + (slot / RUI_BAR_COLS) * RUI_DOT_DY + RUI_DOT_R;
        int cx = 4 + (slot % RUI_BAR_COLS) * RUI_DOT_DX + RUI_DOT_R;
        for (int dy = -RUI_DOT_R; dy <= RUI_DOT_R; dy++) {      /* a filled circle, cheaply */
            int half = (int)(sqrtf((float)(RUI_DOT_R * RUI_DOT_R - dy * dy)));
            render_rect(fb, stride, cx - half, cy + dy, half * 2 + 1, 1, REEF_PALETTE[i]);
        }
        if (!s_erase && i == s_colour)
            render_rect_edge(fb, stride, cx - RUI_DOT_R - 3, cy - RUI_DOT_R - 3,
                             RUI_DOT_R * 2 + 7, RUI_DOT_R * 2 + 7, WHITE);
    }
    /* and the rubber, at the foot of the bar */
    { int ey = RUI_MENU_Y + RUI_BAR_ROWS * RUI_DOT_DY + 6;
      render_button(fb, stride, 4, ey, RUI_BAR_W - 8, 28, s_erase ? 0x3a1418 : PANEL, 0xf25b65, "RUB", 2); }
    /* the catalogue, scrolled */
    for (int i = 0; i < REEF_SHAPE_N; i++) {
        int col = i % RUI_CAT_COLS, row = i / RUI_CAT_COLS;
        int x = RUI_CAT_X + col * (RUI_TILE + RUI_TILE_PAD);
        int y = RUI_MENU_Y + row * (RUI_TILE + RUI_TILE_PAD) - (int)s_scroll;
        if (y + RUI_TILE < RUI_MENU_Y - 4 || y > RUI_FOOT_Y) continue;
        render_rect(fb, stride, x, y, RUI_TILE, RUI_TILE, PANEL);
        render_rect_edge(fb, stride, x, y, RUI_TILE, RUI_TILE, i == s_shape ? WHITE : FAINT);
        uint16_t m = REEF_SHAPES[i].mask;
        int x0, y0, w, h; reef_shape_bounds(m, &x0, &y0, &w, &h);
        int cell = 14;
        int ox = x + (RUI_TILE - w * cell) / 2 - x0 * cell;
        int oy = y + (RUI_TILE - h * cell) / 2 - y0 * cell + 4;
        rui_shape_tile(fb, stride, ox, oy, cell, m, REEF_PALETTE[s_erase ? 14 : s_colour], 255);
        render_text(fb, stride, x + (RUI_TILE - render_text_w(REEF_SHAPES[i].name, 1)) / 2, y + RUI_TILE - 12,
                    1, FAINT, REEF_SHAPES[i].name);
    }
    render_rect(fb, stride, 0, RUI_FOOT_Y, TANK_W, TANK_H - RUI_FOOT_Y, INK);
    render_text(fb, stride, RUI_CAT_X, RUI_FOOT_Y + 5, 2, FAINT, "DRAG TO SCROLL  -  TAP A SHAPE");
}

/* ---- the touches -------------------------------------------------------
 * The canvas and the menu want different things from the same three calls,
 * so each one asks which it is first. Coordinates are SCREEN space; the
 * builder owns the whole glass while it is up, so there is no camera to
 * undo here. */
void reef_ui_press(tank_t *t, float x, float y) {
    (void)t;
    if (s_menu) { s_scroll0 = s_scroll; return; }        /* a drag will scroll the catalogue */
    if (rui_mask()) { s_cx = (int)(x / REEF_CELL) - 1; s_cy = (int)(y / REEF_CELL) - 1; rui_clamp(); }
}
void reef_ui_drag(tank_t *t, float x, float y) {
    (void)t;
    if (s_menu) return;                                   /* scrolling is done on the release's travel */
    if (rui_mask()) { s_cx = (int)(x / REEF_CELL) - 1; s_cy = (int)(y / REEF_CELL) - 1; rui_clamp(); }
}
/* the catalogue's scroll, clamped to what there is */
static void rui_scroll_to(float v) {
    int rows = (REEF_SHAPE_N + RUI_CAT_COLS - 1) / RUI_CAT_COLS;
    float span = (float)(rows * (RUI_TILE + RUI_TILE_PAD)) - (float)(RUI_FOOT_Y - RUI_MENU_Y);
    if (span < 0) span = 0;
    s_scroll = v < 0 ? 0 : v > span ? span : v;
}
int reef_ui_tap(tank_t *t, float x, float y, float dx, float dy) {
    bool swipe_up = dy < -40 && fabsf(dx) < fabsf(dy);
    if (!s_menu) {
        if (swipe_up) { s_menu = true; return REEF_UI_KEPT; }        /* the whole interface */
        uint16_t m = rui_mask();
        if (m) {
            if (y >= RUI_HINT_Y - 44 && y < RUI_HINT_Y - 44 + RUI_FOOT_H) {
                if (x < 96)            { s_rot = (s_rot + 1) & 3; rui_clamp(); return REEF_UI_KEPT; }   /* TURN */
                if (x >= TANK_W - 96)  { s_shape = -1; return REEF_UI_KEPT; }                            /* DROP */
            }
            if (y < RUI_HINT_Y - 6) {                                /* the canvas: stamp it */
                s_cx = (int)(x / REEF_CELL) - 1; s_cy = (int)(y / REEF_CELL) - 1; rui_clamp();
                reef_stamp(t, m, s_cx, s_cy, (uint8_t)(s_erase ? 0 : s_colour));
                return REEF_UI_KEPT;
            }
        }
        return REEF_UI_NONE;
    }
    /* ---- the menu ---- */
    if (dy > 40 && fabsf(dx) < fabsf(dy)) { s_menu = false; return REEF_UI_KEPT; }   /* swipe back down */
    if (fabsf(dy) > 24) { rui_scroll_to(s_scroll0 - dy); return REEF_UI_KEPT; }      /* a drag scrolls */
    if (y < 40 && x >= TANK_W - 104) { s_menu = false; return REEF_UI_CLOSE; }       /* DONE */
    if (x < RUI_BAR_W) {                                                             /* the colour bar */
        int ey = RUI_MENU_Y + RUI_BAR_ROWS * RUI_DOT_DY + 6;
        if (y >= ey && y < ey + 32) { s_erase = !s_erase; return REEF_UI_KEPT; }     /* the rubber */
        int row = (int)((y - RUI_MENU_Y) / RUI_DOT_DY);
        int col = (int)((x - 4) / RUI_DOT_DX);
        if (col < 0) col = 0; if (col >= RUI_BAR_COLS) col = RUI_BAR_COLS - 1;
        int i = row * RUI_BAR_COLS + col + 1;
        if (row >= 0 && i >= 1 && i <= REEF_COLOURS) { s_colour = i; s_erase = false; }
        return REEF_UI_KEPT;
    }
    for (int i = 0; i < REEF_SHAPE_N; i++) {                                         /* a shape: take it */
        int col = i % RUI_CAT_COLS, row = i / RUI_CAT_COLS;
        int tx = RUI_CAT_X + col * (RUI_TILE + RUI_TILE_PAD);
        int ty = RUI_MENU_Y + row * (RUI_TILE + RUI_TILE_PAD) - (int)s_scroll;
        if (y > RUI_FOOT_Y) break;                                                   /* the footer is not a tile */
        if (x >= tx && x < tx + RUI_TILE && y >= ty && y < ty + RUI_TILE) {
            s_shape = i; s_rot = 0; s_menu = false; rui_clamp();
            return REEF_UI_KEPT;
        }
    }
    return REEF_UI_KEPT;
}
