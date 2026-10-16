/* SPDX-License-Identifier: GPL-3.0-only
 *
 * hello-az: the AZ-MODS SDK template mod.
 *
 * What it shows:
 *   - the one exported entry point and the descriptor (az_mod_v1)
 *   - doing all setup in init(), never in a constructor
 *   - a settings page under Mod Menu > Community Mods > Installed > Hello AZ
 *   - a checkbox setting stored with cfg_get / cfg_set
 *   - a live readout of each loaded deck's BPM and track title, using only the
 *     read-only deck services (no reverse engineering, so it is "firmware": ["any"])
 *   - theme colors via color(AZ_COL_*), so it matches Day and the dark themes
 *   - a tick() that only does cheap bookkeeping and asks for a repaint on change,
 *     paced by the monotonic clock (tick's rate varies, so it is never counted as time)
 *
 * Start your own mod with `azmod-pack new`, which copies this folder under your id.
 * MOD_ID here and "id" in azmod.json must always match.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "hello-az"
#define MAX_DECKS 4

static const az_core_v1 *C;          /* set once in init(), never changes after */

/* Settings, cached in memory. cfg_get is read once in init(); cfg_set on change. */
static int show_titles = 1;

/* Page state. Only touched from on_draw / on_input / on_enter / tick, which the core
 * calls on its UI thread, so no locking is needed. */
static int highlighted = 0;          /* 0 = the checkbox row is highlighted */
static az_rect titles_row;           /* where on_draw put the checkbox row, for taps */

/* What tick() last saw, to request a repaint only when something changed, and at most
 * once every MIN_REDRAW_MS (4 a second), however fast the decks change. */
#define MIN_REDRAW_MS 250
static long last_redraw_ms = -MIN_REDRAW_MS;
static int dirty;
static int seen_count = -1;
static int seen_loaded[MAX_DECKS];
static double seen_bpm[MAX_DECKS];

/* The read-only deck services are part of ABI v1, but a mod should still check that
 * the core it runs on has them (AZ_HAS), so the same build keeps working, minus the
 * missing feature, on a core that is older or newer than the header it was built with. */
static int have_deck_services(void)
{
    return AZ_HAS(C, deck_count) && AZ_HAS(C, deck_loaded) &&
           AZ_HAS(C, deck_bpm) && AZ_HAS(C, deck_title);
}

static int deck_count(void)
{
    if (!have_deck_services()) return 0;
    int n = C->deck_count();
    if (n < 0) return 0;              /* unknown: show nothing rather than guess */
    if (n > MAX_DECKS) n = MAX_DECKS;
    return n;
}

static void toggle_titles(void)
{
    show_titles = !show_titles;
    C->cfg_set(MOD_ID, "show_titles", show_titles);
    C->log(MOD_ID, "show_titles=%d", show_titles);
    if (AZ_HAS(C, request_redraw)) C->request_redraw();
}

/* ---- the settings page ------------------------------------------------------------ */

/* Draw s, shortened from the end (whole UTF-8 characters) until it fits in max_w pixels.
 * The core cuts drawing off at the page's edge; it does not shorten text to fit. */
static void draw_fitted(az_draw *d, int x, int y, const char *s, int px, uint32_t color, int max_w)
{
    char buf[160];
    snprintf(buf, sizeof buf, "%s", s);
    size_t len = strlen(buf);
    while (len > 0 && AZ_HAS(C, text_width) && C->text_width(buf, px) > max_w) {
        do len--;
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
        buf[len] = '\0';
    }
    if (len > 0) C->draw_text(d, x, y, buf, px, color);
}

/* One checkbox row at *y, if it fits above the area's bottom: its tap target goes in *hit and
 * *y moves down a row. Returns 0, with *hit empty, when the row does not fit: the caller stops
 * there, so nothing is drawn below the page (the harness's smallest page holds the header and
 * one row). */
static int draw_setting_row(az_draw *d, az_rect area, int *y, const char *label, int on,
                            int highlight, az_rect *hit)
{
    const int row = C->row_height(d);
    const int pad = row / 4, box = row - 2 * pad;
    int px = row / 2;
    if (px < 8) px = 8;
    if (px > 96) px = 96;
    *hit = (az_rect){ 0, 0, 0, 0 };
    if (*y + row > area.y + area.h) return 0;
    *hit = (az_rect){ area.x, *y, area.w, row };
    C->draw_checkbox(d, area.x + pad, *y + pad, box, on, highlight);
    draw_fitted(d, area.x + 2 * pad + box, *y + pad, label, px, C->color(AZ_COL_TEXT),
                area.w - 3 * pad - box);
    *y += row;
    return 1;
}

