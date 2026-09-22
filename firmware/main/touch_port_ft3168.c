/* touch_port_ft3168.c — FT3168 capacitive touch (FT5x06 register family) ->
 * tank_touch_hold / tank_touch_tap, with the same gesture timing as the sim's
 * mouse: press+release < 350 ms with < 24 px displacement = tap (fingertips
 * roll and this panel is 322 ppi); held > 300 ms = hold; a drag down from
 * the top edge = feed at that x; every touched frame streams to
 * tank_touch_drag (a moving stroke wipes algae; a horizontal slash through
 * a canopy trims it). Fish taps hit-test 38 px against the press-time fish
 * snapshot AND the current position - fish move during a tap. While the stats
 * card is up, a tap anywhere on empty glass dismisses it (hunting the same
 * fish again to close it was the old, cumbersome way) and does nothing else.
 * Coordinates are mapped from the portrait panel to the landscape tank. */
#include "touch_port.h"
#include "ui_ext.h"
#include "reef.h"
#include "board_pins.h"
#include "sdkconfig.h"
#include "tank.h"
#include "render.h"
#include "setup.h"
#include "notice.h"
#include "audio_port.h"
#include "progression.h"
#include "display_port.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include <math.h>

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_tp;
static bool s_down; static int64_t s_press_us; static float s_px, s_py;
static float s_lx, s_ly;                          /* LAST touched position (release classification) */
/* With the follow cam live the glass and the water no longer agree: the
 * pages and the card are drawn in SCREEN space, everything in the tank is in
 * TANK space. Both are kept - w* is the same point put back through the
 * camera - and each hit test uses the one it belongs to. At 1x they are
 * identical and this costs nothing. */
static float s_wpx, s_wpy, s_wlx, s_wly;
static float s_fx[N_FISH_MAX], s_fy[N_FISH_MAX];  /* fish positions at press time */
static int s_sel = -1; static int64_t s_sel_us;   /* tapped fish -> stats card */
static bool s_ms;                                 /* milestones page up (its CLOSE button ends it) */
static int  s_fp = -1;                            /* a fish's own page up (ui_fish_page), or -1 */
static bool s_cf; static int64_t s_cf_us; static int s_cf_ans;   /* reset confirm prompt */
static bool s_set;                                /* settings page up (CLOSE returns to the milestones page) */
static bool s_dev;                                /* dev page up (seven taps on the version line) */
static bool s_reef;                               /* the reef builder up (BUILD on the overview) */
static int  s_dev_act;                            /* what its last button asked for; main.c does it */
static bool s_shop;                               /* the shop page up (CLOSE returns to the milestones page) */
static bool s_back;                               /* the settings page's CLOSE just brought the milestones page back: that
                                                     release must not reach the page as a tap on ITS CLOSE (same spot) */
static int  s_shop_act;                           /* an UNLOCK / MOVE tapped: the raw tap code, for main (one-shot) */
static int  s_set_what, s_set_val;                /* a segment tapped: SET_TAP_* + value, for main */
#define CONFIRM_TIMEOUT_US (20LL * 1000000)
static bool s_inverted;                           /* screen 180-flipped: mirror into tank space */
/* Fingers land a little BELOW where the eye aims - the pad rolls onto the
 * glass under the fingertip (phones shift their hit targets down for the
 * same reason; Strato saw it on the swatch rows, 2026-09-13). Reported
 * points move UP by this many px in displayed space; director `touch bias
 * <px>` tunes it live. */
static int s_bias_y = 10;
void touch_port_set_bias(int px) { s_bias_y = px; }
int  touch_port_bias(void) { return s_bias_y; }

void touch_port_set_inverted(bool inverted) { s_inverted = inverted; }
extern i2c_master_bus_handle_t board_i2c_bus(void);
extern bool board_is_v2(void);

