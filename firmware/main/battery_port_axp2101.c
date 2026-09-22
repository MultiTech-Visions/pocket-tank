/* battery_port_axp2101.c — battery fraction + charge state from the AXP2101
 * PMIC's fuel gauge (I2C 0x34): STATUS1 bit3 = battery present, STATUS2
 * bits[7:5] == 1 = charging, 0xA4 = state of charge in percent. Reads are
 * cached for 5 s; any I2C error or absent battery hides the meter. */
#include "battery_port.h"
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <strings.h>
#include <string.h>
#if PIN_BAT_ADC >= 0
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#endif

#define AXP2101_ADDR      0x34
#define REG_STATUS1       0x00
#define REG_STATUS2       0x01
#define REG_COMMON_CFG    0x10
#define REG_BAT_PERCENT   0xA4

static i2c_master_dev_handle_t s_dev;

/* ---- the OTHER board's meter ----------------------------------------
 * The 1.54in board has no PMIC and no fuel gauge, so none of the register
 * reads below answer. It does have an ETA6096 charge manager and the cell on
 * GPIO1 behind a 200k/100k divider, which is enough for a level: read the
 * ADC, multiply by the divider, and put the millivolts through a discharge
 * curve. Everything public in this file goes through adc_mv()/curve() when
 * the PMIC is absent, so the rest of the firmware never learns there are two
 * kinds of battery.
 *
 * "Charging" is NOT knowable here - no VBUS sense and no /CHG pin reach the
 * chip - so it is reported false rather than guessed at. On USB the voltage
 * simply reads high. */
#if PIN_BAT_ADC >= 0
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

static void adc_init(void) {
    adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&u, &s_adc) != ESP_OK) { s_adc = NULL; return; }
    adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    /* GPIO1 is ADC1 channel 0 on the S3, and the channels run with the pin */
    if (adc_oneshot_config_channel(s_adc, (adc_channel_t)(PIN_BAT_ADC - 1), &c) != ESP_OK) {
        adc_oneshot_del_unit(s_adc); s_adc = NULL; return;
    }
    adc_cali_curve_fitting_config_t cal = { .unit_id = ADC_UNIT_1, .chan = (adc_channel_t)(PIN_BAT_ADC - 1),
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;
    ESP_LOGI("battery", "no PMIC: battery read on GPIO%d (ADC1 ch%d, /%d divider)%s",
             PIN_BAT_ADC, PIN_BAT_ADC - 1, BAT_ADC_DIV, s_cali ? ", calibrated" : ", uncalibrated");
}

/* eight reads: the divider is high-impedance and the panel's backlight PWM
   is on the same board, so a single sample wanders by tens of millivolts */
static int adc_mv(void) {
    if (!s_adc) return 0;
    int sum = 0, n = 0;
    for (int i = 0; i < 8; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, (adc_channel_t)(PIN_BAT_ADC - 1), &raw) != ESP_OK) continue;
        if (s_cali) { if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) continue; }
        else mv = raw * 3300 / 4095;                 /* uncalibrated: the nominal full scale */
        sum += mv; n++;
    }
    return n ? sum * BAT_ADC_DIV / n : 0;
}

/* a single 3.7 V cell under the tank's own load, in 5 % steps off the flat
   part of the curve. Below 3.30 V the ESP32-S3 is close to browning out
   anyway, so that is the floor rather than the cell's datasheet 3.0 V. */
static float curve(int mv) {
    static const short pt[][2] = { {4180,100},{4100,95},{4020,90},{3950,80},{3890,70},{3840,60},
                                   {3790,50},{3750,40},{3710,30},{3660,20},{3600,12},{3520,6},{3400,2},{3300,0} };
    int n = (int)(sizeof pt / sizeof pt[0]);
    if (mv >= pt[0][0]) return 1.0f;
    for (int i = 1; i < n; i++) {
        if (mv >= pt[i][0]) {
            float t = (float)(mv - pt[i][0]) / (pt[i - 1][0] - pt[i][0]);
            return (pt[i][1] + t * (pt[i - 1][1] - pt[i][1])) / 100.0f;
        }
    }
    return 0.0f;
}
#else
static void adc_init(void) {}
static int  adc_mv(void) { return 0; }
static float curve(int mv) { (void)mv; return 0.0f; }
#endif