static void on_draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    const uint32_t text = C->color(AZ_COL_TEXT);
    const uint32_t dim = C->color(AZ_COL_TEXT_DIM);
    const int row = C->row_height(d);
    int px = row / 2;                 /* text size relative to the standard row, */
    if (px < 8) px = 8;               /* within the 8..96 px draw_text accepts */
    if (px > 96) px = 96;
    const int pad = row / 4;
    const int bottom = area.y + area.h;   /* every row stops here: nothing is drawn past it */
    int y = area.y;

    C->draw_header(d, "Hello AZ");
    y += row;                         /* leave the header's row */

    /* The setting rows; each stops the page when it does not fit. */
    if (!draw_setting_row(d, area, &y, "Show track titles", show_titles, highlighted == 0,
                          &titles_row))
        return;

    /* A thin divider in the dim text color. */
    if (y + pad > bottom) return;
    C->fill_rect(d, area.x + pad, y + pad / 2, area.w - 2 * pad, 1, dim);
    y += pad;

    /* Rows 2..: one per deck in the current view. */
    int n = deck_count();
    if (n == 0) {
        if (y + row > bottom) return;
        draw_fitted(d, area.x + pad, y + pad, "Deck information is not available.", px, dim,
                    area.w - 2 * pad);
        return;
    }
    for (int deck = 0; deck < n && y + row <= bottom; deck++) {
        char line[160];
        if (!C->deck_loaded(deck)) {
            snprintf(line, sizeof line, "Deck %d   no track", deck + 1);
            draw_fitted(d, area.x + pad, y + pad, line, px, dim, area.w - 2 * pad);
        } else {
            double bpm = C->deck_bpm(deck);
            char bpm_s[24];
            if (bpm > 0.0 && bpm < 1000.0) snprintf(bpm_s, sizeof bpm_s, "%.2f BPM", bpm);
            else snprintf(bpm_s, sizeof bpm_s, "--- BPM");

            char title[96] = "";
            if (show_titles && C->deck_title(deck, title, (int)sizeof title) <= 0)
                title[0] = '\0';      /* unknown title: print nothing, not a guess */
            title[sizeof title - 1] = '\0';

            snprintf(line, sizeof line, "Deck %d   %s%s%s", deck + 1, bpm_s,
                     title[0] ? "   " : "", title);
            draw_fitted(d, area.x + pad, y + pad, line, px, text, area.w - 2 * pad);
        }
        y += row;
    }
}

static int on_input(const az_event *ev, void *user)
{
    (void)user;
    switch (ev->kind) {
    case AZ_EV_KNOB:
        /* One selectable row: keep the highlight on it. A page with more rows would
         * move `highlighted` by ev->dir here. */
        highlighted = 0;
        return 1;
    case AZ_EV_CLICK:
        toggle_titles();
        return 1;
    case AZ_EV_TAP:
        /* only a tap on the checkbox row toggles it; a tap anywhere else does nothing */
        if (ev->x < titles_row.x || ev->x >= titles_row.x + titles_row.w ||
            ev->y < titles_row.y || ev->y >= titles_row.y + titles_row.h)
            return 0;
        toggle_titles();
        return 1;
    case AZ_EV_BACK:
    default:
        return 0;                     /* let the core handle BACK and anything else */
    }
}

static void on_enter(void *user)
{
    (void)user;
    highlighted = 0;
}

static const az_screen_v1 SCREEN = {
    .size = sizeof(az_screen_v1),
    .on_draw = on_draw,
    .on_input = on_input,
    .on_enter = on_enter,
    .user = NULL,
};

/* ---- lifecycle -------------------------------------------------------------------- */

/* Milliseconds on the monotonic clock: steady, and unaffected by the deck setting its
 * clock from the network. */
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Periodic, up to about 30 times a second at a rate that varies: anything timed uses
 * now_ms(), never a count of ticks. Bookkeeping only: read a few numbers, compare, maybe
 * ask for a repaint. No file or network I/O, no blocking, no allocation. */
static void tick(void)
{
    if (!AZ_HAS(C, request_redraw)) return;
    int n = deck_count();
    int changed = (n != seen_count);
    seen_count = n;
    for (int deck = 0; deck < n; deck++) {
        int loaded = C->deck_loaded(deck);
        double bpm = loaded ? C->deck_bpm(deck) : 0.0;
        /* Unknown, implausible or not a number: no tempo. Converting an out-of-range
         * double to an integer (below) is undefined behavior, so it must never get there. */
        if (!(bpm > 0.0 && bpm < 1000.0)) bpm = 0.0;
        /* Compare the shown value (two decimals), not the raw number, so a change
         * the page would not show asks for no repaint. */
        long shown = (long)(bpm * 100.0 + 0.5);
        if (loaded != seen_loaded[deck] || shown != (long)(seen_bpm[deck] * 100.0 + 0.5))
            changed = 1;
        seen_loaded[deck] = loaded;
        seen_bpm[deck] = bpm;
    }
    if (changed) dirty = 1;
    long now = now_ms();
    if (dirty && now - last_redraw_ms >= MIN_REDRAW_MS) {
        dirty = 0;
        last_redraw_ms = now;
        C->request_redraw();
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;

    show_titles = C->cfg_get(MOD_ID, "show_titles", 1) ? 1 : 0;

    if (C->register_screen(MOD_ID, &SCREEN) != 0) {
        C->log(MOD_ID, "register_screen refused; staying loaded without a page");
    }
    C->log(MOD_ID, "loaded on %s, show_titles=%d", C->fw_tag ? C->fw_tag() : "?", show_titles);
    return 0;
}

/* "any": this mod uses only core services and claims no hooks, so it loads on every
 * firmware. A mod that claims hooks lists the exact firmware tags it was tested on. */
static const char *const FW_TAGS[] = { "any", NULL };

static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = tick,
    .on_frame = NULL,                 /* this mod does not look at control frames */
};

/* The one exported symbol. AZ_MOD_EXPORT on the declaration in azmod-mod.h keeps it
 * visible under -fvisibility=hidden. */
const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
