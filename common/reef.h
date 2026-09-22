/* reef.h - the reef builder (2026-09-21, rebuilt).
 *
 * The tank's backdrop is yours to build, out of PIXEL-ART CORAL: irregular,
 * chaotic little sprites at the same scale as the badge icons, not blocks.
 * You colour each one as you place it, and they stack.
 *
 * WHAT CHANGED, and why: the first cut took "like Tetris pieces" literally
 * and gave you sixteen 4x4 masks of flat squares. Wrong on both counts. The
 * shapes are sprites now - hand-drawn, none of them a rectangle - and a
 * piece can only go where a real one could: ON THE FLOOR, or touching
 * something already built. Nothing floats in open water.
 *
 * The store is a LIST OF PIECES, not a painted grid. A sprite is not a grid
 * of colours, so there is nothing to paint; and a list is what lets a piece
 * be picked back up whole. Occupancy - which cells are taken, for the
 * adjacency rule - is derived from the list and cached, never saved.
 *
 * This lives in its own pair of files on purpose: render.c and tank.c are
 * upstream's churn, and a sync has nothing to say about a file only this
 * fork has. Only render.h's public primitives are used to draw.
 */
#ifndef POCKET_TANK_REEF_H
#define POCKET_TANK_REEF_H
#include "tank.h"
#include <stddef.h>

/* the placement grid: coarse, because a piece SNAPS to it - it is not the
 * art's resolution, which is one pixel */
#define REEF_CELL    16
#define REEF_COLS    (TANK_W / REEF_CELL)      /* 28 */
#define REEF_ROWS    (TANK_H / REEF_CELL)      /* 23 */
#define REEF_COLOURS 15
#define REEF_MAX     80                        /* pieces in one reef */

/* the save's 322 bytes: a count, then REEF_MAX of {shape, colour, cx, cy} */
#define REEF_SAVE_BYTES 322

extern const uint32_t REEF_PALETTE[REEF_COLOURS + 1];
extern const char *const REEF_COLOUR_NAMES[REEF_COLOURS + 1];

/* ---- the coral ---------------------------------------------------------
 * Each piece is a sprite of SHADE indices: 0 nothing, 1 the deep side, 2 the
 * body, 3 where the light catches. The keeper's colour is applied to those
 * three levels, so one drawing works in any colour. `cw` x `ch` is the
 * footprint in CELLS; the art is that many REEF_CELLs across and down. */
#define REEF_SHAPE_N 12
typedef struct {
    const char *name;
    uint8_t cw, ch;              /* footprint, in cells */
    const char *const *rows;     /* ch * REEF_CELL strings of cw * REEF_CELL chars */
} reef_shape_t;
extern const reef_shape_t REEF_SHAPES[REEF_SHAPE_N];
/* the shade at a pixel of a shape, 0..3 */
uint8_t reef_shape_px(int shape, int px, int py);
/* is this CELL of the shape's footprint occupied by any art at all? */
bool reef_shape_cell(int shape, int cx, int cy);

/* ---- the reef ---------------------------------------------------------- */
typedef struct { uint8_t shape, colour; int8_t cx, cy; } reef_piece_t;
int  reef_count(const tank_t *t);
bool reef_piece(const tank_t *t, int i, reef_piece_t *out);
bool reef_empty(const tank_t *t);
void reef_clear(tank_t *t);
/* is this cell taken by something already built? */
bool reef_occupied(const tank_t *t, int cx, int cy);
/* MAY a piece go here? Only if every cell it wants is free AND at least one
 * of them sits on the floor or touches something already there. This is the
 * whole difference between building a reef and scattering stickers. */
bool reef_can_place(const tank_t *t, int shape, int cx, int cy);
/* Where a piece dropped at column `cx` comes to REST: straight down from
 * `cy` until it would hit something or reach the floor. Coral does not
 * hover, and asking somebody to line a sprite up with the sand by hand was
 * the wrong job to give them - they aim, it settles. Returns the row, or
 * -1 if that column has no room at all. */
int  reef_settle(const tank_t *t, int shape, int cx, int cy);
/* place it; false if reef_can_place says no, or the reef is full */
bool reef_place(tank_t *t, int shape, uint8_t colour, int cx, int cy);
/* 1 where the reef is right behind a point, falling to 0 in open water:
 * what the fish's shadow fades on. Always 0 while the reef is hidden. */
#define REEF_NEAR_IN   10.0f
#define REEF_NEAR_OUT  46.0f
float reef_near(const tank_t *t, float x, float y);
/* the topmost piece covering this cell, or -1 - for picking one back up */
int  reef_at(const tank_t *t, int cx, int cy);
bool reef_remove(tank_t *t, int index);
unsigned reef_epoch(const tank_t *t);

