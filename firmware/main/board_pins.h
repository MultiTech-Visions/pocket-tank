/* board_pins.h — the two boards this firmware runs on.
 *
 * BOARD_AMOLED18 (the default, and the one the gift devices are):
 *   Waveshare ESP32-S3-Touch-AMOLED-1.8 - V1 SH8601 + FT3168, V2 CO5300 +
 *   CST816, told apart at boot by an I2C probe. 368x448 portrait panel, the
 *   tank drawn landscape 448x368 and rotated 90 degrees on the way out.
 *   Sources: Waveshare's esp-idf examples and the official Arduino variant.
 *   VERIFY I2C SDA/SCL on the bench: Waveshare's own code says SDA=15/SCL=14,
 *   the Arduino variant says the reverse.
 *
 * BOARD_LCD154 (2026-09-22):
 *   Waveshare ESP32-S3-Touch-LCD-1.54 - ST7789 over plain SPI, CST816 touch,
 *   240x240. A quarter of the glass, so the 448x368 frame is SQUASHED to fit
 *   (display_port_st7789.c) rather than the whole UI being re-laid out. Pins
 *   are from Waveshare's own BSP for the board
 *   (examples/.../06_esp-brookesia/components/esp32_s3_touch_lcd_1_54) and
 *   their ESP-IDF LVGL example, not guessed.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"          /* a firmware build; a host test defines the board itself */
#endif

#ifdef CONFIG_POCKET_TANK_BOARD_LCD154
/* ---- Waveshare ESP32-S3-Touch-LCD-1.54 ---- */
#define PIN_LCD_SCLK      38
#define PIN_LCD_MOSI      39
#define PIN_LCD_RST       40
#define PIN_LCD_DC        45
#define PIN_LCD_CS        21
#define PIN_LCD_BL        46       /* backlight, active HIGH; PWM'd for brightness */
#define PIN_I2C_SDA       42
#define PIN_I2C_SCL       41
#define PIN_TP_INT        48
#define PIN_TP_RST        47
#define I2C_ADDR_CST816   0x15
#define PANEL_W           240      /* the glass, landscape-square */
#define PANEL_H           240
/* the tank frame (TANK_W x TANK_H) squashed to fit the width, centred */
#define PANEL_FIT_W       240
#define PANEL_FIT_H       197      /* 368 * 240 / 448, rounded down */
#define PANEL_FIT_Y       ((PANEL_H - PANEL_FIT_H) / 2)     /* 21 px of black top and bottom */
/* audio (ES8311 + NS4150B) and the buttons, from the same BSP */
#define PIN_I2S_MCLK      8
#define PIN_I2S_BCLK      9
#define PIN_I2S_WS        10
#define PIN_I2S_DOUT      12       /* ESP -> codec DSDIN */
#define PIN_AMP_EN        7        /* NS4150B CTRL */
#define PIN_BTN_BOOT      0
#define PIN_BTN_A         5
#define PIN_BTN_B         4
/* This one hangs from its USB socket, which is on the bottom edge, so the
   panel is mounted the other way up from the way it is drawn. The IMU flip
   still works: it is XORed with this, so USB-down reads right side up and
   turning the device over flips it as ever. */
#define BOARD_SCREEN_FLIPPED 1
/* Battery. There is no PMIC and no fuel gauge on this board, but there IS
   charge management (an ETA6096 off the MX1.25 header) and the cell is
   brought to GPIO1 through a 200k/100k divider - so the voltage is
   measurable, a third of it at a time, on ADC1 channel 0. Waveshare's own
   examples read it as 3.3/4096 * 3 * raw. No VBUS sense reaches the chip,
   so "charging" is not knowable here; the meter just shows a level. */
#define BOARD_HAS_FUEL_GAUGE 0
#define PIN_BAT_ADC       1
#define BAT_ADC_DIV       3
/* GPIO46 is a STRAPPING PIN as well as this board's backlight. Holding a
   strapping pad across a reset is how you get a board that comes up dark and
   silent (ROM messages are suppressed when 46 is high at reset) - see
   deep_sleep_now(). */
#define BOARD_BL_IS_STRAP 1

#else
/* ---- Waveshare ESP32-S3-Touch-AMOLED-1.8 (the default) ---- */
#define PIN_LCD_CS        12
#define PIN_LCD_PCLK      11
#define PIN_LCD_DATA0     4
#define PIN_LCD_DATA1     5
#define PIN_LCD_DATA2     6
#define PIN_LCD_DATA3     7
#define PIN_I2C_SDA       15
#define PIN_I2C_SCL       14
#define PIN_TP_INT        21
#define I2C_ADDR_EXPANDER 0x20     /* TCA9554: bit0 LCD_RST, bit1 DSI_PWR_EN, bit2 TOUCH_RST, bit7 SD_CS */
#define I2C_ADDR_FT3168   0x38     /* V1 touch */
#define I2C_ADDR_CST816   0x15     /* V2 touch (probe => V2 board) */
#define PANEL_W           368      /* native portrait */
#define PANEL_H           448
#define V2_PANEL_X_GAP    0x10
/* the ES8311 + NS4150B, as audio_port_es8311.c had them inline */
#define PIN_I2S_MCLK      16
#define PIN_I2S_BCLK      9
#define PIN_I2S_WS        45
#define PIN_I2S_DOUT      8        /* ESP -> codec DSDIN */
#define PIN_AMP_EN        46       /* NS4150B CTRL, 10k pulldown on the board */
#endif

/* defaults for the board that does not set them */
#ifndef BOARD_SCREEN_FLIPPED
#define BOARD_SCREEN_FLIPPED 0
#endif
#ifndef BOARD_HAS_FUEL_GAUGE
#define BOARD_HAS_FUEL_GAUGE 1
#endif
#ifndef PIN_BAT_ADC
#define PIN_BAT_ADC (-1)          /* the AMOLED board has a real fuel gauge */
#define BAT_ADC_DIV 1
#endif
#ifndef BOARD_BL_IS_STRAP
#define BOARD_BL_IS_STRAP 0
#endif

#endif
