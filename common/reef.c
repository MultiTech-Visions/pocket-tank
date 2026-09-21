/* reef.c - the reef builder: the coral, the reef, and the page you build it
 * on (see reef.h). */
#include "reef.h"
#include "render.h"
#include <string.h>
#include <math.h>

/* Coral reads as saturated colour against dark water, so these are picked
 * bright and spread round the wheel: a reef built out of them should look
 * like one somebody BUILT. */
const uint32_t REEF_PALETTE[REEF_COLOURS + 1] = {
    0x000000,
    0xff5f8d, 0xff3fa8, 0xc23ff0, 0x7a5cff,
    0x3f7dff, 0x38b8ff, 0x2fe6d8, 0x2fd98a,
    0x7ae03a, 0xd8e63a, 0xffc22e, 0xff8a28,
    0xff5236, 0xf2f0e6, 0x9a7bd8,
};
const char *const REEF_COLOUR_NAMES[REEF_COLOURS + 1] = {
    "EMPTY", "ROSE", "MAGENTA", "ORCHID", "VIOLET", "COBALT", "SKY", "TURQUOISE",
    "JADE", "LIME", "CHARTREUSE", "AMBER", "TANGERINE", "CORAL", "BONE", "LILAC",
};

/* The coral, drawn once and baked in. A '.' is nothing, '-' the deep side,
 * '=' the body and '*' where the light catches; the keeper's colour is
 * applied to those three levels, so one drawing works in any of the fifteen.
 *
 * Drawn with tools/gen_coral.py and pasted here: the art is FIXED, not made
 * at run time, so what was looked at is what ships. None of them is a
 * rectangle - that was the whole complaint about the first cut. */
