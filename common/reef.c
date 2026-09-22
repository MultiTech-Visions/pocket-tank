/* reef.c - the reef builder: the coral, the reef, and the page you build it
 * on (see reef.h). */
#include "reef.h"
#include "render.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

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
/* How much reef there is right behind a point, 1 at it and 0 well clear of
 * it: what render.c's shadow under a fish fades on, so a fish out in open
 * water carries no smudge and only one down among the coral does. The piece
 * list is walked (at most REEF_MAX) rather than the cells, because
 * reef_occupied is itself a walk and this runs per fish per frame. */
float reef_near(const tank_t *t, float x, float y) {
    if (t->reef_hide) return 0.0f;
    float best = 1e9f;
    reef_piece_t p;
    for (int i = 0; i < reef_count(t); i++) {
        if (!reef_piece(t, i, &p)) continue;
        const reef_shape_t *sh = &REEF_SHAPES[p.shape];
        float x0 = (float)(p.cx * REEF_CELL), x1 = x0 + sh->cw * REEF_CELL;
        float y0 = (float)(p.cy * REEF_CELL), y1 = y0 + sh->ch * REEF_CELL;
        float dx = x < x0 ? x0 - x : x > x1 ? x - x1 : 0;
        float dy = y < y0 ? y0 - y : y > y1 ? y - y1 : 0;
        float d = dx > dy ? dx : dy;                  /* box distance: no sqrt per piece */
        if (d < best) best = d;
        if (best <= REEF_NEAR_IN) return 1.0f;
    }
    if (best >= REEF_NEAR_OUT) return 0.0f;
    return 1.0f - (best - REEF_NEAR_IN) / (REEF_NEAR_OUT - REEF_NEAR_IN);
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
    /* hidden by the keeper (settings): still built, still saved, just not
       drawn - unless the builder is open, where it is the thing being
       worked on. */
    if (t->reef_hide && !reef_ui_active()) return;
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
/* ---- the layout -------------------------------------------------------
 * Reworked 2026-09-22 after a session on the real glass:
 *  - the colour dots were 22 px across and all but unhittable. The rail now
 *    carries ONE big swatch that opens a panel of big ones.
 *  - the hint sat along the foot, which is exactly where the first row of
 *    coral goes, so it covered the thing being aimed at. It is at the top.
 *  - a piece is planted when the finger LIFTS. Dragging across the clear
 *    upper half and letting go used to throw the piece away.
 *  - a slow drag in the catalogue was read as a tap on whatever was under
 *    the finger. Anything past RUI_SLOP px is a scroll and never a tap.
 *  - DONE was small; there was no way to start over. */
#define RUI_SLOP       8             /* past this, a finger is scrolling, not tapping */
#define RUI_RAIL_W     88            /* the left rail: colour, OUT, RESET */
#define RUI_SW_Y       56            /* the big colour swatch */
#define RUI_SW_H       58
#define RUI_OUT_Y      (RUI_SW_Y + RUI_SW_H + 10)
#define RUI_RESET_Y    (RUI_OUT_Y + 44)
#define RUI_BTN_H      38
#define RUI_DONE_W     118
#define RUI_DONE_H     40
#define RUI_DONE_X     (TANK_W - RUI_DONE_W - 8)
#define RUI_DONE_Y     6
#define RUI_CAT_X      (RUI_RAIL_W + 10)
#define RUI_CAT_COLS   3
#define RUI_TILE       110
#define RUI_TILE_PAD   6
#define RUI_MENU_Y     52
#define RUI_FOOT_Y     (TANK_H - 24)
#define RUI_HINT_Y     8             /* the hint is at the TOP now, off the building floor */
#define RUI_HINT_H     26
/* the colour panel: big swatches, five across */
#define RUI_PAN_COLS   5
#define RUI_PAN_X      24
#define RUI_PAN_Y      64
#define RUI_PAN_SW     76
#define RUI_PAN_SH     62
#define RUI_PAN_PAD    8
/* the magnifier, for picking one coral out of a crowd */
#define RUI_ZOOM_MAX   3.0f
#define RUI_BACK_W     92
#define RUI_BACK_H     34
#define RUI_ZBAR_Y     (RUI_HINT_Y + RUI_HINT_H + 6)

static bool  s_menu;
static int   s_shape = -1, s_colour = 1;
static int   s_cx, s_cy;
static float s_scroll, s_scroll0;
static bool  s_rub;
static bool  s_settled;      /* did the piece in hand find anywhere to rest in this column? */
static bool  s_panel;        /* the colour panel is up over the catalogue */
static bool  s_confirm;      /* RESET is asking */
static float s_zoom = 1.0f;  /* the magnifier, in OUT mode only */
static float s_zx, s_zy;     /* what it is centred on, in tank space */
static float s_px, s_py;     /* where the finger went down (screen space) */
static float s_zx0, s_zy0;   /* ... and where the view was then */
static bool  s_dragged;      /* has it moved past the slop since? */

static bool s_open;          /* the builder has the glass */
bool reef_ui_active(void) { return s_open; }
void reef_ui_open(tank_t *t) {
    (void)t; s_menu = false; s_shape = -1; s_rub = false; s_open = true;
    s_cx = REEF_COLS / 2; s_cy = REEF_ROWS - 3; s_scroll = 0;
    s_panel = s_confirm = false; s_zoom = 1.0f;
}
void reef_ui_close(void) { s_menu = false; s_shape = -1; s_open = false; s_zoom = 1.0f; }
bool reef_ui_menu_up(void) { return s_menu; }
bool reef_ui_zoom(float *x, float *y, float *z) {
    if (s_zoom <= 1.01f || s_menu) return false;
    if (x) *x = s_zx;
    if (y) *y = s_zy;
    if (z) *z = s_zoom;
    return true;
}
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
/* a touch in SCREEN space, put back into tank space if the magnifier is up */
static void rui_unzoom(float *x, float *y) {
    if (s_zoom <= 1.01f) return;
    float tx, ty; render_camera_unmap(*x, *y, &tx, &ty);
    *x = tx; *y = ty;
}
static void rui_pan(float dx, float dy) {
    float rw = TANK_W / s_zoom * 0.5f, rh = TANK_H / s_zoom * 0.5f;
    s_zx = s_zx0 - dx / s_zoom; s_zy = s_zy0 - dy / s_zoom;
    if (s_zx < rw) s_zx = rw;
    if (s_zx > TANK_W - rw) s_zx = TANK_W - rw;
    if (s_zy < rh) s_zy = rh;
    if (s_zy > TANK_H - rh) s_zy = TANK_H - rh;
}

static void rui_swatch(uint16_t *fb, int stride, int x, int y, int w, int h, int colour, bool lit) {
    render_rect(fb, stride, x, y, w, h, REEF_PALETTE[colour]);
    render_rect_edge(fb, stride, x, y, w, h, lit ? 0xffffff : 0x03151a);
    if (lit) render_rect_edge(fb, stride, x - 3, y - 3, w + 6, h + 6, 0xffffff);
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
        /* the hint rides along the TOP: the bottom of the glass is where the
           first row of coral goes, and the strip used to cover it */
        render_rect_blend(fb, stride, 0, RUI_HINT_Y - 6, TANK_W, RUI_HINT_H + 4, INK, 200);
        const char *hint = s_rub ? (s_zoom > 1.01f ? "TAP A CORAL TO TAKE IT OUT"
                                                   : "TAP WHERE YOU WANT A CLOSER LOOK")
                         : s_shape < 0 ? "SWIPE UP FOR CORAL"
                         : s_settled ? "LET GO TO PLANT IT"
                         : "NO ROOM IN THIS COLUMN";
        uint32_t hc = (s_shape >= 0 && !s_settled) ? NO : WHITE;
        render_text(fb, stride, (TANK_W - render_text_w(hint, 2)) / 2, RUI_HINT_Y, 2, hc, hint);
        if (s_shape >= 0)
            render_button(fb, stride, TANK_W - 90, TANK_H - RUI_BTN_H - 8, 82, RUI_BTN_H, PANEL, TEAL, "DROP", 2);
        if (s_rub && s_zoom > 1.01f) {                      /* the magnifier's own bar, under the hint:
                                                               down at the foot it covered the coral */
            render_button(fb, stride, 8, RUI_ZBAR_Y, RUI_BACK_W, RUI_BACK_H, PANEL, TEAL, "BACK", 2);
            char z[8]; snprintf(z, sizeof z, "%dX", (int)(s_zoom + 0.5f));
            render_button(fb, stride, 8 + RUI_BACK_W + 8, RUI_ZBAR_Y, 56, RUI_BACK_H, PANEL, TEAL, z, 2);
        }
        return;
    }
    /* ---- the catalogue ---- */
    render_rect_blend(fb, stride, 0, 0, TANK_W, TANK_H, INK, 236);
    render_text(fb, stride, 12, 16, 3, WHITE, "CORAL");
    render_button(fb, stride, RUI_DONE_X, RUI_DONE_Y, RUI_DONE_W, RUI_DONE_H, PANEL, TEAL, "DONE", 3);
    /* the rail: the colour in hand (tap for the panel), OUT, RESET */
    render_text(fb, stride, 8, RUI_SW_Y - 14, 1, FAINT, "COLOUR");
    rui_swatch(fb, stride, 8, RUI_SW_Y, RUI_RAIL_W - 16, RUI_SW_H, s_colour, false);
    render_button(fb, stride, 8, RUI_OUT_Y, RUI_RAIL_W - 16, RUI_BTN_H, s_rub ? 0x3a1418 : PANEL, NO, "OUT", 2);
    render_button(fb, stride, 8, RUI_RESET_Y, RUI_RAIL_W - 16, RUI_BTN_H, PANEL, FAINT, "RESET", 2);
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
    render_text(fb, stride, 8, RUI_FOOT_Y + 5, 2, FAINT, "DRAG TO SCROLL  -  TAP TO PICK");
    if (s_panel) {                                          /* the colours, big enough to hit */
        render_rect_blend(fb, stride, 0, 0, TANK_W, TANK_H, INK, 246);
        render_text(fb, stride, RUI_PAN_X, 24, 3, WHITE, "COLOUR");
        render_button(fb, stride, RUI_DONE_X, RUI_DONE_Y, RUI_DONE_W, RUI_DONE_H, PANEL, TEAL, "DONE", 3);
        for (int i = 1; i <= REEF_COLOURS; i++) {
            int slot = i - 1;
            int x = RUI_PAN_X + (slot % RUI_PAN_COLS) * (RUI_PAN_SW + RUI_PAN_PAD);
            int y = RUI_PAN_Y + (slot / RUI_PAN_COLS) * (RUI_PAN_SH + RUI_PAN_PAD);
            rui_swatch(fb, stride, x, y, RUI_PAN_SW, RUI_PAN_SH, i, i == s_colour);
        }
    }
    if (s_confirm) {
        const int W = 300, H = 118, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
        render_rect_blend(fb, stride, 0, 0, TANK_W, TANK_H, INK, 220);
        render_rect(fb, stride, X, Y, W, H, PANEL);
        render_rect_edge(fb, stride, X, Y, W, H, NO);
        render_text(fb, stride, X + (W - render_text_w("CLEAR THE WHOLE REEF?", 2)) / 2, Y + 20, 2, WHITE,
                    "CLEAR THE WHOLE REEF?");
        render_text(fb, stride, X + (W - render_text_w("THIS CANNOT BE UNDONE", 1)) / 2, Y + 44, 1, FAINT,
                    "THIS CANNOT BE UNDONE");
        render_button(fb, stride, X + 16, Y + H - 48, 120, 38, PANEL, FAINT, "KEEP IT", 2);
        render_button(fb, stride, X + W - 136, Y + H - 48, 120, 38, 0x3a1418, NO, "CLEAR", 2);
    }
}

