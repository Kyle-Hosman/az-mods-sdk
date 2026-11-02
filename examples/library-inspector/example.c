/* SPDX-License-Identifier: GPL-3.0-only */
/* One diagnostic client of the core's shared service. No parsing, native addresses,
 * private hooks, independent cache or command implementation lives in this mod. */
#include "pages.h"
#include <time.h>
const az_library_v1 *library;
static const az_core_v1 *core;
static const library_page *const pages[] = {
    &library_page_cues, &library_page_live, &library_page_analysis, &library_page_control
};
static const char *const titles[] = { "Cues", "Live", "Analysis", "Control" };
static unsigned active;
static az_rect tabs;
static uint64_t last_redraw;
static void draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    if (area.w < 32 || area.h < 64) return;
    tabs = (az_rect){area.x, area.y, area.w, 40};
    for (unsigned i = 0; i < 4; i++) {
        int left = area.x + (int)i * area.w / 4;
        int right = area.x + (int)(i + 1) * area.w / 4;
        uint32_t bg = core->color(i == active ? AZ_COL_HI : AZ_COL_BG);
        uint32_t fg = core->color(i == active ? AZ_COL_HI_TEXT : AZ_COL_TEXT);
        core->fill_rect(d, left, area.y, right - left, 36, bg);
        if (core->text_width(titles[i], 16) + 12 <= right - left)
            core->draw_text(d, left + 6, area.y + 8, titles[i], 16, fg);
    }
    area.y += 40;
    area.h -= 40;
    pages[active]->draw(d, area, NULL);
}
static int input(const az_event *ev, void *user)
{
    (void)user;
    if (ev->kind == AZ_EV_TAP && tabs.w > 0 && ev->x >= tabs.x && ev->x < tabs.x + tabs.w &&
        ev->y >= tabs.y && ev->y < tabs.y + tabs.h) {
        active = (unsigned)(ev->x - tabs.x) * 4u / (unsigned)tabs.w;
        return 1;
    }
    if (pages[active]->input) return pages[active]->input(ev, NULL);
    if (ev->kind == AZ_EV_KNOB && ev->dir) {
        active = (active + (ev->dir < 0 ? 3u : 1u)) % 4;
        return 1;
    }
    return 0;
}
static void tick(void)
{
    for (unsigned i = 0; i < 4; i++) pages[i]->tick();
    struct timespec now;
    if (!clock_gettime(CLOCK_MONOTONIC, &now)) {
        uint64_t ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + now.tv_nsec;
        if (ns - last_redraw >= UINT64_C(250000000)) {
            core->request_redraw();
            last_redraw = ns;
        }
    }
}
static int init(const az_core_v1 *c)
{
    if (!AZ_HAS(c, library_service)) return -1;
    library = c->library_service(AZ_LIBRARY_ABI);
    if (!AZ_LIB_HAS(library, data_capabilities) || !AZ_LIB_HAS(library, deck_snapshot)) return -1;
    core = c;
    for (unsigned i = 0; i < 4; i++) if (pages[i]->init(c)) return -1;
    core->log(LIBRARY_INSPECTOR_ID, "shared core library service ABI %u, one inspector client", library->abi);
    static az_screen_v1 screen = {0};
    screen.size = sizeof screen;
    screen.on_draw = draw;
    screen.on_input = input;
    return core->register_screen(LIBRARY_INSPECTOR_ID, &screen);
}
static const char *const tags[] = { "any", NULL };
static const az_mod_v1 mod = {
    .abi = AZ_MOD_ABI, .size = sizeof(az_mod_v1), .id = LIBRARY_INSPECTOR_ID,
    .fw_tags = tags, .init = init, .tick = tick
};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