static const char *const SPR_STAGHORN[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "............*...................",
    "...........*=*..................",
    "............--.............**...",
    "............-=*...***.***..--..*",
    "............-=-...-*-.-=-.*=-.*-",
    "............-=-...-==*==-.-=-.--",
    ".............-=*.*=======*=-.*=-",
    ".............-==*======-====*==-",
    ".............-========-.-=====-.",
    ".............-=======-.*=====-.*",
    ".............-======-.**======*-",
    "..............-====-..-======*=-",
    "..............-===-..*=-=======-",
    "..............-==-..*==========-",
    "..............-==-.*===========-",
    "..............-==-.-====-======-",
    "..............-===*=*==-.---===-",
    "..............-========-....--=-",
    "..............-========-......--",
    ".............*====-===-........-",
    ".............-========-.........",
    ".............-======-=-.........",
    "............*=========-.........",
    "............-========-..........",
    "...........*==-======-..........",
    "...........-====-=-==-..........",
    "..........*======*==-...........",
    "..........-=========-...........",
    "..........-=========-...........",
    ".........*=========-............",
    ".........-=========-............",
    ".........-=========-............",
    ".........-==========*...........",
    ".........-==========-*..........",
    ".........-===========-..........",
    ".........-============*.........",
    ".........-============-.........",
    ".........-===*========-.........",
    ".........-============-.........",
    ".........--------------.........",
};
static const char *const SPR_BRANCH[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    ".***............................",
    "*==-............................",
    "-==-........................***.",
    "-===*.......................-==*",
    "-===-.......................-==-",
    "-==-........................-==-",
    "-==-........................-==-",
    "-==-........................-==-",
    "-==-...............***......-==-",
    "-===**.............-=-*....*=*=-",
    "-=====*.....***....-===*...-===-",
    "-======*...*===*..*====-...-===-",
    ".-=====-...-===-.*======*..-==-.",
    ".-*=====*..-===-.-======-.*===-.",
    ".-======-..-===-.-==*====*====-.",
    ".-=======*.-===-.-============-.",
    ".-=======-.-===-.-============-.",
    ".-=======-.-====*=-===========-.",
    ".-=======-..-=================-.",
    ".-=======-.*====-===-=========-.",
    ".-====-=*-.-===-.-======*====*-.",
    ".---------.-----.--------------.",
};
static const char *const SPR_BRAIN[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "........*******.................",
    ".......*=======**...******......",
    ".....**--=====--=***=--===**....",
    "....*=-..-===-..-===-..-===-....",
    "....-==**=====**===*=**===*=**..",
    "...*============**============*.",
    "...-==--=====---====--=====--=-.",
    "...-=-..-===-..-===-..-===-..-=*",
    "...-==**=====**=====**==*==**==-",
    "...-=--*====--=====--=====--===-",
    "...--..-===-..-===-..-=*=-..-==-",
    "...--**==*==**=====**-====**=*=-",
    "...---*====--=====--===-=--====-",
    "......-===-..-===-..-===-..-===-",
    "....**=====**=====**=====**====-",
    ".....-====--=====--=====--====-.",
    "......-==-..-===-..-*==-..-==-..",
    "......-===**=====**=====**===-..",
    "......-==--=====--=====--===-...",
    "......-=-..-===-..-===-..-=-....",
    "......-==**=====**=====**=-.....",
    ".......-=-==========-=====-.....",
    ".......-*===============*-......",
    "........------------------......",
};
static const char *const SPR_FAN[] = {
    "........................**......",
    "....**................**=-......",
    "....-=*..............*=--.......",
    "....--=**.......**.**--.........",
    "......-==***...*=-.-==-.....****",
    ".......-====*.*=-..-==-..****---",
    ".....*.-==-==*=-..*====**==--...",
    "....*=*==-.-===-..-*==-===-.....",
    ".....-====*=*===**=*======-.....",
    ".....-==========-========-..****",
    "......-========-.-======-..*===-",
    "***.**-=========*========**==--.",
    "-==*=-.-===========*======*=-...",
    "-====-.-========**=========-....",
    "-=====*.-======-==========--....",
    ".-=-===*=================-......",
    "..--====================-.......",
    "....-==*================-.......",
    ".....-===============---........",
    "......-==*===========-..........",
    ".......-=============-..........",
    "........-===========-...........",
    ".........--========-............",
    "...........-======-.............",
    "............-====-..............",
    "............-====-..............",
    ".............-===-..............",
    ".............-===-..............",
    ".............-====*.............",
    "..............-===-.............",
    "..............-===-.............",
    "..............-----.............",
};
static const char *const SPR_TUBES[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    ".....**.**......***.............",
    "....*==*==**...*===**...........",
    "....-=======*..-===*-...........",
    "....-=======-..-====-...........",
    ".....-======-...-===-.**........",
    ".....-======-...-====*==*.......",
    "....*=======-...-==*=====*......",
    "....-=======-...-*=======-.****.",
    "....-=======-..*==========*====*",
    "....-======-...-===============-",
    "....-======-...-========*-=====-",
    "...*===*===-...-========-.-====-",
    "...-=======-...-=====-=*-.-====-",
    "...--====*=-..*======-==-.-==*=-",
    "...-==*===-...-====-===-=*=====-",
    "...-======-...-===-.-===-======-",
    "...-======-...-====*===-.-====-.",
    "..*=======-...-========-.-====-.",
    "..-=======-...-=====*==-.-====-.",
    "..-=======-..*=========-.-====-.",
    "..-=======-..-=========-.-====-.",
    "..-=======-..-====-====-.-====-.",
    "..-=======-...-========-.-====-.",
    "..-======--...-=====-==-.-====-.",
    "..-===-=*=-...-====-===-.-===*-.",
    "...--------...-----.----.------.",
};
static const char *const SPR_TABLE[] = {
    "................................................",
    "................................................",
    "................................................",
    "................................................",
    "................................................",
    "................................................",
    "................................................",
    "................................................",
    "................................................",
    "................................................",
    ".....********....***.******.....................",
    "....*========**.*===*======**...................",
    "...*===========*===*======*==*.****.............",
    "..*=============**============*====*............",
    "..-============-===================**...........",
    "..--================================-...........",
    "..-=====================*===========-...........",
    "..-====*===========================-............",
    "...-=================-=*=========--.............",
    "....-====*==-------==-=======*==-...............",
    ".....-------.......-===-========-...............",
    "...................-======---=--................",
    "....................-====-...-..................",
    "....................-====-......................",
    "....................-=====*.....................",
    "....................-=====-.....................",
    ".....................-====-.....................",
    ".....................-====-.....................",
    ".....................-====-.....................",
    ".....................-=====*....................",
    ".....................-==*==-....................",
    ".....................-------....................",
};
static const char *const SPR_ANEMONE[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "..................**............",
    ".................*=-............",
    ".................-=-....**......",
    ".................-=-...*=-......",
    "....*.....****...-=-...-=-......",
    "...*=**...-===*..-=-..*==-......",
    "...-===***====-.*==-.*==-.......",
    "....--=========*====*===-.......",
    "......--===============-........",
    "........-==============-........",
    ".........-===========--.........",
    "..........-==========--.****....",
    "...........-===========*====**..",
    "***********==================-..",
    "--=====================------...",
    "---------=============-.........",
    ".........-==========*==*........",
    ".........-=============-........",
    ".........-=============-........",
    ".........-========-====-........",
    ".........-=============-........",
    "..........-=========-=-.........",
    "..........-===========-.........",
    "...........-----------..........",
};
static const char *const SPR_CABBAGE[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "..............*.................",
    "...........***=***..............",
    "..........*=*=====*.............",
    ".........*=========*............",
    "........*====-======****........",
    ".......*====-.-=========*.......",
    "......*====-.*=====*====-.......",
    "......-===-.*===**====--........",
    "......-==-.*===--====-..........",
    ".....*====*====-.-====***.......",
    "....*===========*=======**......",
    "...*===*=================-......",
    "....-================-=*-.......",
    "....--===*=======-===-===**.....",
    "......-=========-.-====-===*....",
    "....**=========-.*=========-....",
    "...*==========-.*=========-.....",
    "..*====-=====-.*====-====-......",
    "..-===-.-=====*====-.-====**....",
    "...---.*============*=======*...",
    "......*=====================-...",
    ".....*============-========-....",
    "...**====--==========---====*...",
    "..*=====-..-========-...-====*..",
    ".*====--....-======-.....-===**.",
    "..-----......------.......----..",
};
static const char *const SPR_PILLAR[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "...........................***..",
    "..........................*==-..",
    "..........................-==-..",
    "..........................-==-..",
    "..........................-==-..",
    ".......................***===-..",
    ".......................-=====-..",
    "......................*=*====-..",
    "...................*..-======-..",
    "..................*=**=*=====-..",
    ".................*===-=======-..",
    "..................-====-=====-..",
    "..................-==========-..",
    "..................-===========*.",
    "..................-===========-.",
    ".................*==*=====-==-..",
    ".................-=======-.---..",
    "................*========-......",
    "................-=-=====-.......",
    "................-=======-.......",
    "...............*====-==-........",
    "...............-=======-........",
    "...............-=====-=-........",
    "..............*=======-.........",
    "..............-=-=-===-.........",
    "..............-==*====-.........",
    ".............*=======-..........",
    ".............-=======-..........",
    ".............-=======-..........",
    ".............-=======-..........",
    ".............-=======-..........",
    ".............-=======-..........",
    ".............-======--..........",
    ".............-=======-..........",
    ".............-=======-..........",
    "............*========-..........",
    "............-*=======-..........",
    "............-=======-...........",
    "............---------...........",
};
static const char *const SPR_MOUND[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    ".....*****......................",
    "...**=====**...*****............",
    "..*=========*.*====***..........",
    ".*==========**=====*==***.......",
    "*-=======================**.....",
    "-==*======================-.....",
    "-==========================*....",
    "-==================*======*=*...",
    "-===============**===========*..",
    ".-=============-=============-..",
    "..----------------------------..",
};
static const char *const SPR_WHIP[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "**..............",
    "--*.............",
    "-==*............",
    "-===*...........",
    "-===-...........",
    ".-===*..........",
    "..-===*.........",
    "..--==-.........",
    "...-==-.........",
    "...-===*........",
    "....-==-........",
    "....-==-........",
    "....-=*=*.......",
    ".....-==-.......",
    ".....-==-.......",
    ".....-===*......",
    "......-==-......",
    "......-==-......",
    "......-===*.....",
    "......-===-.....",
    ".......-==-.....",
    ".......-=--.....",
    ".......-*=-.....",
    ".......-==-.....",
    ".......-==-.....",
    ".......-===*....",
    ".......-===-....",
    ".......-===-....",
    ".......-==-.....",
    ".......-==-.....",
    ".......--=-.....",
    ".......-==-.....",
    ".......-==-.....",
    ".......-==-.....",
    ".......-===*....",
    ".......-===-....",
    ".......-==-.....",
    ".......-==-.....",
    "......*==--.....",
    ".......----.....",
};
static const char *const SPR_BUBBLE[] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "..........................**....",
    "........................***=**..",
    "........****...........*======*.",
    "......**====**.........-======-.",
    "......-======-.........-=======*",
    ".....*========*........-*======-",
    ".....-=*======-........-======-.",
    ".....-========-....**..-======-.",
    ".....-===*====-.***==**.------..",
    "......-======-.*=======*........",
    "......--======*=========***.....",
    "........-==================**...",
    "........-====================*..",
    "........-===========*========-..",
    ".......*======================*.",
    ".....**========================*",
    "....*=============-============-",
    "...*===========================-",
    "...-=====-==========-==========-",
    "...-==-=*--------=======*====*=-",
    "....-----........---------------",
};
const reef_shape_t REEF_SHAPES[REEF_SHAPE_N] = {
    { "STAGHORN", 2, 3, SPR_STAGHORN },
    { "BRANCH", 2, 2, SPR_BRANCH },
    { "BRAIN", 2, 2, SPR_BRAIN },
    { "FAN", 2, 2, SPR_FAN },
    { "TUBES", 2, 2, SPR_TUBES },
    { "TABLE", 3, 2, SPR_TABLE },
    { "ANEMONE", 2, 2, SPR_ANEMONE },
    { "CABBAGE", 2, 2, SPR_CABBAGE },
    { "PILLAR", 2, 3, SPR_PILLAR },
    { "MOUND", 2, 1, SPR_MOUND },
    { "WHIP", 1, 3, SPR_WHIP },
    { "BUBBLE", 2, 2, SPR_BUBBLE },
};