void reef_ui_press(tank_t *t, float x, float y) {
    s_px = x; s_py = y; s_dragged = false;
    s_zx0 = s_zx; s_zy0 = s_zy;
    if (s_menu) { s_scroll0 = s_scroll; return; }
    if (s_rub) return;                           /* the magnifier pans; nothing to aim */
    rui_unzoom(&x, &y);
    rui_to_cell(t, x, y);
}
static void rui_scroll_to(float v) {
    int rows = (REEF_SHAPE_N + RUI_CAT_COLS - 1) / RUI_CAT_COLS;
    float span = (float)(rows * (RUI_TILE + RUI_TILE_PAD)) - (float)(RUI_FOOT_Y - RUI_MENU_Y);
    if (span < 0) span = 0;
    s_scroll = v < 0 ? 0 : v > span ? span : v;
}
void reef_ui_drag(tank_t *t, float x, float y) {
    float dx = x - s_px, dy = y - s_py;
    if (dx * dx + dy * dy > RUI_SLOP * RUI_SLOP) s_dragged = true;
    if (s_menu) {                                 /* the catalogue follows the finger */
        if (!s_panel && !s_confirm && s_dragged) rui_scroll_to(s_scroll0 - dy);
        return;
    }
    if (s_rub) { if (s_zoom > 1.01f && s_dragged) rui_pan(dx, dy); return; }
    rui_unzoom(&x, &y);
    rui_to_cell(t, x, y);
}
int reef_ui_tap(tank_t *t, float x, float y, float dx, float dy) {
    bool moved = dx * dx + dy * dy > RUI_SLOP * RUI_SLOP;
    if (!s_menu) {
        /* OUT mode: the magnifier owns the glass */
        if (s_rub) {
            if (s_zoom > 1.01f) {
                if (y >= RUI_ZBAR_Y - 6 && y < RUI_ZBAR_Y + RUI_BACK_H + 6) {
                    if (x < 8 + RUI_BACK_W) { s_zoom = 1.0f; return REEF_UI_KEPT; }         /* BACK */
                    if (x < 8 + RUI_BACK_W + 8 + 56) {                                       /* the zoom step */
                        s_zoom += 1.0f; if (s_zoom > RUI_ZOOM_MAX) s_zoom = 2.0f;
                        rui_pan(0, 0);
                        return REEF_UI_KEPT;
                    }
                }
                if (moved) return REEF_UI_KEPT;                      /* that was a pan */
                rui_unzoom(&x, &y);
                int i = reef_at(t, (int)(x / REEF_CELL), (int)(y / REEF_CELL));
                if (i >= 0) { reef_remove(t, i); return REEF_UI_KEPT; }
                return REEF_UI_NONE;
            }
            if (dy < -40 && fabsf(dx) < fabsf(dy)) { s_menu = true; return REEF_UI_KEPT; }
            if (moved) return REEF_UI_KEPT;
            /* the first tap does not take anything out: it goes in for a
               closer look, so a finger can pick one coral out of a crowd */
            s_zoom = 2.0f; s_zx = x; s_zy = y; s_zx0 = x; s_zy0 = y; rui_pan(0, 0);
            return REEF_UI_KEPT;
        }
        if (s_shape >= 0) {
            if (x >= TANK_W - 98 && y >= TANK_H - RUI_BTN_H - 16) { s_shape = -1; return REEF_UI_KEPT; }   /* DROP */
            if (dy < -70 && fabsf(dx) < fabsf(dy)) { s_menu = true; return REEF_UI_KEPT; }  /* back for another */
            /* LETTING GO plants it, wherever the drag ended. Dragging across
               the clear water at the top and releasing used to lose the
               piece, which is the one way of aiming that lets you see it. */
            rui_unzoom(&x, &y);
            rui_to_cell(t, x, y);
            if (s_settled && reef_place(t, s_shape, (uint8_t)s_colour, s_cx, s_cy)) return REEF_UI_KEPT;
            return REEF_UI_NONE;                                    /* refused: the ghost said so */
        }
        if (dy < -40 && fabsf(dx) < fabsf(dy)) { s_menu = true; return REEF_UI_KEPT; }
        return REEF_UI_NONE;
    }
    /* ---- the catalogue ---- */
    if (s_confirm) {
        const int W = 300, H = 118, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
        if (y >= Y + H - 54 && y < Y + H) {
            if (x >= X + W - 142) { reef_clear(t); s_shape = -1; s_rub = false; }
            s_confirm = false;
        }
        return REEF_UI_KEPT;
    }
    if (s_panel) {
        if (y < RUI_DONE_Y + RUI_DONE_H + 6 && x >= RUI_DONE_X - 6) { s_panel = false; return REEF_UI_KEPT; }
        for (int i = 1; i <= REEF_COLOURS; i++) {
            int slot = i - 1;
            int px = RUI_PAN_X + (slot % RUI_PAN_COLS) * (RUI_PAN_SW + RUI_PAN_PAD);
            int py = RUI_PAN_Y + (slot / RUI_PAN_COLS) * (RUI_PAN_SH + RUI_PAN_PAD);
            if (x >= px && x < px + RUI_PAN_SW && y >= py && y < py + RUI_PAN_SH) {
                s_colour = i; s_rub = false; s_panel = false; return REEF_UI_KEPT;
            }
        }
        return REEF_UI_KEPT;
    }
    if (dy > 40 && fabsf(dx) < fabsf(dy)) { s_menu = false; return REEF_UI_KEPT; }
    /* anything past the slop was a scroll (the drag already moved it), and a
       scroll must never also count as a tap on whatever it finished over */
    if (moved) { rui_scroll_to(s_scroll0 - dy); return REEF_UI_KEPT; }
    if (y < RUI_DONE_Y + RUI_DONE_H + 6 && x >= RUI_DONE_X - 6) { s_menu = false; return REEF_UI_CLOSE; }
    if (x < RUI_RAIL_W) {
        if (y >= RUI_SW_Y - 6 && y < RUI_SW_Y + RUI_SW_H + 6) { s_panel = true; return REEF_UI_KEPT; }
        if (y >= RUI_OUT_Y - 4 && y < RUI_OUT_Y + RUI_BTN_H + 4) {
            s_rub = !s_rub; s_zoom = 1.0f;
            if (s_rub) s_shape = -1;
            s_menu = false; return REEF_UI_KEPT;
        }
        if (y >= RUI_RESET_Y - 4 && y < RUI_RESET_Y + RUI_BTN_H + 4) { s_confirm = true; return REEF_UI_KEPT; }
        return REEF_UI_KEPT;
    }
    for (int i = 0; i < REEF_SHAPE_N; i++) {
        int col = i % RUI_CAT_COLS, row = i / RUI_CAT_COLS;
        int tx = RUI_CAT_X + col * (RUI_TILE + RUI_TILE_PAD);
        int ty = RUI_MENU_Y + row * (RUI_TILE + RUI_TILE_PAD) - (int)s_scroll;
        if (y > RUI_FOOT_Y) break;
        if (x >= tx && x < tx + RUI_TILE && y >= ty && y < ty + RUI_TILE) {
            s_shape = i; s_rub = false; s_menu = false; s_zoom = 1.0f;
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