static int64_t s_last_us = -1;
static float s_frac; static bool s_charging, s_valid;
static bool rd(uint8_t reg, uint8_t *val);

bool battery_port_init(i2c_master_bus_handle_t bus) {
    if (!bus || i2c_master_probe(bus, AXP2101_ADDR, 50) != ESP_OK) {
        ESP_LOGW("battery", "no AXP2101");
        adc_init();      /* the meter may still be readable; the PMIC is not */
        return false;    /* the caller's s_pmic: no PWR key, no soft power-off */
    }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = AXP2101_ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return false;
    /* charge current: the PMIC's default read back as code 0x11, past the
     * 1000 mA (code 16) top of the table - several C for the 200 mAh 302530
     * cell on this board. 0.5C = 100 mA (code 4; codes 0-8 are 25 mA steps)
     * charges it in ~2.5 h and is kind to it; 200 mA (code 8) is the 1C
     * alternative. CV stays at the default 4.2 V (code 3). */
    uint8_t icc;
    if (rd(0x62, &icc)) {
        uint8_t wr[2] = { 0x62, (uint8_t)((icc & 0xE0) | 4) };
        bool ok = i2c_master_transmit(s_dev, wr, 2, 100) == ESP_OK;
        ESP_LOGI("battery", "AXP2101 fuel gauge up; charge current code %d -> %s", icc & 0x1F, ok ? "4 (100 mA)" : "unchanged (write failed)");
    } else ESP_LOGI("battery", "AXP2101 fuel gauge up");
    return true;
}

static bool rd(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100) == ESP_OK;
}

/* COMMON_CONFIG bit0 = soft power-off: every rail drops within ms, draw falls
 * to the PMIC's quiescent few uA. A PWR-button press powers the board back on. */
bool battery_port_poweroff(void) {
#ifdef PIN_BAT_EN
    if (!s_dev) {                      /* the 1.54in LCD: let go of the latch. On battery the rails drop here;
                                          on USB nothing happens and the caller deep-sleeps (PWR boots it). */
        gpio_set_level(PIN_BAT_EN, 0);
        return true;
    }
#endif
    if (!s_dev) return false;
    uint8_t v;
    if (!rd(REG_COMMON_CFG, &v)) return false;
    uint8_t wr[2] = { REG_COMMON_CFG, (uint8_t)(v | 0x01) };
    return i2c_master_transmit(s_dev, wr, 2, 100) == ESP_OK;
}

/* ---- the PWR key (2026-09-16): REG 41 IRQ enables, REG 49 IRQ status 1
 * (RW1C; bit 3 short press, bit 2 long press), REG 27 IRQLEVEL 5:4 /
 * OFFLEVEL 3:2 / ONLEVEL 1:0. Polled from the tank task; no IRQ line needed. */
static bool wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return s_dev && i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK;
}
void battery_port_key_init(void) {
    if (!s_dev) return;
    uint8_t v, v27 = 0;
    /* REG 27: IRQLEVEL 5:4 (the long press: 1 / 1.5 / 2 / 2.5 s), OFFLEVEL 3:2
     * (the PMIC's own cut: 4 / 6 / 8 / 10 s), ONLEVEL 1:0 - how long PWRON must
     * be HELD to power the board on from a power-off (128 / 512 ms, 1 / 2 s).
     * ONLEVEL is the one that decides how a wake from the night feels: at the
     * chip's default a tap is shorter than the hold and does nothing, and the
     * keeper presses again (2026-09-16: "three presses"). 128 ms = a tap. */
    static const char *ON[4] = { "128 ms", "512 ms", "1 s", "2 s" }, *IRQ[4] = { "1 s", "1.5 s", "2 s", "2.5 s" };
    { uint8_t r20 = 0, r21 = 0, r48 = 0, r49 = 0, r4a = 0, r10 = 0;   /* what the PMIC saw before this boot: power-on / power-off source, the pending IRQ flags */
      rd(0x20, &r20); rd(0x21, &r21); rd(0x48, &r48); rd(0x49, &r49); rd(0x4A, &r4a); rd(0x10, &r10);
      ESP_LOGI("battery", "PMIC at boot: power-on source %02x, power-off source %02x, common cfg %02x | IRQ status %02x %02x %02x (49: bit3 short press, bit2 long, bit1 release, bit0 press)",
               r20, r21, r10, r48, r49, r4a); }
    bool ok = rd(0x27, &v27) && wr(0x27, (uint8_t)((v27 & ~0x0F) | 0x0C));   /* OFFLEVEL 10 s (the firmware's 1.5 s long press saves + cuts first), ONLEVEL 128 ms */
    ok = rd(0x41, &v) && wr(0x41, (uint8_t)(v | 0x0C)) && ok;           /* short + long press IRQs on (the defaults, made sure of) */
    ok = wr(0x48, 0xFF) && wr(0x49, 0xFF) && wr(0x4A, 0xFF) && ok;       /* the power-on press itself: cleared */
    ESP_LOGI("battery", "PWR key%s: REG 27 was %02x (power-on hold %s, long press %s) -> power-on hold 128 ms; short press = sleep, %s = power-off, 10 s = the PMIC's own cut",
             ok ? "" : " (a register write FAILED)", v27, ON[v27 & 3], IRQ[(v27 >> 4) & 3], IRQ[(v27 >> 4) & 3]);
}
int battery_port_key_poll(void) {
    if (!s_dev) return 0;
    uint8_t st;
    if (!rd(0x49, &st) || !(st & 0x0C)) return 0;
    wr(0x49, (uint8_t)(st & 0x0C));                                     /* clear what was taken */
    return (st & 0x04) ? 2 : 1;
}