uint8_t reef_shape_px(int shape, int px, int py) {
    if (shape < 0 || shape >= REEF_SHAPE_N) return 0;
    const reef_shape_t *s = &REEF_SHAPES[shape];
    if (px < 0 || py < 0 || px >= s->cw * REEF_CELL || py >= s->ch * REEF_CELL) return 0;
    switch (s->rows[py][px]) {
    case '-': return 1;
    case '=': return 2;
    case '*': return 3;
    default:  return 0;
    }
}
bool reef_shape_cell(int shape, int cx, int cy) {
    if (shape < 0 || shape >= REEF_SHAPE_N) return false;
    const reef_shape_t *s = &REEF_SHAPES[shape];
    if (cx < 0 || cy < 0 || cx >= s->cw || cy >= s->ch) return false;
    for (int y = 0; y < REEF_CELL; y++)
        for (int x = 0; x < REEF_CELL; x++)
            if (reef_shape_px(shape, cx * REEF_CELL + x, cy * REEF_CELL + y)) return true;
    return false;
}

/* ---- the reef: a list of pieces, packed into the save's bytes ----
 * byte 0 is the count; then four bytes each - shape, colour, cx, cy. A list
 * is what lets a piece be picked back UP whole, which a painted grid could
 * never do, and it is the natural store for a sprite. */
_Static_assert(sizeof(((tank_t *)0)->reef) == REEF_SAVE_BYTES,
               "tank_t.reef is not REEF_SAVE_BYTES: fix the literal in tank.h");
_Static_assert(1 + REEF_MAX * 4 <= REEF_SAVE_BYTES, "REEF_MAX does not fit the save");

static unsigned s_epoch = 1;
int reef_count(const tank_t *t) {
    int n = t->reef[0];
    return n > REEF_MAX ? REEF_MAX : n;
}
bool reef_piece(const tank_t *t, int i, reef_piece_t *out) {
    if (i < 0 || i >= reef_count(t)) return false;
    const uint8_t *p = &t->reef[1 + i * 4];
    out->shape = p[0]; out->colour = p[1]; out->cx = (int8_t)p[2]; out->cy = (int8_t)p[3];
    return out->shape < REEF_SHAPE_N;
}
bool reef_empty(const tank_t *t) { return reef_count(t) == 0; }
void reef_clear(tank_t *t) { memset(t->reef, 0, sizeof t->reef); s_epoch++; }
unsigned reef_epoch(const tank_t *t) { (void)t; return s_epoch; }