bool touch_port_init(void) {
    esp_lcd_panel_io_handle_t io;
#ifdef CONFIG_POCKET_TANK_BOARD_LCD154
    /* the 1.54" board is CST816 only, and its touch reset is a plain GPIO
       rather than a bit on an IO expander */
    const bool v2 = true;
    const int rst_pin = PIN_TP_RST, int_pin = -1;
#else
    const bool v2 = board_is_v2();
    const int rst_pin = -1, int_pin = -1;
#endif
    esp_lcd_panel_io_i2c_config_t io_cfg = v2 ? (esp_lcd_panel_io_i2c_config_t)ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG()
                                              : (esp_lcd_panel_io_i2c_config_t)ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
#ifdef CONFIG_POCKET_TANK_BOARD_LCD154
    io_cfg.dev_addr = I2C_ADDR_CST816;
#else
    io_cfg.dev_addr = v2 ? I2C_ADDR_CST816 : I2C_ADDR_FT3168;
#endif
    io_cfg.scl_speed_hz = 400000;
    if (esp_lcd_new_panel_io_i2c(board_i2c_bus(), &io_cfg, &io) != ESP_OK) { ESP_LOGW(TAG, "no touch io"); return false; }
    esp_lcd_touch_config_t tp_cfg = { .x_max = PANEL_W, .y_max = PANEL_H, .rst_gpio_num = rst_pin, .int_gpio_num = int_pin,
        .levels = { .reset = 0, .interrupt = 0 }, .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 } };
    esp_err_t err = v2 ? esp_lcd_touch_new_i2c_cst816s(io, &tp_cfg, &s_tp)
                       : esp_lcd_touch_new_i2c_ft5x06(io, &tp_cfg, &s_tp);
    if (err != ESP_OK) { ESP_LOGW(TAG, "no %s", v2 ? "CST816" : "FT3168"); return false; }
    ESP_LOGI(TAG, "%s ready", v2 ? "CST816" : "FT3168");
    return true;
}