/* bench (director `keytime N`, 2026-09-16): how long are the keeper's presses?
 * Polls REG 49 every ~2 ms for N s and logs each press from the PWRON falling
 * edge (bit 1) to the rising edge (bit 0). The short / long flags are swallowed
 * with them, so a press inside the trace never sleeps the tank. The point: a
 * powered-off PMIC only powers on for a hold longer than ONLEVEL (128 ms, the
 * shortest it offers) - a lighter tap does nothing at all, and the keeper
 * presses again. This tells whether the taps land under it. */
void battery_port_key_trace(int seconds) {
    if (!s_dev) return;
    int64_t end = esp_timer_get_time() + (int64_t)seconds * 1000000, down = 0, last_up = 0; int n = 0;
    uint8_t en = 0; rd(0x41, &en); wr(0x41, (uint8_t)(en | 0x0F));       /* the edge IRQs too, for the trace only */
    wr(0x49, 0xFF);
    ESP_LOGI("battery", "key trace: press the PWR key a few times, naturally, in the next %d s (the tank holds still; nothing sleeps)", seconds);
    while (esp_timer_get_time() < end) {
        uint8_t st; int64_t now = esp_timer_get_time();
        if (rd(0x49, &st) && (st & 0x0F)) {
            wr(0x49, (uint8_t)(st & 0x0F));
            if (st & 0x02) down = now;
            if (st & 0x09) {                                              /* the rising edge, or the short-press flag it comes with */
                n++;
                int held = down ? (int)((now - down) / 1000) : -1;
                ESP_LOGI("battery", "key trace: press %d held %d ms%s%s | gap since the last release %lld ms | status %02x", n, held,
                         held >= 0 && held < 128 ? "  <-- UNDER the 128 ms power-on hold: a powered-off PMIC ignores this one" : "",
                         (st & 0x04) ? " (long)" : "", last_up ? (now - last_up) / 1000 : 0LL, st);
                last_up = now; down = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    wr(0x41, en); wr(0x49, 0xFF);
    ESP_LOGI("battery", "key trace over: %d presses", n);
}

/* ---- diagnostics (2026-09-11, the battery-life pass) ----
 * AXP2101 register map (as XPowersLib reads it): 0x80 DCDC1-5 enables
 * (bits 0-4); 0x82-0x86 DCDC1-5 voltage codes; 0x90 LDO enables (bit0 ALDO1,
 * 1 ALDO2, 2 ALDO3, 3 ALDO4, 4 BLDO1, 5 BLDO2, 6 CPUSLDO, 7 DLDO1), 0x91 bit0
 * DLDO2; 0x92-0x9A LDO voltage codes; 0x34/0x35 VBAT ADC (13-bit, mV); 0x18
 * bit1 charger enable; 0x62 charge current code; 0x64 charge voltage code.
 * Every register is printed raw too, so a decode slip can't hide a rail. */
static int dcdc_mv(int n, uint8_t v) {
    switch (n) {
    case 1: return 1500 + (v & 0x1F) * 100;
    case 2: v &= 0x7F; return v <= 70 ? 500 + v * 10 : 1220 + (v - 71) * 20;
    case 3: v &= 0x7F; return v <= 70 ? 500 + v * 10 : v <= 87 ? 1220 + (v - 71) * 20 : 1600 + (v - 88) * 100;
    case 4: v &= 0x7F; return v <= 70 ? 500 + v * 10 : 1220 + (v - 71) * 20;
    default: return 1400 + (v & 0x1F) * 100;
    }
}
static int ldo_mv(uint8_t v)  { return 500 + (v & 0x1F) * 100; }   /* ALDO1-4, BLDO1-2, DLDO1 */
static int sldo_mv(uint8_t v) { return 500 + (v & 0x1F) * 50; }    /* CPUSLDO, DLDO2 */

/* rail switches: 0x80 DCDC1-5 (bits 0-4), 0x90 ALDO1-4 BLDO1-2 CPUSLDO DLDO1
 * (bits 0-7), 0x91 DLDO2 (bit 0) */
typedef struct { const char *name; uint8_t reg, bit; } rail_t;
static const rail_t RAILS[] = {
    { "dcdc2", 0x80, 1 }, { "dcdc3", 0x80, 2 }, { "dcdc4", 0x80, 3 }, { "dcdc5", 0x80, 4 },
    { "aldo1", 0x90, 0 }, { "aldo2", 0x90, 1 }, { "aldo3", 0x90, 2 }, { "aldo4", 0x90, 3 },
    { "bldo1", 0x90, 4 }, { "bldo2", 0x90, 5 }, { "cpusldo", 0x90, 6 }, { "dldo1", 0x90, 7 },
    { "dldo2", 0x91, 0 },
};
bool battery_port_set_rail(const char *name, bool on) {
    if (!s_dev) return false;
    for (size_t i = 0; i < sizeof RAILS / sizeof RAILS[0]; i++) {
        if (strcasecmp(name, RAILS[i].name)) continue;
        uint8_t v;
        if (!rd(RAILS[i].reg, &v)) return false;
        uint8_t nv = on ? (v | (1 << RAILS[i].bit)) : (v & ~(1 << RAILS[i].bit));
        if (nv == v) return true;
        uint8_t wr[2] = { RAILS[i].reg, nv };
        return i2c_master_transmit(s_dev, wr, 2, 100) == ESP_OK;
    }
    ESP_LOGW("battery", "no such rail '%s' (dcdc1 and the RTC LDO are never switched)", name);
    return false;
}
/* boot: everything with no consumer off, plus ALDO1 (audio analog, unused) */
void battery_port_trim_rails(void) {
    static const char *off[] = { "dcdc2", "dcdc3", "dcdc4", "aldo1", "aldo2", "aldo3", "aldo4", "bldo1", "bldo2", "cpusldo", "dldo1", "dldo2" };
    int n = 0;
    for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) n += battery_port_set_rail(off[i], false);
    ESP_LOGI("battery", "rails trimmed: %d of %d unused outputs off (dcdc1 = VCC3V3 and the RTC LDO stay)", n, (int)(sizeof off / sizeof off[0]));
}

int battery_port_vbat_mv(void) {
    uint8_t h, l;
    if (!s_dev) return adc_mv();
    if (!rd(0x34, &h) || !rd(0x35, &l)) return 0;
    return ((h & 0x1F) << 8) | l;                     /* 13-bit, 1 mV/LSB (XPowersLib readRegisterH5L8) */
}

void battery_port_dump(void) {
    if (!s_dev) {
        int mv = adc_mv();
        ESP_LOGI("battery", "no PMIC; GPIO%d divider: VBAT %d mV -> %d%% (charging state is not sensed on this board)",
                 PIN_BAT_ADC, mv, (int)(curve(mv) * 100 + 0.5f));
        return;
    }
    uint8_t st1 = 0, st2 = 0, cfg = 0, dc_en = 0, ldo0 = 0, ldo1 = 0, chg_en = 0, icc = 0, cv = 0, pct = 0;
    uint8_t dcv[5] = {0}, ldov[9] = {0};
    bool ok = rd(REG_STATUS1, &st1) && rd(REG_STATUS2, &st2) && rd(REG_COMMON_CFG, &cfg) &&
              rd(0x80, &dc_en) && rd(0x90, &ldo0) && rd(0x91, &ldo1) && rd(0x18, &chg_en) &&
              rd(0x62, &icc) && rd(0x64, &cv) && rd(REG_BAT_PERCENT, &pct);
    for (int i = 0; i < 5 && ok; i++) ok = rd(0x82 + i, &dcv[i]);
    for (int i = 0; i < 9 && ok; i++) ok = rd(0x92 + i, &ldov[i]);
    if (!ok) { ESP_LOGW("battery", "AXP2101 read failed"); return; }
    ESP_LOGI("battery", "AXP2101 raw: st1 %02x st2 %02x cfg %02x | dcdc_en %02x v %02x %02x %02x %02x %02x | ldo_en %02x %02x v %02x %02x %02x %02x %02x %02x %02x %02x %02x | chg %02x icc %02x cv %02x",
             st1, st2, cfg, dc_en, dcv[0], dcv[1], dcv[2], dcv[3], dcv[4], ldo0, ldo1,
             ldov[0], ldov[1], ldov[2], ldov[3], ldov[4], ldov[5], ldov[6], ldov[7], ldov[8], chg_en, icc, cv);
    for (int i = 0; i < 5; i++)
        ESP_LOGI("battery", "  DCDC%d  %-3s %4d mV", i + 1, (dc_en >> i) & 1 ? "ON" : "off", dcdc_mv(i + 1, dcv[i]));
    static const char *ldo_name[9] = { "ALDO1", "ALDO2", "ALDO3", "ALDO4", "BLDO1", "BLDO2", "CPUSLDO", "DLDO1", "DLDO2" };
    for (int i = 0; i < 9; i++) {
        bool on = i < 8 ? (ldo0 >> i) & 1 : ldo1 & 1;
        int mv = (i == 6 || i == 8) ? sldo_mv(ldov[i]) : ldo_mv(ldov[i]);
        ESP_LOGI("battery", "  %-7s %-3s %4d mV", ldo_name[i], on ? "ON" : "off", mv);
    }
    int icc_ma = (icc & 0x1F) <= 8 ? (icc & 0x1F) * 25 : 300 + ((icc & 0x1F) - 9) * 100;
    ESP_LOGI("battery", "  charger %s, %d mA, CV code %d | VBUS %s | battery %s, VBAT %d mV, SoC %d%%, %s",
             (chg_en >> 1) & 1 ? "enabled" : "disabled", icc_ma, cv & 0x07,
             (st1 >> 5) & 1 ? "present" : "absent", (st1 >> 3) & 1 ? "present" : "absent",
             battery_port_vbat_mv(), pct,
             ((st2 >> 5) & 0x07) == 0x01 ? "charging" : ((st2 >> 5) & 0x07) == 0x02 ? "discharging" : "standby");
}

bool battery_port_read(float *frac, bool *charging) {
    int64_t now = esp_timer_get_time();
    if (!s_dev) {                                   /* no PMIC: the divider, same 5 s cache */
        if (s_last_us < 0 || now - s_last_us > 5 * 1000000) {
            s_last_us = now;
            int mv = adc_mv();
            /* no cell on the header reads as the rail through the divider,
               which is nowhere near a plausible battery: hide the meter
               rather than draw a confident wrong number */
            s_valid = mv >= 2800 && mv <= 4400;
            if (s_valid) { s_frac = curve(mv); s_charging = false; }
        }
        if (!s_valid) return false;
        *frac = s_frac; *charging = s_charging;
        return true;
    }
    if (s_last_us < 0 || now - s_last_us > 5 * 1000000) {
        s_last_us = now;
        uint8_t st1, st2, pct;
        s_valid = rd(REG_STATUS1, &st1) && rd(REG_STATUS2, &st2) && rd(REG_BAT_PERCENT, &pct)
                  && (st1 & 0x08) && pct <= 100;          /* battery present, sane SoC */
        if (s_valid) { s_frac = pct / 100.0f; s_charging = ((st2 >> 5) & 0x07) == 0x01; }
    }
    if (!s_valid) return false;
    *frac = s_frac; *charging = s_charging;
    return true;
}