bool reef_occupied(const tank_t *t, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= REEF_COLS || cy >= REEF_ROWS) return false;
    reef_piece_t p;
    for (int i = 0; i < reef_count(t); i++) {
        if (!reef_piece(t, i, &p)) continue;
        if (reef_shape_cell(p.shape, cx - p.cx, cy - p.cy)) return true;
    }
    return false;
}
int reef_at(const tank_t *t, int cx, int cy) {
    reef_piece_t p;
    for (int i = reef_count(t) - 1; i >= 0; i--) {          /* the last placed is on top */
        if (!reef_piece(t, i, &p)) continue;
        if (reef_shape_cell(p.shape, cx - p.cx, cy - p.cy)) return i;
    }
    return -1;
}
/* The rule that makes this a reef and not a sticker album: every cell the
 * piece wants must be free, all of it must be on the glass, and at least one
 * of its cells must sit on the floor or touch something already built. */
bool reef_can_place(const tank_t *t, int shape, int cx, int cy) {
    if (shape < 0 || shape >= REEF_SHAPE_N) return false;
    if (reef_count(t) >= REEF_MAX) return false;
    const reef_shape_t *s = &REEF_SHAPES[shape];
    bool anchored = false, any = false;
    for (int y = 0; y < s->ch; y++)
        for (int x = 0; x < s->cw; x++) {
            if (!reef_shape_cell(shape, x, y)) continue;
            int gx = cx + x, gy = cy + y;
            if (gx < 0 || gy < 0 || gx >= REEF_COLS || gy >= REEF_ROWS) return false;
            if (reef_occupied(t, gx, gy)) return false;
            any = true;
            if (gy == REEF_ROWS - 1) anchored = true;        /* standing on the floor */
            else if (reef_occupied(t, gx, gy + 1) || reef_occupied(t, gx, gy - 1) ||
                     reef_occupied(t, gx - 1, gy) || reef_occupied(t, gx + 1, gy))
                anchored = true;                             /* growing off something */
        }
    return any && anchored;
}
int reef_settle(const tank_t *t, int shape, int cx, int cy) {
    if (shape < 0 || shape >= REEF_SHAPE_N) return -1;
    const reef_shape_t *sh = &REEF_SHAPES[shape];
    if (cy < 0) cy = 0;
    if (cy + sh->ch > REEF_ROWS) cy = REEF_ROWS - sh->ch;
    /* fall until the next row down would not be legal, then stop where we
       are - so it lands ON the floor or ON whatever is under it */
    int y = cy;
    while (y + sh->ch <= REEF_ROWS && !reef_can_place(t, shape, cx, y)) y++;   /* find the first legal row at all */
    if (y + sh->ch > REEF_ROWS) return -1;
    while (y + 1 + sh->ch <= REEF_ROWS && reef_can_place(t, shape, cx, y + 1)) y++;
    return y;
}
bool reef_place(tank_t *t, int shape, uint8_t colour, int cx, int cy) {
    if (!reef_can_place(t, shape, cx, cy)) return false;
    int n = reef_count(t);
    uint8_t *p = &t->reef[1 + n * 4];
    p[0] = (uint8_t)shape;
    p[1] = colour < 1 ? 1 : colour > REEF_COLOURS ? REEF_COLOURS : colour;
    p[2] = (uint8_t)(int8_t)cx; p[3] = (uint8_t)(int8_t)cy;
    t->reef[0] = (uint8_t)(n + 1);
    s_epoch++;
    return true;
}
bool reef_remove(tank_t *t, int index) {
    int n = reef_count(t);
    if (index < 0 || index >= n) return false;
    memmove(&t->reef[1 + index * 4], &t->reef[1 + (index + 1) * 4], (size_t)(n - index - 1) * 4);
    memset(&t->reef[1 + (n - 1) * 4], 0, 4);
    t->reef[0] = (uint8_t)(n - 1);
    s_epoch++;
    return true;
}

/* ---- drawing ---- */
/* The reef is the BACKDROP, and a backdrop that shouts is a backdrop you
 * lose the fish against. Everything drawn into the tank is muted: pulled
 * towards the deep water it sits in and knocked down in brightness, so it
 * reads as depth rather than as foreground. The catalogue and the ghost
 * draw at full strength, because there you are choosing a colour and need
 * to see it (2026-09-21). */