/* call every frame from the tank task */
void touch_port_poll(tank_t *t) {
    int64_t now = esp_timer_get_time();
    if (s_cf && now - s_cf_us > CONFIRM_TIMEOUT_US) touch_port_confirm_answer(-1);   /* nobody answered: keep the tank */
    if (!s_tp) return;
    uint16_t x[1], y[1], st[1]; uint8_t n = 0;
    esp_lcd_touch_read_data(s_tp);
    bool touched = esp_lcd_touch_get_coordinates(s_tp, x, y, st, &n, 1) && n > 0;
    /* the panel's own geometry belongs to its display port: a rotation on the
       AMOLED, the inverse of the squash on the 1.54" LCD (display_port.h) */
    float tx = s_lx, ty = s_ly;
    if (touched) {
        display_port_map_touch((float)x[0], (float)y[0], s_inverted, &tx, &ty);
        ty -= s_bias_y;
        if (ty < 0) ty = 0;
    }
    float wx = tx, wy = ty;
    render_camera_unmap(tx, ty, &wx, &wy);      /* where in the WATER the finger is */
    if (touched) { s_wlx = wx; s_wly = wy; }
    if (touched && !s_down) {
        audio_port_prewarm();                   /* the release's cue plays warm */
        s_press_us = now; s_px = tx; s_py = ty; s_wpx = wx; s_wpy = wy;
        /* snapshot the school: the user aims at where a fish WAS - by release
           a darting fish has moved and the finger hid it the whole time */
        for (int i = 0; i < t->n_fish && i < N_FISH_MAX; i++) { s_fx[i] = t->fish[i].x; s_fy[i] = t->fish[i].y; }
    }
    bool su = setup_active();                                /* before the touch: BEGIN's release is not a tank tap */
    if (s_set && !s_cf && !su) {                             /* the settings page owns the glass: segments, the seconds wheel, CLOSE */
        int v = 0, r = render_settings_touch(t, tx, ty, touched, &v);
        if (r) ESP_LOGI(TAG, "settings: %s %d", r == SET_TAP_CLOSE ? "CLOSE" : r == SET_TAP_BRIGHT ? "brightness" : r == SET_TAP_VOLUME ? "volume"
                                                  : r == SET_TAP_SPEED ? "fish speed" : r == SET_TAP_LIGHT ? "lights out" : "idle seconds", v);
        if (r == SET_TAP_DEV) { s_set = false; s_dev = true; s_back = true;       /* seven taps on the version line */
                                ESP_LOGI(TAG, "dev page up (CLOSE leaves)"); }
        else if (r == SET_TAP_CLOSE) { s_set = false; s_ms = true; s_back = true; }   /* back to the milestones page (2026-09-16); the release is spent */
        else if (r == SET_TAP_BRIGHT || r == SET_TAP_VOLUME || r == SET_TAP_LIGHT || r == SET_TAP_IDLE ||
                 r == SET_TAP_SPEED) { s_set_what = r; s_set_val = v; }
    }
    if (su && !s_cf) {
        bool birth = setup_is_birth(); int who = setup_fish(), place = setup_item();
        setup_touch(t, tx, ty, touched);                     /* taps and the letter wheel, classified in setup.c */
        if (!setup_active()) {
            if (birth) ESP_LOGI(TAG, "birth flow done: %s named and saved", who >= 0 && who < t->n_fish ? t->fish[who].name : "?");
            else if (place >= 0) { tank_decor_noticed(t, place);   /* it is in: the fish come and look */
                                   ESP_LOGI(TAG, "placed: %s at x %.0f, %s layer, saved", SD_ITEMS[place].name, tank_decor_x(t, place),
                                          tank_decor_z(t, place) == DECOR_Z_BACK ? "BEHIND" : tank_decor_z(t, place) == DECOR_Z_FRONT ? "IN FRONT" : "AMONG"); }
            else ESP_LOGI(TAG, "setup done: %s + %s", t->fish[0].name, t->fish[1].name);
        }
    }
    bool modal = s_ms || s_set || s_dev || s_shop || s_reef || s_cf || su || s_fp >= 0;  /* a page or a prompt owns the glass */
    if (s_reef && !s_cf && !su) {                            /* the builder owns the glass while it is up */
        if (touched && !s_down) reef_ui_press(t, tx, ty);
        else if (touched) reef_ui_drag(t, tx, ty);
    }
    if (s_fp >= 0 && touched && s_down) ui_fish_page_swipe(tx - s_lx, t->clock);   /* the long lines scrub */
    bool on_card = s_sel >= 0 && RENDER_CARD_HIT(s_px, s_py);   /* the card is not glass */
    if (touched) { s_lx = tx; s_ly = ty; if (!modal && !on_card) tank_touch_drag(t, wx, wy); }  /* stroke = wipe/slash */
    if (touched && !modal && !on_card && now - s_press_us > 300000 && fabsf(ty - s_py) < 30) tank_touch_hold(t, wx, wy);
    if (!touched && s_down && !modal && !s_cf && !su) {
        /* The hidden way in (reef.h). Every gesture on the live tank is
           offered to it; once a run is under way the gesture is CONSUMED, so
           the taps raise no fright and the swipe up does not open the
           overview half way through. The first gesture still does its usual
           job, because a stray swipe left must not stop wiping the glass. */
        bool was = reef_combo_busy();
        int g = reef_gesture_of(s_wpx, s_wpy, s_lx - s_px, s_ly - s_py);
        if (reef_combo(g, t->clock)) {
            t->reef_open = 1; progression_save(t);
            s_sel = -1; s_ms = true; reef_tour_begin();
            ESP_LOGI(TAG, "the reef builder has been found");
            s_down = touched; return;
        }
        if (was) { s_down = touched; return; }            /* mid-run: the tank is left alone */
    }
    if (!touched && s_down) {
        /* release: classify with the LAST touched position (the old code fell
           back to the PRESS position here, so dx/dy were always 0 - every
           quick swipe read as a tap and the drag-feed could never fire) */
        float dx = s_lx - s_px, dy = s_ly - s_py;
        /* the card first, and NOT through the tap test below: a thumb on a
           124 px slab on the left edge rolls further than 24 px and takes
           longer than 350 ms, so this used to be read as a stroke and the
           card just sat there (2026-09-21) */
        if (s_reef && !s_cf && !su && (dx * dx + dy * dy >= 24 * 24 || now - s_press_us >= 350000)) {
            int r = reef_ui_tap(t, s_px, s_py, dx, dy);          /* a swipe: the menu, or the catalogue's scroll */
            if (r == REEF_UI_CLOSE) { s_reef = false; s_ms = true; progression_save(t); }
            goto released;
        }
        if (!modal && !s_cf && render_card_opens_page(s_sel, s_px, s_py, s_lx, s_ly)) {
            s_fp = s_sel; s_sel = -1;
            ESP_LOGI(TAG, "card press %.0f,%.0f release %.0f,%.0f -> the %s page", s_px, s_py, s_lx, s_ly, t->fish[s_fp].name);
            goto released;
        }
        if (s_cf) {                     /* the prompt owns the glass: a press AND release on the
                                           same button answers it, nothing else counts - not
                                           even the tap that opened it (it began before) */
            int h = s_press_us > s_cf_us ? render_confirm_hit(s_px, s_py) : 0;
            if (h && h == render_confirm_hit(s_lx, s_ly)) touch_port_confirm_answer(h);
            goto released;
        }
        if (su) {                       /* the setup had the glass (setup_touch above); just the log:
                                           where the finger landed vs what it hit, in case this panel
                                           reports fingers offset from where they feel */
            ESP_LOGI(TAG, "setup touch press %.0f,%.0f release %.0f,%.0f -> %s", s_px, s_py, s_lx, s_ly,
                     setup_hit_name(setup_active() ? setup_hit(s_px, s_py) : 0));
            s_sel = -1; goto released;
        }
        if (now - s_press_us < 350000 && dx * dx + dy * dy < 24 * 24) {
            if (notice_current()) { notice_dismiss(); ESP_LOGI(TAG, "tap closed the announcement"); goto released; }
            if (s_set || s_back) { s_back = false; goto released; }   /* the settings page had the glass (render_settings_touch above) */
            if (s_reef) {                                           /* the builder: swipe up for pieces, tap to place */
                int r = reef_ui_tap(t, s_px, s_py, s_lx - s_px, s_ly - s_py);
                if (r == REEF_UI_CLOSE) { s_reef = false; s_ms = true; progression_save(t); ESP_LOGI(TAG, "reef: done, saved"); }
                else if (r == REEF_UI_KEPT) progression_save(t);     /* every edit is worth keeping at once */
                goto released;
            }
            if (s_dev) {                                            /* the dev page: eight buttons and CLOSE */
                int a = ui_dev_page_tap(s_px, s_py);
                ESP_LOGI(TAG, "dev tap at %.0f,%.0f -> %d", s_px, s_py, a);
                if (a == UI_DEV_CLOSE) { s_dev = false; s_set = true; }
                else if (a > UI_DEV_CLOSE) s_dev_act = a;           /* main.c does it (and plays the cue) */
                goto released;
            }
            if (s_shop) {                                           /* the shop: a row's modal, UNLOCK, HOW TO EARN, CLOSE */
                int r = render_shop_tap(t, s_px, s_py);
                ESP_LOGI(TAG, "shop tap at %.0f,%.0f -> %s", s_px, s_py, r == SHOP_TAP_CLOSE ? "CLOSE" : r == SHOP_TAP_GRANT ? "dev grant" : r >= SHOP_TAP_MOVE ? "MOVE" : r >= SHOP_TAP_BUY ? "UNLOCK" : r == SHOP_TAP_KEPT ? "modal" : "nothing");
                if (r == SHOP_TAP_REEF) {                            /* the coral in the corner */
                    s_shop = false; render_shop_leave(); s_reef = true; reef_ui_open(t); reef_tour_end();
                    ESP_LOGI(TAG, "reef builder up (swipe up for coral, DONE leaves)");
                }
                else if (r == SHOP_TAP_CLOSE) { s_shop = false; render_shop_leave(); s_ms = true; }   /* back to the milestones page (2026-09-16) */
                else if (r == SHOP_TAP_GRANT || r >= SHOP_TAP_BUY) s_shop_act = r;   /* main.c buys (and plays the cue), grants, or opens the placement page */
                goto released;
            }
            if (s_fp >= 0) {                                        /* a fish's page: a bar explains itself, CLOSE leaves */
                int r = ui_fish_page_tap(t, s_fp, s_px, s_py);
                ESP_LOGI(TAG, "fish page tap at %.0f,%.0f -> %s", s_px, s_py,
                         r == UI_FP_CLOSE ? "CLOSE" : r == UI_FP_KEPT ? "a level" : "nothing");
                if (r == UI_FP_CLOSE) { s_fp = -1; ui_fish_page_leave(); }
                goto released;
            }
            if (s_ms) {                                             /* the page: badges open a modal, the CLOSE
                                                                       button ends it, the brightness row cycles */
                int r = render_milestones_tap(t, s_px, s_py);     /* CLOSE / SETTINGS / the sand dollar, detail modal, nothing */
                ESP_LOGI(TAG, "page tap at %.0f,%.0f (release %.0f,%.0f) -> %s", s_px, s_py, s_lx, s_ly,
                         r == MS_TAP_CLOSE ? "CLOSE" : r == MS_TAP_SETTINGS ? "SETTINGS" : r == MS_TAP_SHOP ? "SHOP" : r == MS_TAP_KEPT ? "detail" : "nothing");
                if (r >= MS_TAP_FISH) {                          /* a fish popup's MORE: into that fish's page */
                    s_fp = r - MS_TAP_FISH; s_ms = false; s_sel = -1;
                    progression_ack_milestones(t); render_milestones_leave();
                    ESP_LOGI(TAG, "fish page: %s, from the overview", t->fish[s_fp].name);
                    goto released;
                }
                if (r != MS_TAP_CLOSE && r != MS_TAP_SETTINGS && r != MS_TAP_SHOP) goto released;   /* only a button leaves the page */
                s_ms = false; s_sel = -1; s_set = r == MS_TAP_SETTINGS; s_shop = r == MS_TAP_SHOP;
                progression_ack_milestones(t); render_milestones_leave();   /* everything shown is now "seen" */
                goto released;
            }
            /* fish first; only an empty tap reaches the water. 38 px radius
               (a fingertip on this 322 ppi panel covers ~60 px) against BOTH
               the press-time snapshot and the current position - whichever is
               closer - so a fish that moved mid-tap still registers. */
            int best = -1; float bd = 38 * 38;
            for (int i = 0; i < t->n_fish; i++) {
                float ax = s_fx[i] - s_wpx, ay = s_fy[i] - s_wpy;
                float bx = t->fish[i].x - s_wpx, by = t->fish[i].y - s_wpy;
                float d2a = ax * ax + ay * ay, d2b = bx * bx + by * by;
                float d2 = d2a < d2b ? d2a : d2b;
                if (d2 < bd) { bd = d2; best = i; }
            }
            if (best >= 0) { s_sel = (best == s_sel) ? -1 : best; s_sel_us = now; }
            else if (tank_disco_hit(t, s_wpx, s_wpy)) {   /* the ball: the keeper's own show */
                tank_disco_toggle(t);
                ESP_LOGI(TAG, "disco ball: %s", t->disco_show_s > 0 ? "lowering, show on" : "show off");
            }
            else if (tank_snail_hit(t, s_wpx, s_wpy)) {   /* the snail: its card (2026-09-16), the fish first */
                s_sel = s_sel == RENDER_CARD_SNAIL ? -1 : RENDER_CARD_SNAIL; s_sel_us = now;
                ESP_LOGI(TAG, "snail tapped: card %s (%d spots grazed)", s_sel >= 0 ? "up" : "down", (int)t->snail_grazed); }
            else if (s_sel >= 0) s_sel = -1;   /* card up: a tap on empty glass just
                                                  dismisses it - it is NOT a tank tap
                                                  (no feed, no light-toggle burst) */
            else tank_touch_tap(t, s_wpx, s_wpy);
        }
        else if (!s_ms && s_fp < 0 && s_py < 60 && dy >= 40) tank_feed(t, s_wlx, 3);  /* drag down from the top = feed */
        else if (!s_ms && !s_set && !s_shop && s_fp < 0 && s_py > TANK_H - 70 && dy <= -40) {
            s_ms = true; s_sel = -1;                                      /* swipe up from the bottom = the overview */
            ESP_LOGI(TAG, "swipe up from %.0f,%.0f -> the overview page", s_px, s_py);
        }
    }
released:
    s_down = touched;
    if (s_sel >= t->n_fish && s_sel != RENDER_CARD_SNAIL) s_sel = -1;   /* fresh tank / save load */
    if (s_fp >= t->n_fish) { s_fp = -1; ui_fish_page_leave(); }         /* ... the page too */
    if (s_sel >= 0 && now - s_sel_us > 10 * 1000000) s_sel = -1; /* auto-dismiss */
}

