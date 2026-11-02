/* SPDX-License-Identifier: GPL-3.0-only */
#include "pages.h"
#include "azmod-timestamps.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define ID LIBRARY_INSPECTOR_ID
#define TRACKS 64
static const az_core_v1 *core;
static struct {
    az_track_ref ref;
    char title[128];
} catalog[TRACKS];
static _Atomic int catalog_ready;
static int catalog_count, catalog_status, selected, issued = -1, browse_cues, cue_cursor;
static uint32_t cue_count;
static uint64_t ticket, retry_after_ns;
static int retry_selected = -1;
static char lines[8][112];
static uint32_t row_colors[5];
static unsigned row_colored[5];
static void clear_cues(void)
{
    cue_cursor = 0;
    cue_count = 0;
    memset(row_colors, 0, sizeof row_colors);
    memset(row_colored, 0, sizeof row_colored);
    for (int i = 3; i < 8; i++) lines[i][0] = 0;
}
static int pick(const az_track *track, void *user)
{
    (void)user;
    if (catalog_count == TRACKS) return 1;
    az_track_ref ref = { 0 };
    ref.size = sizeof ref;
    catalog_status = library->track_resolve(ID, 0, track->id, &ref);
    if (catalog_status != AZ_OK) return 1;
    catalog[catalog_count].ref = ref;
    snprintf(catalog[catalog_count].title, sizeof catalog[0].title, "%.100s", track->title ? track->title : "");
    return ++catalog_count == TRACKS;
}
static void *enumerate(void *arg)
{
    (void)arg;
    catalog_count = 0;
    catalog_status = AZ_OK;
    int rc = library->lib_tracks(ID, 0, 0, pick, NULL);
    if (rc < 0) catalog_status = AZ_FAILED;
    atomic_store_explicit(&catalog_ready, 1, memory_order_release);
    return NULL;
}
static void draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    if (area.w < 32 || area.h < 32) return;
    int y = area.y + 8;
    for (int i = 0; i < 8 && y + 24 < area.y + area.h; i++, y += 28) {
        uint32_t color = i >= 3 && row_colored[i - 3] ? row_colors[i - 3] : core->color(AZ_COL_TEXT);
        if (core->text_width(lines[i], 16) < area.w - 16) core->draw_text(d, area.x + 8, y, lines[i], 16, color);
    }
}
static void tick(void)
{
    if (!atomic_load_explicit(&catalog_ready, memory_order_acquire)) return;
    if (!catalog_count) {
        snprintf(lines[1], sizeof lines[1], "USB 1: no tracks available (status %d)", catalog_status);
        return;
    }
    if (selected != issued) {
        struct timespec now;
        if (retry_selected == selected && !clock_gettime(CLOCK_MONOTONIC, &now) &&
            compare_timestamps((uint64_t)now.tv_sec * UINT64_C(1000000000) + now.tv_nsec, retry_after_ns, 1000000000, 1000000000, 1) == AZ_TIME_BEFORE) return;
        retry_selected = -1;
        if (ticket && library->track_release(ID, ticket) != AZ_OK) return;
        ticket = 0;
        clear_cues();
        snprintf(lines[1], sizeof lines[1], "%d/%d: %.80s", selected + 1, catalog_count, catalog[selected].title);
        int rc = library->track_request(ID, &catalog[selected].ref, AZ_DATA_METADATA | AZ_DATA_CUES, &ticket);
        if (rc == AZ_BUSY) return;
        issued = selected;
        if (rc != AZ_OK) {
            snprintf(lines[2], sizeof lines[2], "Request status %d", rc);
            return;
        }
    }
    if (!ticket) return;
    const az_track_snapshot *s = NULL;
    int rc = library->track_acquire(ID, ticket, &s);
    if (rc == AZ_BUSY) return;
    if (rc != AZ_OK) {
        clear_cues();
        if (rc == AZ_PENDING) snprintf(lines[2], sizeof lines[2], "Loading saved cues");
        else snprintf(lines[2], sizeof lines[2], "Track data status %d", rc);
        if (rc != AZ_PENDING && library->track_release(ID, ticket) == AZ_OK) {
            ticket = 0;
            /* A backing-file change need not invalidate its source reference. Retry
             * at most once a second; a stale source is refused without old cue rows. */
            if (rc == AZ_STALE) {
                struct timespec now;
                if (!clock_gettime(CLOCK_MONOTONIC, &now)) {
                    retry_after_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + now.tv_nsec + UINT64_C(1000000000);
                    retry_selected = selected;
                    issued = -1;
                }
            }
        }
        return;
    }
    cue_count = s->cue_count;
    if (s->cues_info.availability != AZ_AVAIL_READY && s->cues_info.availability != AZ_AVAIL_EMPTY) {
        clear_cues();
        snprintf(lines[2], sizeof lines[2], "Saved cue data unavailable");
        return;
    }
    snprintf(lines[2], sizeof lines[2], "%u saved entries; knob: %s; click switches", cue_count, browse_cues ? "cues" : "tracks");
    for (int i = 0; i < 5; i++) {
        lines[i + 3][0] = 0;
        row_colors[i] = 0;
        row_colored[i] = 0;
        uint32_t index = (uint32_t)cue_cursor + (uint32_t)i;
        if (index >= cue_count) continue;
        const az_cue_entry *c = &s->cues[index];
        snprintf(lines[i + 3], sizeof lines[i + 3], "%s %u %.3fs%s%.3fs %.28s",
                 c->classification == AZ_CUE_HOT ? "HOT" : "MEM", c->entry_id, c->start_s,
                 c->type == AZ_CUE_LOOP ? " loop to " : " point ", c->type == AZ_CUE_LOOP ? c->end_s : c->start_s, c->comment);
        if (c->valid & AZ_CUE_COLOR) {
            row_colors[i] = c->rgb;
            row_colored[i] = 1;
        }
    }
    /* One bounded ticket pins the selected track. A new selection releases it first. */
}
static int input(const az_event *event, void *user)
{
    (void)user;
    if (!atomic_load_explicit(&catalog_ready, memory_order_acquire) || !catalog_count) return 0;
    if (event->kind == AZ_EV_CLICK) {
        browse_cues = !browse_cues;
        return 1;
    }
    if (event->kind == AZ_EV_BACK && browse_cues) {
        browse_cues = 0;
        return 1;
    }
    if (event->kind != AZ_EV_KNOB || !event->dir) return 0;
    int step = event->dir < 0 ? -1 : 1;
    if (browse_cues && cue_count) cue_cursor = (cue_cursor + step + (int)cue_count) % (int)cue_count;
    else selected = (selected + step + catalog_count) % catalog_count;
    return 1;
}
static int init(const az_core_v1 *c)
{
    if (!AZ_LIB_HAS(library, data_capabilities) || !AZ_LIB_HAS(library, track_resolve) || !AZ_LIB_HAS(library, track_request) ||
        !AZ_LIB_HAS(library, track_acquire) || !AZ_LIB_HAS(library, track_release) || !AZ_HAS(c, thread_start) || !AZ_LIB_HAS(library, lib_tracks)) return -1;
    core = c;
    ticket = 0;
    selected = 0;
    issued = -1;
    browse_cues = 0;
    cue_cursor = 0;
    atomic_store_explicit(&catalog_ready, 0, memory_order_relaxed);
    snprintf(lines[0], sizeof lines[0], "USB 1 cue browser (first %d tracks)", TRACKS);
    snprintf(lines[1], sizeof lines[1], "Loading library");
    az_data_capabilities caps = { 0 };
    caps.size = sizeof caps;
    if (library->data_capabilities(&caps) != AZ_OK || !(caps.stored_components & AZ_DATA_CUES)) {
        snprintf(lines[1], sizeof lines[1], "Track data unavailable");
        return 0;
    }
    return core->thread_start(ID, "cue-browser", enumerate, NULL);
}

const library_page library_page_cues = { init, tick, draw, input };