#define REEF_MUTE   0.56f     /* how much of the colour survives into the tank */
#define REEF_TOWARD 0x14343f  /* ... and the water it is pulled towards */
static uint32_t reef_shade(uint32_t rgb, int level, float dim) {
    int r = (int)((rgb >> 16) & 0xff), g = (int)((rgb >> 8) & 0xff), b = (int)(rgb & 0xff);
    if (level == 1) { r = r * 44 / 100; g = g * 44 / 100; b = b * 44 / 100; }        /* the deep side */
    else if (level == 3) { r += (255 - r) * 52 / 100; g += (255 - g) * 52 / 100; b += (255 - b) * 52 / 100; }
    if (dim < 1.0f) { r = (int)(r * dim); g = (int)(g * dim); b = (int)(b * dim); }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static uint32_t reef_mute(uint32_t rgb) {
    int r = (int)((rgb >> 16) & 0xff), g = (int)((rgb >> 8) & 0xff), b = (int)(rgb & 0xff);
    int tr = (REEF_TOWARD >> 16) & 0xff, tg = (REEF_TOWARD >> 8) & 0xff, tb = REEF_TOWARD & 0xff;
    r = tr + (int)((r - tr) * REEF_MUTE);
    g = tg + (int)((g - tg) * REEF_MUTE);
    b = tb + (int)((b - tb) * REEF_MUTE);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
void reef_draw_shape(uint16_t *fb, int stride, int x, int y, int shape,
                     uint8_t colour, int alpha, float dim) {
    reef_draw_shape_muted(fb, stride, x, y, shape, colour, alpha, dim, false);
}
void reef_draw_shape_muted(uint16_t *fb, int stride, int x, int y, int shape,
                           uint8_t colour, int alpha, float dim, bool mute) {
    if (shape < 0 || shape >= REEF_SHAPE_N) return;
    const reef_shape_t *s = &REEF_SHAPES[shape];
    uint32_t base = REEF_PALETTE[colour < 1 ? 1 : colour > REEF_COLOURS ? REEF_COLOURS : colour];
    uint32_t lut[4] = { 0, reef_shade(base, 1, dim), reef_shade(base, 2, dim), reef_shade(base, 3, dim) };
    if (mute) for (int i = 1; i < 4; i++) lut[i] = reef_mute(lut[i]);
    for (int py = 0; py < s->ch * REEF_CELL; py++) {
        int run = 0, level = 0;
        for (int px = 0; px <= s->cw * REEF_CELL; px++) {   /* runs, so a row is a few rects not 32 */
            int v = px < s->cw * REEF_CELL ? reef_shape_px(shape, px, py) : 0;
            if (v == level) { run++; continue; }
            if (level && run) {
                if (alpha >= 255) render_rect(fb, stride, x + px - run, y + py, run, 1, lut[level]);
                else render_rect_blend(fb, stride, x + px - run, y + py, run, 1, lut[level], alpha);
            }
            level = v; run = 1;
        }
    }
}
/* ---- the carrier ------------------------------------------------------
 * Living coral grows on the dead skeleton of what grew before it, so a reef
 * is a MASS with colour on its surface - not a row of stickers. Without
 * this the pieces read as stamps on the water, which is exactly what they
 * were (2026-09-21).
 *
 * Every occupied cell gets rock. How much depends on where the cell sits:
 * one with coral on all four sides is solid, one on the outside is eaten
 * away towards the open water, and the erosion is dithered against a hash
 * of the pixel so the silhouette comes out ragged instead of square. That
 * is the whole trick - the boundary must never be a straight cell edge. */
#define CARRIER_ROCK   0x3a464c
#define CARRIER_DEEP   0x1b2428
#define CARRIER_LIGHT  0x5d6c71
static inline uint32_t carrier_hash(int x, int y) {
    uint32_t h = (uint32_t)x * 2654435761u ^ (uint32_t)y * 40503u;
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    return h;
}
static void reef_draw_carrier(const tank_t *t, uint16_t *fb, int stride, float dim) {
    uint32_t rock  = reef_mute(reef_shade(CARRIER_ROCK, 2, dim));
    uint32_t deep  = reef_mute(reef_shade(CARRIER_DEEP, 2, dim));
    uint32_t light = reef_mute(reef_shade(CARRIER_LIGHT, 2, dim));
    for (int cy = 0; cy < REEF_ROWS; cy++)
        for (int cx = 0; cx < REEF_COLS; cx++) {
            if (!reef_occupied(t, cx, cy)) continue;
            bool up = reef_occupied(t, cx, cy - 1), dn = cy == REEF_ROWS - 1 || reef_occupied(t, cx, cy + 1);
            bool lf = reef_occupied(t, cx - 1, cy), rt = reef_occupied(t, cx + 1, cy);
            for (int y = 0; y < REEF_CELL; y++)
                for (int x = 0; x < REEF_CELL; x++) {
                    /* how far into the mass this pixel is, 0 at an open edge
                       and 1 well inside; an open side pulls it down */
                    /* The falloff is STEEP, so the inside of a cell is solid
                       rock and only a band a few pixels wide near an open
                       side gets eaten. The first cut used a gentle slope and
                       dithered most of every cell, which came out as static
                       rather than stone. */
                    float f = 1.0f;
                    float ex = (x + 0.5f) / REEF_CELL, ey = (y + 0.5f) / REEF_CELL;
                    if (!lf) { float d = ex * 4.5f;         if (d < f) f = d; }
                    if (!rt) { float d = (1 - ex) * 4.5f;   if (d < f) f = d; }
                    if (!up) { float d = ey * 3.0f;         if (d < f) f = d; }
                    if (!dn) { float d = (1 - ey) * 4.5f;   if (d < f) f = d; }
                    if (f <= 0) continue;
                    int px = cx * REEF_CELL + x, py = cy * REEF_CELL + y;
                    uint32_t h = carrier_hash(px, py);
                    if (f < 1.0f && (h & 255) > (uint32_t)(f * f * 255.0f)) continue;   /* the ragged edge */
                    uint32_t col = rock;
                    if ((h >> 9 & 31) == 0) col = light;                 /* grain, sparse enough to be stone */
                    else if ((h >> 15 & 31) == 0) col = deep;
                    if (!up && ey < 0.30f) col = light;                  /* the top catches the light */
                    else if (!dn && ey > 0.72f) col = deep;              /* and the underside is in shadow */
                    render_rect(fb, stride, px, py, 1, 1, col);
                }
        }
}
void reef_draw(const tank_t *t, uint16_t *fb, int stride, float dim) {
    if (reef_empty(t)) return;
    reef_draw_carrier(t, fb, stride, dim);           /* the dead mass it all grows on */
    reef_piece_t p;
    for (int i = 0; i < reef_count(t); i++) {
        if (!reef_piece(t, i, &p)) continue;
        reef_draw_shape_muted(fb, stride, p.cx * REEF_CELL, p.cy * REEF_CELL, p.shape, p.colour, 255, dim, true);
    }
}

/* ---- the builder -------------------------------------------------------
 * Two states and the swipe between them. The canvas shows the reef with a
 * ghost of the piece in hand; the ghost says whether it MAY go there, so
 * the adjacency rule is something you can see rather than something that
 * silently refuses you. */
#define RUI_BAR_COLS   2
#define RUI_DOT_R      11
#define RUI_DOT_DX     28
#define RUI_DOT_DY     28
#define RUI_BAR_W      (RUI_BAR_COLS * RUI_DOT_DX + 6)
#define RUI_BAR_ROWS   ((REEF_COLOURS + RUI_BAR_COLS - 1) / RUI_BAR_COLS)
#define RUI_CAT_X      (RUI_BAR_W + 10)
#define RUI_CAT_COLS   3
#define RUI_TILE       110
#define RUI_TILE_PAD   6
#define RUI_MENU_Y     44
#define RUI_FOOT_Y     (TANK_H - 24)
#define RUI_HINT_Y     (TANK_H - 22)
#define RUI_BTN_H      30

static bool  s_menu;
static int   s_shape = -1, s_colour = 1;
static int   s_cx, s_cy;
static float s_scroll, s_scroll0;
static bool  s_rub;
static bool  s_settled;      /* did the piece in hand find anywhere to rest in this column? */

void reef_ui_open(tank_t *t) {
    (void)t; s_menu = false; s_shape = -1; s_rub = false;
    s_cx = REEF_COLS / 2; s_cy = REEF_ROWS - 3; s_scroll = 0;
}
void reef_ui_close(void) { s_menu = false; s_shape = -1; }
bool reef_ui_menu_up(void) { return s_menu; }
void reef_ui_hand(int *shape, int *colour, bool *rubbing) {
    if (shape) *shape = s_shape;
    if (colour) *colour = s_colour;
    if (rubbing) *rubbing = s_rub;
}
/* the finger holds the piece by its middle, and it never leaves the glass */
static void rui_to_cell(const tank_t *t, float x, float y) {
    if (s_shape < 0) return;
    const reef_shape_t *s = &REEF_SHAPES[s_shape];
    s_cx = (int)(x / REEF_CELL) - s->cw / 2;
    s_cy = (int)(y / REEF_CELL) - s->ch / 2;
    if (s_cx < 0) s_cx = 0;
    if (s_cx + s->cw > REEF_COLS) s_cx = REEF_COLS - s->cw;
    if (s_cy < 0) s_cy = 0;
    if (s_cy + s->ch > REEF_ROWS) s_cy = REEF_ROWS - s->ch;
    int settled = reef_settle(t, s_shape, s_cx, s_cy);   /* it falls to where it would rest */
    s_settled = settled >= 0;
    if (s_settled) s_cy = settled;
}

void reef_ui_draw(const tank_t *t, uint16_t *fb, int stride, float clock) {
    const uint32_t INK = 0x031015, PANEL = 0x04141a, TEAL = 0x9fd8e2, FAINT = 0x3f6a72,
                   WHITE = 0xffffff, NO = 0xf25b65;
    if (!s_menu) {
        if (s_shape >= 0) {
            bool ok = s_settled;
            int pulse = 150 + (int)(70 * (0.5f + 0.5f * sinf(clock * 4.0f)));
            reef_draw_shape(fb, stride, s_cx * REEF_CELL, s_cy * REEF_CELL, s_shape,
                            (uint8_t)s_colour, ok ? pulse : 110, 1.0f);
            /* the footprint, ringed in teal where it may go and red where it
               may not: the rule is on the glass, not hidden in a refusal */
            const reef_shape_t *sh = &REEF_SHAPES[s_shape];
            for (int cy = 0; cy < sh->ch; cy++)
                for (int cx = 0; cx < sh->cw; cx++)
                    if (reef_shape_cell(s_shape, cx, cy))
                        render_rect_edge(fb, stride, (s_cx + cx) * REEF_CELL, (s_cy + cy) * REEF_CELL,
                                         REEF_CELL, REEF_CELL, ok ? TEAL : NO);
        }
        render_rect_blend(fb, stride, 0, RUI_HINT_Y - 6, TANK_W, 28, INK, 200);
        const char *hint = s_rub ? "TAP A CORAL TO TAKE IT OUT"
                         : s_shape < 0 ? "SWIPE UP FOR CORAL"
                         : s_settled ? "TAP TO PLANT IT"
                         : "NO ROOM IN THIS COLUMN";
        uint32_t hc = (s_shape >= 0 && !s_settled) ? NO : WHITE;
        render_text(fb, stride, (TANK_W - render_text_w(hint, 2)) / 2, RUI_HINT_Y, 2, hc, hint);
        if (s_shape >= 0)
            render_button(fb, stride, TANK_W - 82, RUI_HINT_Y - 44, 74, RUI_BTN_H, PANEL, TEAL, "DROP", 2);
        return;
    }
    /* ---- the catalogue ---- */
    render_rect_blend(fb, stride, 0, 0, TANK_W, TANK_H, INK, 236);
    render_text(fb, stride, RUI_CAT_X, 14, 3, WHITE, "CORAL");
    render_button(fb, stride, TANK_W - 96, 8, 88, 28, PANEL, TEAL, "DONE", 2);
    for (int i = 1; i <= REEF_COLOURS; i++) {
        int slot = i - 1;
        int cy = RUI_MENU_Y + (slot / RUI_BAR_COLS) * RUI_DOT_DY + RUI_DOT_R;
        int cx = 4 + (slot % RUI_BAR_COLS) * RUI_DOT_DX + RUI_DOT_R;
        for (int dy = -RUI_DOT_R; dy <= RUI_DOT_R; dy++) {
            int half = (int)sqrtf((float)(RUI_DOT_R * RUI_DOT_R - dy * dy));
            render_rect(fb, stride, cx - half, cy + dy, half * 2 + 1, 1, REEF_PALETTE[i]);
        }
        if (!s_rub && i == s_colour)
            render_rect_edge(fb, stride, cx - RUI_DOT_R - 3, cy - RUI_DOT_R - 3,
                             RUI_DOT_R * 2 + 7, RUI_DOT_R * 2 + 7, WHITE);
    }
    { int ey = RUI_MENU_Y + RUI_BAR_ROWS * RUI_DOT_DY + 6;
      render_button(fb, stride, 4, ey, RUI_BAR_W - 8, 28, s_rub ? 0x3a1418 : PANEL, NO, "OUT", 2); }
    for (int i = 0; i < REEF_SHAPE_N; i++) {
        int col = i % RUI_CAT_COLS, row = i / RUI_CAT_COLS;
        int x = RUI_CAT_X + col * (RUI_TILE + RUI_TILE_PAD);
        int y = RUI_MENU_Y + row * (RUI_TILE + RUI_TILE_PAD) - (int)s_scroll;
        if (y + RUI_TILE < RUI_MENU_Y - 4 || y > RUI_FOOT_Y) continue;
        render_rect(fb, stride, x, y, RUI_TILE, RUI_TILE, PANEL);
        render_rect_edge(fb, stride, x, y, RUI_TILE, RUI_TILE, i == s_shape ? WHITE : FAINT);
        const reef_shape_t *sh = &REEF_SHAPES[i];
        int aw = sh->cw * REEF_CELL, ah = sh->ch * REEF_CELL;
        reef_draw_shape(fb, stride, x + (RUI_TILE - aw) / 2, y + (RUI_TILE - 16 - ah) / 2 + 2,
                        i, (uint8_t)s_colour, 255, 1.0f);
        render_text(fb, stride, x + (RUI_TILE - render_text_w(sh->name, 1)) / 2, y + RUI_TILE - 12,
                    1, FAINT, sh->name);
    }
    render_rect(fb, stride, 0, RUI_FOOT_Y, TANK_W, TANK_H - RUI_FOOT_Y, INK);
    render_text(fb, stride, RUI_CAT_X, RUI_FOOT_Y + 5, 2, FAINT, "DRAG TO SCROLL  -  TAP A CORAL");
}

void reef_ui_press(tank_t *t, float x, float y) {
    if (s_menu) { s_scroll0 = s_scroll; return; }
    rui_to_cell(t, x, y);
}
void reef_ui_drag(tank_t *t, float x, float y) {
    if (!s_menu) rui_to_cell(t, x, y);
}
static void rui_scroll_to(float v) {
    int rows = (REEF_SHAPE_N + RUI_CAT_COLS - 1) / RUI_CAT_COLS;
    float span = (float)(rows * (RUI_TILE + RUI_TILE_PAD)) - (float)(RUI_FOOT_Y - RUI_MENU_Y);
    if (span < 0) span = 0;
    s_scroll = v < 0 ? 0 : v > span ? span : v;
}
int reef_ui_tap(tank_t *t, float x, float y, float dx, float dy) {
    if (!s_menu) {
        if (dy < -40 && fabsf(dx) < fabsf(dy)) { s_menu = true; return REEF_UI_KEPT; }
        if (s_shape >= 0 && y >= RUI_HINT_Y - 44 && y < RUI_HINT_Y - 44 + RUI_BTN_H && x >= TANK_W - 96) {
            s_shape = -1; return REEF_UI_KEPT;                      /* DROP */
        }
        if (y >= RUI_HINT_Y - 6) return REEF_UI_NONE;
        if (s_rub) {                                                /* take one back out */
            int i = reef_at(t, (int)(x / REEF_CELL), (int)(y / REEF_CELL));
            if (i >= 0) { reef_remove(t, i); return REEF_UI_KEPT; }
            return REEF_UI_NONE;
        }
        if (s_shape >= 0) {
            rui_to_cell(t, x, y);
            if (s_settled && reef_place(t, s_shape, (uint8_t)s_colour, s_cx, s_cy)) return REEF_UI_KEPT;
            return REEF_UI_NONE;                                    /* refused: the ghost said so */
        }
        return REEF_UI_NONE;
    }
    if (dy > 40 && fabsf(dx) < fabsf(dy)) { s_menu = false; return REEF_UI_KEPT; }
    if (fabsf(dy) > 24) { rui_scroll_to(s_scroll0 - dy); return REEF_UI_KEPT; }
    if (y < 40 && x >= TANK_W - 104) { s_menu = false; return REEF_UI_CLOSE; }
    if (x < RUI_BAR_W) {
        int ey = RUI_MENU_Y + RUI_BAR_ROWS * RUI_DOT_DY + 6;
        if (y >= ey && y < ey + 32) { s_rub = !s_rub; if (s_rub) s_shape = -1; s_menu = false; return REEF_UI_KEPT; }
        int row = (int)((y - RUI_MENU_Y) / RUI_DOT_DY);
        int col = (int)((x - 4) / RUI_DOT_DX);
        if (col < 0) col = 0;
        if (col >= RUI_BAR_COLS) col = RUI_BAR_COLS - 1;
        int i = row * RUI_BAR_COLS + col + 1;
        if (row >= 0 && i >= 1 && i <= REEF_COLOURS) { s_colour = i; s_rub = false; }
        return REEF_UI_KEPT;
    }
    for (int i = 0; i < REEF_SHAPE_N; i++) {
        int col = i % RUI_CAT_COLS, row = i / RUI_CAT_COLS;
        int tx = RUI_CAT_X + col * (RUI_TILE + RUI_TILE_PAD);
        int ty = RUI_MENU_Y + row * (RUI_TILE + RUI_TILE_PAD) - (int)s_scroll;
        if (y > RUI_FOOT_Y) break;
        if (x >= tx && x < tx + RUI_TILE && y >= ty && y < ty + RUI_TILE) {
            s_shape = i; s_rub = false; s_menu = false;
            s_cy = REEF_ROWS - REEF_SHAPES[i].ch;                   /* start it on the floor */
            s_settled = reef_settle(t, i, s_cx, s_cy) >= 0;
            return REEF_UI_KEPT;
        }
    }
    return REEF_UI_KEPT;
}

/* ---- finding it -------------------------------------------------------- */
static const uint8_t COMBO[] = { REEF_G_SWIPE_L, REEF_G_SWIPE_R, REEF_G_SWIPE_U, REEF_G_SWIPE_D,
                                 REEF_G_TAP_R, REEF_G_TAP_L, REEF_G_TAP_C };
#define COMBO_N ((int)(sizeof COMBO / sizeof COMBO[0]))
static int   s_combo;             /* how many in a row have matched */
static float s_combo_at;          /* when the last one landed, on the tank clock */

int reef_gesture_of(float px, float py, float dx, float dy) {
    if (dx * dx + dy * dy >= 30 * 30) {                 /* a swipe: whichever way it went furthest */
        if (fabsf(dx) > fabsf(dy)) return dx < 0 ? REEF_G_SWIPE_L : REEF_G_SWIPE_R;
        return dy < 0 ? REEF_G_SWIPE_U : REEF_G_SWIPE_D;
    }
    (void)py;
    if (px < TANK_W / 3.0f) return REEF_G_TAP_L;
    if (px >= TANK_W * 2.0f / 3.0f) return REEF_G_TAP_R;
    return REEF_G_TAP_C;
}
bool reef_combo(int gesture, float clock) {
    if (s_combo > 0 && clock - s_combo_at > REEF_COMBO_GAP_S) s_combo = 0;   /* too slow: forgotten */
    if (gesture == COMBO[s_combo]) {
        s_combo_at = clock;
        if (++s_combo >= COMBO_N) { s_combo = 0; return true; }
        return false;
    }
    /* a wrong move starts over - but if it happens to BE the first move,
       start over from one rather than from nothing */
    s_combo = (gesture == COMBO[0]) ? 1 : 0;
    s_combo_at = clock;
    return false;
}
bool reef_combo_busy(void) { return s_combo > 0; }
int  reef_combo_progress(void) { return s_combo; }
void reef_combo_reset(void) { s_combo = 0; }

/* ---- the tour ---- */
#define TOUR_FLASH_S 0.42f
#define TOUR_FLASHES 3
static int   s_tour;
static float s_tour_t;
void reef_tour_begin(void) { s_tour = REEF_TOUR_OVERVIEW; s_tour_t = 0; }
void reef_tour_tick(float dt) { if (s_tour) s_tour_t += dt; }
int  reef_tour_stage(void) { return s_tour; }
bool reef_tour_lit(void) { return s_tour && fmodf(s_tour_t, TOUR_FLASH_S * 2) < TOUR_FLASH_S; }
bool reef_tour_stage_done(void) { return s_tour && s_tour_t >= TOUR_FLASH_S * 2 * TOUR_FLASHES; }
void reef_tour_next(void) { if (s_tour == REEF_TOUR_OVERVIEW) { s_tour = REEF_TOUR_SHOP; s_tour_t = 0; } else s_tour = REEF_TOUR_OFF; }
void reef_tour_end(void) { s_tour = REEF_TOUR_OFF; s_tour_t = 0; }

/* ---- the little coral in the shop's corner ---- */
void reef_icon_draw(uint16_t *fb, int stride, bool highlight) {
    render_rect(fb, stride, REEF_ICON_X, REEF_ICON_Y, REEF_ICON_W, REEF_ICON_H, 0x04141a);
    render_rect_edge(fb, stride, REEF_ICON_X, REEF_ICON_Y, REEF_ICON_W, REEF_ICON_H,
                     highlight ? 0xffffff : 0x3f6a72);
    if (highlight)
        render_rect_edge(fb, stride, REEF_ICON_X - 3, REEF_ICON_Y - 3, REEF_ICON_W + 6, REEF_ICON_H + 6, 0xffffff);
    /* the catalogue's own art, not a second drawing that then drifts from
       it. A 2x2 coral, because a 2x3 one hangs out of the box. */
    reef_draw_shape_muted(fb, stride, REEF_ICON_X + (REEF_ICON_W - 32) / 2,
                          REEF_ICON_Y + (REEF_ICON_H - 32) / 2, REEF_ICON_SHAPE, 7, 255, 1.0f, false);
}
bool reef_icon_hit(float x, float y) {
    return x >= REEF_ICON_X - 8 && x < REEF_ICON_X + REEF_ICON_W + 8 &&
           y >= REEF_ICON_Y - 8 && y < REEF_ICON_Y + REEF_ICON_H + 8;
}
