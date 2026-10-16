/* SPDX-License-Identifier: GPL-3.0-only
 *
 * planted: the harness's controls. A minimal, well-behaved mod that, built with -DFAULT=n,
 * breaks exactly one rule, so each harness check is shown failing on the fault it exists
 * to catch (and passing on FAULT_NONE) in the same run as the real mods.
 */
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "azmod-mod.h"

#define FAULT_NONE 0
#define FAULT_CRASH 1    /* no-crash:     writes through a null pointer on tick 500 */
#define FAULT_OUTSIDE 2    /* draw-area:    one rectangle just past the area's right edge */
#define FAULT_CONTEXT 3    /* draw-context: paints from tick with a kept paint context */
#define FAULT_TEXTSIZE 4    /* text-size:    draw_text at 6 px */
#define FAULT_IO 5    /* no-io:        opens a file from tick */
#define FAULT_ALLOC 6    /* no-alloc:     allocates from tick */
#define FAULT_REDRAW 7    /* redraw-rate:  request_redraw on every tick */
#define FAULT_LOG 8    /* log-quiet:    a log line every tick */
#define FAULT_API 9    /* api:          asks for deck 4 (decks are 0..3) */
#define FAULT_OVERFLOW 10   /* no-crash, sanitizer run only: tells deck_title its 8-byte
                             * buffer holds 64 bytes */
#define FAULT_HEAP 11   /* no-crash, ASan run only: writes one byte past an 8-byte
                             * heap block */
#define FAULT_UB 12   /* no-crash, UBSan run only: a signed integer overflow */
#define FAULT_RACE 13   /* no-crash, TSan run only: a worker thread writes a plain int
                             * that tick reads, with no lock or atomic */

#ifndef FAULT
#define FAULT FAULT_NONE
#endif

#define MOD_ID "planted"

static const az_core_v1 *C;
static unsigned ticks;
static int last_loaded = -1;
static az_draw *kept;

#if FAULT == FAULT_RACE
static int racy;

static void *racer(void *arg)
{
    (void)arg;
    for (;;) {
        racy++;
        struct timespec ts = { 0, 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}
#endif

static void on_draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    kept = d;
    C->draw_header(d, "Planted");
    int row = C->row_height(d);
    if (area.h >= 2 * row && row / 2 >= 8 && row / 2 <= 96 &&
        C->text_width("Deck 1", row / 2) <= area.w)
        C->draw_text(d, area.x, area.y + row, last_loaded == 1 ? "Deck 1" : "Empty", row / 2,
                     C->color(AZ_COL_TEXT));
#if FAULT == FAULT_OUTSIDE
    C->fill_rect(d, area.x + area.w - 4, area.y, 5, 1, C->color(AZ_COL_TEXT));
#elif FAULT == FAULT_TEXTSIZE
    C->draw_text(d, area.x, area.y, "", 6, C->color(AZ_COL_TEXT));
#endif
}

static int on_input(const az_event *ev, void *user)
{
    (void)user;
    return ev->kind == AZ_EV_BACK ? 0 : 1;
}

static const az_screen_v1 SCREEN = {
    .size = sizeof(az_screen_v1),
    .on_draw = on_draw,
    .on_input = on_input,
};

static void tick(void)
{
    ticks++;
    int loaded = C->deck_loaded(0);
    if (loaded != last_loaded) {
        last_loaded = loaded;
        C->request_redraw();
    }
#if FAULT == FAULT_CRASH
    if (ticks == 500) *(volatile int *)0 = 1;
#elif FAULT == FAULT_CONTEXT
    if (kept && ticks == 500) C->fill_rect(kept, 0, 0, 1, 1, 0);
#elif FAULT == FAULT_IO
    if (ticks == 500) {
        int fd = open("/proc/self/stat", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) close(fd);
    }
#elif FAULT == FAULT_ALLOC
    if (ticks == 500) {
        void *volatile p = malloc(64);      /* volatile: the compiler may not drop the pair */
        free(p);
    }
#elif FAULT == FAULT_REDRAW
    C->request_redraw();
#elif FAULT == FAULT_LOG
    C->log(MOD_ID, "tick %u", ticks);
#elif FAULT == FAULT_API
    if (ticks == 500) (void)C->deck_loaded(4);
#elif FAULT == FAULT_OVERFLOW
    if (ticks == 500) {
        char small[8];
        (void)C->deck_title(0, small, 64);
    }
#elif FAULT == FAULT_HEAP
    if (ticks == 500) {
        char *volatile p = malloc(8);
        volatile int i = 8;
        p[i] = 1;
        free(p);
    }
#elif FAULT == FAULT_UB
    if (ticks == 500) {
        volatile int big = INT_MAX;
        volatile int one = (int)(ticks / 500);
        big = big + one;
    }
#elif FAULT == FAULT_RACE
    if (ticks >= 500 && racy < 0) C->log(MOD_ID, "never");
#endif
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
#if FAULT == FAULT_RACE
    if (C->thread_start(MOD_ID, "racer", racer, NULL) != 0) return 4;
#endif
    return C->register_screen(MOD_ID, &SCREEN) == 0 ? 0 : 3;
}

static const char *const FW_TAGS[] = { "any", NULL };
static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = tick,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
