/* display_port_st7789.c — Waveshare ESP32-S3-Touch-LCD-1.54 (240x240 ST7789
 * over plain SPI), 2026-09-22.
 *
 * THE SQUASH. Every page in this firmware is laid out in pixels for a
 * 448x368 tank: the stats card is 124x258, the settings rows sit at y46..204,
 * the reef grid is 28x23 cells of 16 px. Re-laying all of that out for a
 * quarter of the area is the real port. This is the other one: the frame is
 * composed at 448x368 exactly as it always was, and SCALED on the way to the
 * panel - 448x368 -> 240x197, centred in the 240x240 glass with a black band
 * above and below.
 *
 * Text pays for it. A 2x glyph is 10x14 px and lands at about 5x7, which is
 * legible on a 1.54" 240 px panel but no better than legible. That is the
 * deal this build makes, and it is why the other board is still the default.
 *
 * The scale is not an integer (0.5357), so nearest-neighbour would drop
 * whole glyph rows and columns and shimmer as things move. Each destination
 * pixel is the average of the 2x2 source block at its footprint instead,
 * which is cheap (four loads, one add per channel) and keeps thin strokes
 * visible as grey rather than dropping them. The source x of each
 * destination column is precomputed once.
 *
 * Touch comes back through display_port_map_touch: the inverse of this,
 * including the black bands.
 */
#include "display_port.h"
#include "board_pins.h"
#include "display_squash.h"
#include "tank.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "st7789";
#define LCD_HOST SPI2_HOST
#define STRIPE_ROWS 24                          /* panel rows per DMA transfer */
#define BL_TIMER    LEDC_TIMER_1
#define BL_CHANNEL  LEDC_CHANNEL_1

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static uint8_t s_brightness = 0xFF;
static uint16_t *s_stripe[2];                   /* PANEL_W x STRIPE_ROWS, DMA-capable; ping-pong */
static SemaphoreHandle_t s_stripe_free;
static i2c_master_bus_handle_t s_i2c;
static bool s_inverted;
static uint16_t s_srcx[PANEL_FIT_W];            /* destination column -> source x */

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
bool board_is_v2(void) { return true; }         /* this board's touch is a CST816 */

void display_port_set_inverted(bool inverted) { s_inverted = inverted; }

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    (void)io; (void)ev; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_stripe_free, &hp);
    return hp == pdTRUE;
}

/* the backlight is a plain GPIO on this board, so brightness is PWM */
static void backlight_init(void) {
    ledc_timer_config_t tcfg = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                                 .timer_num = BL_TIMER, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&tcfg);
    ledc_channel_config_t ccfg = { .gpio_num = PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                   .channel = BL_CHANNEL, .timer_sel = BL_TIMER, .duty = 255, .hpoint = 0 };
    ledc_channel_config(&ccfg);
}

