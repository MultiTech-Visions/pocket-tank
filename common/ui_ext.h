/* ui_ext.h - UI this fork adds on top of render.c (2026-09-20).
 *
 * Both of these draw with nothing but the primitives render.h already
 * exports, so render.c - the third most-churned file upstream - needs no
 * edits at all and an upstream sync never conflicts here.
 *
 *  - ui_battery_bolt: the always-on charge indicator, top right.
 *  - ui_fish_page:    a fish's own page, behind MORE.
 *  - ui_dev_page:     the hidden workbench, behind seven taps on the version.
 */
#ifndef POCKET_TANK_UI_EXT_H
#define POCKET_TANK_UI_EXT_H
#include "tank.h"
#include <stddef.h>

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
/* a horizontal drag across the page takes the long DOING / LAST lines over
 * from the walk for a few seconds: `dx` is this frame's travel in px, `clock`
 * is tank_t.clock. A line short enough to fit ignores it. */
void ui_fish_page_swipe(float dx, float clock);
#define FP_SCRUB_HOLD_S 4.0f   /* how long that swipe holds the walk off */

/* ---- the dev page (2026-09-21) --------------------------------------
 * The phone trick: tap the FW version line at the bottom of the settings
 * page SET_DEV_TAPS times in a row (render.h) and this opens. Seven buttons for looking
 * at things that otherwise take hours of play or a serial console - money,
 * the whole shelf, a fish's age, a bass party on cue, the light, a fry, and
 * the battery bolt's four colours.
 *
 * Nothing here is drawn anywhere else and nothing reaches it by accident:
 * any other tap on the settings page resets the count.
 *
 * ui_dev_page_tap only says what was asked for; ui_dev_apply does it and
 * writes the one-line result into `status`, so the sim and the firmware
 * share the actions instead of each keeping a copy. It returns false for
 * UI_DEV_BATTERY alone, which is the platform's own (the faked charge lives
 * beside its battery port, not in the tank) - the caller steps that itself
 * and writes its own status line. `status` is drawn under the buttons; pass
 * NULL or "" for none. */
enum { UI_DEV_NONE = 0, UI_DEV_KEPT, UI_DEV_CLOSE,
       UI_DEV_DOLLARS,      /* + SD_DEV_GRANT sand dollars */
       UI_DEV_BROKE,        /* the balance back to nothing, to see a short one */
       UI_DEV_UNLOCK_ALL,   /* buy the whole shelf, placed where it defaults */
       UI_DEV_GROW,         /* push a fish to its next stage */
       UI_DEV_PARTY,        /* lift the totem now: parade, party, march home */
       UI_DEV_FRY,          /* a fry arrives */
       UI_DEV_BATTERY };    /* step the faked charge: 100 -> 60 -> 40 -> 10 -> real */
/* this fork's own milestone bits (MS_LOCAL_BIT0 and up), for the announcement
 * render.c puts over the live tank: its tables only reach the upstream ones.
 * Returns the badge icon and, through `name`, what to call it - NULL for a bit
 * that is not one of ours. */
#include "icons.h"
const icon_t *ui_local_ms(uint32_t bit, const char **name);
void ui_dev_page(const tank_t *t, uint16_t *fb, int stride, const char *status);
int  ui_dev_page_tap(float x, float y);
bool ui_dev_apply(tank_t *t, int act, char *status, size_t n);
/* the faked charges UI_DEV_BATTERY walks, so both platforms step the same
 * four colours: 100% green, 60% yellow, 40% orange, 10% red, then the real
 * reading again. ui_dev_battery_next returns the next one after `frac`, or a
 * negative number for "back to the real battery". */
float ui_dev_battery_next(float frac);

#endif
