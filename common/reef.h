/* reef.h - the reef builder (2026-09-21).
 *
 * The tank's background is yours to build. A grid of REEF_CELL px cells
 * covers the whole glass; every cell is either empty or one of REEF_COLOURS
 * colours, and you stamp Tetris-ish pieces into it like Lego. Everything
 * else in the tank - the kelp, the sand, the shop's pieces, the fish - draws
 * in front of it, so this really is the backdrop and nothing else.
 *
 * WHY A PAINTED GRID, not a list of pieces: a grid is a fixed size in the
 * save (REEF_BYTES, two cells to a byte) no matter how much is built, it
 * cannot overflow, and stacking and overlapping are free because a stamp is
 * just paint. Nothing remembers where one piece ends and the next begins,
 * which is exactly how Lego behaves once it is together.
 *
 * This lives in its own pair of files on purpose: render.c and tank.c are
 * upstream's churn, and an upstream sync has nothing to say about a file
 * that only this fork has.
 */
#ifndef POCKET_TANK_REEF_H
#define POCKET_TANK_REEF_H
#include "tank.h"
#include <stddef.h>

#define REEF_CELL    16
#define REEF_COLS    (TANK_W / REEF_CELL)      /* 28 */
#define REEF_ROWS    (TANK_H / REEF_CELL)      /* 23 */
#define REEF_CELLS   (REEF_COLS * REEF_ROWS)
#define REEF_BYTES   ((REEF_CELLS + 1) / 2)    /* two cells to a byte */
#define REEF_COLOURS 15                        /* 0 is empty, so 1..15 are colours */

/* the palette, as it reads on the colour bar: index 1..REEF_COLOURS */
extern const uint32_t REEF_PALETTE[REEF_COLOURS + 1];
extern const char *const REEF_COLOUR_NAMES[REEF_COLOURS + 1];

/* ---- the catalogue ----------------------------------------------------
 * Every piece is a 4x4 mask, so one shape covers everything from a single
 * stud to a 3x3 block, and rotation is the same operation for all of them. */
#define REEF_SHAPE_N 16
#define REEF_SHAPE_W 4
typedef struct { const char *name; uint16_t mask; } reef_shape_t;   /* row-major, bit 0 = top-left */
extern const reef_shape_t REEF_SHAPES[REEF_SHAPE_N];
/* the mask turned `rot` quarter-turns clockwise */
uint16_t reef_shape_rot(uint16_t mask, int rot);
/* the mask's extent, so a piece can be centred on the finger and kept on the grid */
void reef_shape_bounds(uint16_t mask, int *x0, int *y0, int *w, int *h);

/* ---- the grid ---------------------------------------------------------- */
uint8_t reef_get(const tank_t *t, int cx, int cy);          /* 0 = empty */
void    reef_set(tank_t *t, int cx, int cy, uint8_t colour);
bool    reef_empty(const tank_t *t);
void    reef_clear(tank_t *t);
/* stamp a piece with its top-left at (cx, cy); colour 0 erases those cells.
 * Cells off the grid are dropped. Returns how many cells changed. */
int     reef_stamp(tank_t *t, uint16_t mask, int cx, int cy, uint8_t colour);
/* bumped by every change, so the scene cache knows to rebuild */
unsigned reef_epoch(const tank_t *t);

/* draw the reef as the backdrop. Call it over the water gradient and under
 * everything else; `dim` is the night dim in force. */
void reef_draw(const tank_t *t, uint16_t *fb, int stride, float dim);

#endif