bool display_port_init(void) {
    /* THE BACKLIGHT GOES ON FIRST, before anything that can fail. A dark
       screen and a lit blank screen are different faults and there is no
       other way to tell them apart with the device in your hand: lit means
       the board, the rail and this pin are fine and the problem is further
       in; dark means it never got here at all. */
    backlight_init();
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, 255);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
    ESP_LOGI(TAG, "backlight on (GPIO %d); bringing the panel up", PIN_LCD_BL);

    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .flags.enable_internal_pullup = true };
    bus.glitch_ignore_cnt = 7;
    esp_err_t ierr = i2c_new_master_bus(&bus, &s_i2c);
    if (ierr != ESP_OK) { ESP_LOGE(TAG, "i2c bus: %s", esp_err_to_name(ierr)); s_i2c = NULL; }

    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(PANEL_W * STRIPE_ROWS * 2, MALLOC_CAP_DMA);
        if (!s_stripe[i]) return false;
    }
    s_stripe_free = xSemaphoreCreateCounting(2, 2);
    for (int dx = 0; dx < PANEL_FIT_W; dx++) s_srcx[dx] = (uint16_t)squash_src_x(dx);
    const spi_bus_config_t spi = {
        .sclk_io_num = PIN_LCD_SCLK, .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = PANEL_W * STRIPE_ROWS * 2,
    };
    esp_err_t err = spi_bus_initialize(LCD_HOST, &spi, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) { ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err)); return false; }
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC, .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,             /* Waveshare's own example clocks it here */
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 10,
        .on_color_trans_done = on_trans_done,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io);
    if (err != ESP_OK) { ESP_LOGE(TAG, "panel_io_spi: %s", esp_err_to_name(err)); return false; }
    const esp_lcd_panel_dev_config_t pcfg = { .reset_gpio_num = PIN_LCD_RST,
                                              .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                              .bits_per_pixel = 16 };
    /* NOT ESP_ERROR_CHECK: a panel that will not come up must not take the
       board down with it. Failing here leaves the backlight ON and the tank
       running, which is a device that can be looked at and asked questions,
       instead of a boot loop that shows nothing and says nothing. */
    err = esp_lcd_new_panel_st7789(s_io, &pcfg, &s_panel);
    if (err != ESP_OK) { ESP_LOGE(TAG, "new_panel_st7789: %s", esp_err_to_name(err)); s_panel = NULL; return false; }
    esp_lcd_panel_reset(s_panel);
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) { ESP_LOGE(TAG, "panel_init: %s", esp_err_to_name(err)); s_panel = NULL; return false; }
    esp_lcd_panel_invert_color(s_panel, true);   /* this panel is IPS: inverted */
    esp_lcd_panel_disp_on_off(s_panel, true);
    /* the bands above and below the squashed frame, cleared once */
    memset(s_stripe[0], 0, PANEL_W * STRIPE_ROWS * 2);
    for (int y = 0; y < PANEL_H; y += STRIPE_ROWS) {
        int h = y + STRIPE_ROWS > PANEL_H ? PANEL_H - y : STRIPE_ROWS;
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, PANEL_W, y + h, s_stripe[0]);
    }
    display_port_set_brightness(s_brightness);
    ESP_LOGI(TAG, "panel up: %dx%d, tank frame %dx%d squashed to %dx%d at y%d",
             PANEL_W, PANEL_H, TANK_W, TANK_H, PANEL_FIT_W, PANEL_FIT_H, PANEL_FIT_Y);
    return true;
}

void display_port_sleep(void) {
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}
void display_port_wake(void) {
    if (!s_panel) return;
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_disp_on_off(s_panel, true);
    display_port_set_brightness(s_brightness);
}
void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* screen -> tank: the inverse of the squash (display_squash.h) */
void display_port_map_touch(float px, float py, bool inverted, float *tx, float *ty) {
    squash_unmap(px, py, inverted, tx, ty);
}

void display_port_flush(const uint16_t *fb) {
    if (!s_panel) return;                        /* the panel never came up; the tank still runs */
    int cur = 0;
    for (int dy0 = 0; dy0 < PANEL_FIT_H; dy0 += STRIPE_ROWS) {
        int rows = dy0 + STRIPE_ROWS > PANEL_FIT_H ? PANEL_FIT_H - dy0 : STRIPE_ROWS;
        xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        uint16_t *stripe = s_stripe[cur];
        for (int r = 0; r < rows; r++) {
            int dy = dy0 + r;
            int sy = squash_src_y(s_inverted ? PANEL_FIT_H - 1 - dy : dy);
            uint16_t *dst = stripe + r * PANEL_W;
            if (!s_inverted)
                for (int dx = 0; dx < PANEL_FIT_W; dx++) dst[dx] = squash_px(fb, s_srcx[dx], sy);
            else
                for (int dx = 0; dx < PANEL_FIT_W; dx++) dst[dx] = squash_px(fb, s_srcx[PANEL_FIT_W - 1 - dx], sy);
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, PANEL_FIT_Y + dy0,
                                                  PANEL_FIT_W, PANEL_FIT_Y + dy0 + rows, stripe);
        if (err != ESP_OK) {
            static int logged;
            if (logged++ < 3) ESP_LOGE(TAG, "draw_bitmap dy0=%d: %s", dy0, esp_err_to_name(err));
        }
        cur ^= 1;
    }
}
