/* ui_ext.h - UI this fork adds on top of render.c (2026-09-20).
 *
 * Both of these draw with nothing but the primitives render.h already
 * exports, so render.c - the third most-churned file upstream - needs no
 * edits at all and an upstream sync never conflicts here.
 *
 *  - ui_battery_bolt: the always-on charge indicator, top right.
 *  - ui_fish_page:    a fish's own page, behind MORE.
 */
#ifndef POCKET_TANK_UI_EXT_H
#define POCKET_TANK_UI_EXT_H
#include "tank.h"

/* A lightning bolt in the top right corner, lit by how much charge is left:
 * green above 75%, yellow above 50%, orange above 25%, red at or below it,
 * and the charger's teal while charging. It breathes, and it breathes faster
 * the emptier it gets, so the tank says something is wrong from across a
 * room. Draw it AFTER render_tank, like the card; it ignores the night dim.
 * `clock` is tank_t.clock. */
void ui_battery_bolt(uint16_t *fb, int stride, float frac, bool charging, float clock);
/* the same quartile the bolt shows, 0 = red .. 3 = green (tests, logs) */
int  ui_battery_level(float frac);

/* ---- a fish's own page (2026-09-20) ----------------------------------
 * The whole fish on one screen: who it is, the eight levels that drive it,
 * what it is doing right now and the last thing that happened to it. Every
 * level is a button - tapping one explains, in plain words, what that bar
 * actually measures and what moves it.
 *
 * It is reached two ways, both of them a MORE button: from the stats card
 * over the live tank, and from the fish's popup on the overview page.
 *
 * ui_fish_page_tap returns UI_FP_CLOSE for the way out (the caller closes
 * the page and calls ui_fish_page_leave), UI_FP_KEPT when a bar's
 * explanation opened or closed, UI_FP_NONE for a tap on nothing. */
enum { UI_FP_NONE = 0, UI_FP_KEPT = 1, UI_FP_CLOSE = 2 };
void ui_fish_page(const tank_t *t, int fish, uint16_t *fb, int stride, float clock);
int  ui_fish_page_tap(const tank_t *t, int fish, float x, float y);
void ui_fish_page_leave(void);

#endif
