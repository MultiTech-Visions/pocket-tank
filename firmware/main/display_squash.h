/* display_squash.h — fitting a 448x368 tank frame onto a 240x240 panel.
 *
 * The geometry of the 1.54" build, kept apart from the driver that uses it
 * (display_port_st7789.c) so it can be run and checked on a host with no
 * ESP-IDF anywhere near it - which is the only way any of this gets tested
 * before the board is in somebody's hand.
 *
 * The scale is 240/448 = 0.5357, not a half, so nearest-neighbour would drop
 * whole glyph rows and shimmer as things move. Each destination pixel is the
 * average of the 2x2 source block at its footprint instead: four loads and
 * three adds per channel, and a one-pixel stroke survives as grey rather
 * than disappearing.
 */
#ifndef DISPLAY_SQUASH_H
#define DISPLAY_SQUASH_H
#include <stdint.h>
#include <stdbool.h>
#include "tank.h"
#include "board_pins.h"

/* the source x (or y) of destination column (or row) d */
static inline int squash_src_x(int dx) {
    int sx = dx * TANK_W / PANEL_FIT_W;
    return sx > TANK_W - 2 ? TANK_W - 2 : sx;      /* the 2x2 block stays on the frame */
}
static inline int squash_src_y(int dy) {
    int sy = dy * TANK_H / PANEL_FIT_H;
    return sy > TANK_H - 2 ? TANK_H - 2 : sy;
}

/* the 2x2 average at (sx,sy), byte-swapped for the panel's big-endian RGB565 */
static inline uint16_t squash_px(const uint16_t *fb, int sx, int sy) {
    const uint16_t *r0 = fb + sy * TANK_W + sx;
    const uint16_t *r1 = r0 + TANK_W;
    uint32_t a = r0[0], b = r0[1], c = r1[0], d = r1[1];
    uint32_t r = ((a >> 11) + (b >> 11) + (c >> 11) + (d >> 11)) >> 2;
    uint32_t g = (((a >> 5) & 0x3f) + ((b >> 5) & 0x3f) + ((c >> 5) & 0x3f) + ((d >> 5) & 0x3f)) >> 2;
    uint32_t bl = ((a & 0x1f) + (b & 0x1f) + (c & 0x1f) + (d & 0x1f)) >> 2;
    uint16_t v = (uint16_t)((r << 11) | (g << 5) | bl);
    return (uint16_t)((v << 8) | (v >> 8));
}

/* screen (px,py) -> tank (tx,ty): the inverse, black bands included. A touch
 * in a band clamps to the nearest edge of the frame rather than reporting
 * somewhere false. */
static inline void squash_unmap(float px, float py, bool inverted, float *tx, float *ty) {
    float fy = py - PANEL_FIT_Y;
    if (fy < 0) fy = 0;
    if (fy > PANEL_FIT_H - 1) fy = (float)(PANEL_FIT_H - 1);
    if (px < 0) px = 0;
    if (px > PANEL_FIT_W - 1) px = (float)(PANEL_FIT_W - 1);
    /* the flip is about the FRAME, not the glass. The bands are 21 px above
       and 22 below (240-197 is odd), so flipping about the panel first and
       taking the band off after puts every touch one pixel out and jams the
       top row against the clamp. Take the band off first. */
    if (inverted) {
        px = (float)(PANEL_FIT_W - 1) - px;
        fy = (float)(PANEL_FIT_H - 1) - fy;
    }
    *tx = px * (float)TANK_W / PANEL_FIT_W;
    *ty = fy * (float)TANK_H / PANEL_FIT_H;
}
#endif