int touch_port_selected(void) { return s_sel; }
bool touch_port_milestones(void) { return s_ms; }
int  touch_port_fishpage(void) { return s_fp; }
void touch_port_show_fishpage(int fish) { if (s_fp >= 0 && fish < 0) ui_fish_page_leave(); s_fp = fish; if (fish >= 0) { s_ms = false; s_set = false; s_shop = false; s_sel = -1; } }
void touch_port_show_milestones(bool on) { if (s_ms && !on) render_milestones_leave(); s_ms = on; }
void touch_port_dismiss(void) { s_sel = -1; if (s_ms) render_milestones_leave(); if (s_shop) render_shop_leave(); if (s_fp >= 0) ui_fish_page_leave(); s_ms = false; s_set = false; s_dev = false; s_shop = false; s_fp = -1; }

/* ---- reset confirm prompt ---- */
void touch_port_confirm_open(void) {
    s_cf = true; s_cf_us = esp_timer_get_time(); s_cf_ans = 0;
    s_sel = -1; s_ms = false; s_set = false; s_dev = false; s_shop = false; s_reef = false; s_fp = -1; ui_fish_page_leave(); render_shop_leave();   /* it replaces the card / the pages */
    ESP_LOGI(TAG, "reset prompt up (YES / NO on the glass; NO by itself in %d s)", (int)(CONFIRM_TIMEOUT_US / 1000000));
}
bool touch_port_confirm_answer(int ans) {
    if (!s_cf) return false;
    s_cf = false; s_cf_ans = ans > 0 ? 1 : -1;
    return true;
}
bool  touch_port_confirm_up(void)   { return s_cf; }
float touch_port_confirm_frac(void) {
    if (!s_cf) return 0;
    float f = 1.0f - (esp_timer_get_time() - s_cf_us) / (float)CONFIRM_TIMEOUT_US;
    return f < 0 ? 0 : f;
}
int  touch_port_confirm_take(void)  { int a = s_cf_ans; s_cf_ans = 0; return a; }
bool touch_port_pressed_since(int64_t us) { return s_down && s_press_us > us; }
bool touch_port_dev(void) { return s_dev; }
bool touch_port_reef(void) { return s_reef; }
int  touch_port_take_dev(void) { int a = s_dev_act; s_dev_act = 0; return a; }
bool touch_port_settings(void) { return s_set; }
void touch_port_show_settings(bool on) { s_set = on; if (on) { s_ms = false; s_sel = -1; } }
int  touch_port_take_setting(int *value) { int w = s_set_what; *value = s_set_val; s_set_what = 0; return w; }
bool touch_port_shop(void) { return s_shop; }
void touch_port_show_shop(bool on) { if (s_shop && !on) render_shop_leave(); s_shop = on; if (on) { s_ms = false; s_set = false; s_sel = -1; } }
int  touch_port_take_shop(void) { int r = s_shop_act; s_shop_act = 0; return r; }
