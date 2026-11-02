/* SPDX-License-Identifier: GPL-3.0-only */
#include "pages.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define ID LIBRARY_INSPECTOR_ID
static const az_core_v1 *core;
static char lines[10][112];
static void draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    if (area.w < 32 || area.h < 32) return;
    uint32_t color = core->color(AZ_COL_TEXT);
    int y = area.y + 8;
    /* Fixed short lines fit at the native screen size; tiny harness areas show status only. */
    for (int i = 0; i < 10 && y + 24 < area.y + area.h; i++, y += 28) {
        if (core->text_width(lines[i], 16) < area.w - 16)
            core->draw_text(d, area.x + 8, y, lines[i], 16, color);
    }
}
static void tick(void)
{
    if (!core) return;
    for (int deck = 0; deck < 4; deck++) {
        az_deck_snapshot s = { 0 };
        s.size = sizeof s;
        if (library->deck_snapshot(deck, &s) != AZ_OK) continue;
        snprintf(lines[deck * 2 + 1], sizeof lines[deck * 2 + 1], "Deck %d: loaded %d, loop %s", deck + 1,
                 s.valid & AZ_LIVE_LOADED ? s.loaded : -1,
                 s.loop.valid & AZ_LOOP_ENGAGED ? (s.loop.engaged ? "engaged" : "off") : "unavailable");
        if (s.loop.valid & AZ_LOOP_BOUNDS)
            snprintf(lines[deck * 2 + 2], sizeof lines[deck * 2 + 2], "  Working IN %.6f s / OUT %.6f s", s.loop.in_s, s.loop.out_s);
        else
            snprintf(lines[deck * 2 + 2], sizeof lines[deck * 2 + 2], "  Working bounds unavailable; retained points unknown");
    }
}
static int init(const az_core_v1 *c)
{
    if (!AZ_LIB_HAS(library, data_capabilities) || !AZ_LIB_HAS(library, deck_snapshot)) return -1;
    core = c;
    snprintf(lines[0], sizeof lines[0], "Live deck and loop inspector");
    return 0;
}

const library_page library_page_live = { init, tick, draw, NULL };