/* draw the reef as the backdrop: over the water, under everything else.
 * Draws NOTHING while tank_t.reef_hide is set - the keeper asked for it out
 * of sight. reef_draw_all ignores that, for the builder's own canvas. */
void reef_draw(const tank_t *t, uint16_t *fb, int stride, float dim);
void reef_draw_all(const tank_t *t, uint16_t *fb, int stride, float dim);
/* one piece on its own, for the ghost and the catalogue's tiles */
void reef_draw_shape(uint16_t *fb, int stride, int x, int y, int shape,
                     uint8_t colour, int alpha, float dim);
/* the same, with `mute` telling it whether this is going into the TANK -
 * where the reef is a backdrop and must not compete with the fish - or onto
 * a page, where you are choosing a colour and need to see it */
void reef_draw_shape_muted(uint16_t *fb, int stride, int x, int y, int shape,
                           uint8_t colour, int alpha, float dim, bool mute);

/* ---- the builder ------------------------------------------------------- */
enum { REEF_UI_NONE = 0, REEF_UI_KEPT, REEF_UI_CLOSE };
void reef_ui_open(tank_t *t);
/* is the builder up? (the reef draws even when hidden while it is) */
bool reef_ui_active(void);
void reef_ui_close(void);
bool reef_ui_menu_up(void);
void reef_ui_draw(const tank_t *t, uint16_t *fb, int stride, float clock);
void reef_ui_press(tank_t *t, float x, float y);
void reef_ui_drag(tank_t *t, float x, float y);
int  reef_ui_tap(tank_t *t, float x, float y, float dx, float dy);
void reef_ui_hand(int *shape, int *colour, bool *rubbing);
/* The magnifier (OUT mode): true while it is up, with the point it is
 * centred on and the zoom. The app drives it - draw the canvas, then
 * render_camera_point + render_camera_apply, THEN reef_ui_draw, so the
 * builder's own chrome stays full size. Touches arrive in screen space and
 * the builder unmaps them itself. */
bool reef_ui_zoom(float *x, float *y, float *z);

#endif

/* ---- finding it (2026-09-21) -------------------------------------------
 * The builder is not on by default. It is FOUND: a sequence on the glass
 * that nobody does by accident -
 *
 *   swipe left, swipe right, swipe up, swipe down,
 *   tap the right third, tap the left third, tap the middle.
 *
 * Feed every gesture on the live tank to reef_combo, in order. It returns
 * true on the last one, and the tank is never disturbed on the way: the
 * caller asks reef_combo_busy() first and, while a run is under way, does
 * NOT let the gesture do its usual job, so the taps raise no fright and the
 * swipe up does not open the overview mid-sequence. A run that stalls for
 * REEF_COMBO_GAP_S forgets itself.
 *
 * Then the tour: rather than dropping somebody into a page they have never
 * seen, the overview opens with UPGRADES flashing, and then the shop opens
 * with the new coral icon flashing, so the way back is something they have
 * been shown rather than told. */
enum { REEF_G_NONE = 0, REEF_G_SWIPE_L, REEF_G_SWIPE_R, REEF_G_SWIPE_U, REEF_G_SWIPE_D,
       REEF_G_TAP_L, REEF_G_TAP_C, REEF_G_TAP_R };
#define REEF_COMBO_GAP_S 3.0f
bool reef_combo(int gesture, float clock);    /* true = that was the last one */
bool reef_combo_busy(void);                   /* a run is under way: leave the tank alone */
int  reef_combo_progress(void);               /* how far in, for a log */
void reef_combo_reset(void);
/* which gesture a press-and-release was, in tank coordinates */
int  reef_gesture_of(float px, float py, float dx, float dy);

/* the tour: 0 none, 1 the overview with UPGRADES flashing, 2 the shop with
 * the coral icon flashing. The platform drives the pages; this owns the
 * flashing and when each stage is done. */
enum { REEF_TOUR_OFF = 0, REEF_TOUR_OVERVIEW, REEF_TOUR_SHOP };
void reef_tour_begin(void);
void reef_tour_tick(float dt);
int  reef_tour_stage(void);
bool reef_tour_lit(void);                     /* is the highlight showing this instant? */
bool reef_tour_stage_done(void);              /* three flashes gone by: move on */
void reef_tour_next(void);
void reef_tour_end(void);
/* the little coral in the shop's top right corner, once it is found */
#define REEF_ICON_X (TANK_W - 52)
#define REEF_ICON_Y 10
#define REEF_ICON_W 40
#define REEF_ICON_H 40
#define REEF_ICON_SHAPE 1      /* a 2x2 coral: a 2x3 one hangs out of the box */
void reef_icon_draw(uint16_t *fb, int stride, bool highlight);
bool reef_icon_hit(float x, float y);
