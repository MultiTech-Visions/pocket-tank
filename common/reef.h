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
/* the topmost piece covering this cell, or -1 - for picking one back up */
int  reef_at(const tank_t *t, int cx, int cy);
bool reef_remove(tank_t *t, int index);
unsigned reef_epoch(const tank_t *t);

/* draw the reef as the backdrop: over the water, under everything else */
void reef_draw(const tank_t *t, uint16_t *fb, int stride, float dim);
/* one piece on its own, for the ghost and the catalogue's tiles */
void reef_draw_shape(uint16_t *fb, int stride, int x, int y, int shape,
                     uint8_t colour, int alpha, float dim);

/* ---- the builder ------------------------------------------------------- */
enum { REEF_UI_NONE = 0, REEF_UI_KEPT, REEF_UI_CLOSE };
void reef_ui_open(tank_t *t);
void reef_ui_close(void);
bool reef_ui_menu_up(void);
void reef_ui_draw(const tank_t *t, uint16_t *fb, int stride, float clock);
void reef_ui_press(tank_t *t, float x, float y);
void reef_ui_drag(tank_t *t, float x, float y);
int  reef_ui_tap(tank_t *t, float x, float y, float dx, float dy);
void reef_ui_hand(int *shape, int *colour, bool *rubbing);

#endif
