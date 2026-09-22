/* display_port.h — the ONLY platform-specific seam for the renderer.
 * The tank renders into an RGB565 buffer (common/render.c); this port ships
 * it to the panel. QEMU/bring-up: stub. Track 4: SH8601 over QSPI (V1 board)
 * or CO5300 (V2), rotated 90 degrees so the tank is landscape 448x368. */
#ifndef DISPLAY_PORT_H
#define DISPLAY_PORT_H
#include <stdint.h>
#include <stdbool.h>

bool display_port_init(void);
/* push a full TANK_W x TANK_H RGB565 frame; may return before DMA completes */
void display_port_flush(const uint16_t *fb);
/* power the panel down for device sleep; display_port_wake (or a boot's
 * display_port_init) re-sequences it */
void display_port_sleep(void);
/* re-power and re-init the panel after display_port_sleep, without reboot */
void display_port_wake(void);
/* true = present the frame rotated 180 degrees (device held upside down) */
void display_port_set_inverted(bool inverted);
/* panel brightness 0..255 (DCS 0x51; the init sequence starts at 255). Kept
 * across display_port_wake, which re-inits the panel. */
void    display_port_set_brightness(uint8_t level);
uint8_t display_port_brightness(void);
/* A touch point as the controller reports it (panel coordinates) put into
 * TANK coordinates. Each board's geometry lives with its display port: the
 * AMOLED is a 90-degree rotation, the 1.54" LCD is the inverse of the
 * squash, bands and all. `inverted` = the screen is being shown 180 round.
 * The touch port applies its finger bias afterwards, in tank space. */
void display_port_map_touch(float px, float py, bool inverted, float *tx, float *ty);
#endif
