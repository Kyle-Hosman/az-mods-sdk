/* SPDX-License-Identifier: GPL-3.0-only */
#include "pages.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define ID LIBRARY_INSPECTOR_ID
static const az_core_v1 *core;
static _Atomic uint64_t ready_ticket;
static uint64_t ticket, wave_ticket;
static _Atomic uint64_t ready_wave;
static int status = AZ_PENDING;
static char lines[8][112];
static uint8_t preview[64];
static unsigned preview_count;
static int detail_ready;
static void draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    if (area.w < 32 || area.h < 32) return;
    uint32_t color = core->color(AZ_COL_TEXT);
    int y = area.y + 8;
    /* Fixed short lines fit at the native screen size; tiny harness areas show status only. */
    for (int i = 0; i < 8 && y + 24 < area.y + area.h; i++, y += 28) {
        if (core->text_width(lines[i], 16) < area.w - 16)
            core->draw_text(d, area.x + 8, y, lines[i], 16, color);
    }
    int base = area.y + area.h - 16;
    int plot_height = base - y - 8;
    if (plot_height > 80) plot_height = 80;
    if (area.w > 144 && plot_height > 0 && preview_count) {
        int width = area.w - 16;
        for (unsigned i = 0; i < preview_count; i++) {
            int x = area.x + 8 + (int)i * width / (int)preview_count;
            int next = area.x + 8 + (int)(i + 1) * width / (int)preview_count;
            int height = (int)preview[i] * plot_height / 255;
            if (height) core->fill_rect(d, x, base - height, next - x - 1, height, color);
        }
    }
}
static uint32_t first_track;
static int pick(const az_track *track, void *user)
{
    (void)user;
    first_track = track->id;
    return 1;
}
static void *load(void *arg)
{
    (void)arg;
    if (library->lib_tracks(ID, 0, 0, pick, NULL) < 0 || !first_track) return NULL;
    az_track_ref ref = { 0 };
    ref.size = sizeof ref;
    if (library->track_resolve(ID, 0, first_track, &ref) != AZ_OK) return NULL;
    uint64_t t = 0;
    int rc;
    do {
        rc = library->track_request(ID, &ref, 31, &t);
        if (rc == AZ_BUSY) usleep(1000);
    } while (rc == AZ_BUSY);
    if (rc == AZ_OK) atomic_store_explicit(&ready_ticket, t, memory_order_release);
    if (AZ_LIB_HAS(library, waveform_request) && AZ_LIB_HAS(library, waveform_acquire)) {
        az_wave_query query = {sizeof query, AZ_DATA_VERSION, AZ_WAVE_PWV3_MONO, 0, 4096, 0};
        do {
            rc = library->waveform_request(ID, &ref, &query, &t);
            if (rc == AZ_BUSY) usleep(1000);
        } while (rc == AZ_BUSY);
        if (rc == AZ_OK) atomic_store_explicit(&ready_wave, t, memory_order_release);
    }
    return NULL;
}
static void tick_track(void)
{
    if (!core) return;
    if (!ticket) ticket = atomic_exchange_explicit(&ready_ticket, 0, memory_order_acq_rel);
    if (!ticket) return;
    const az_track_snapshot *s = NULL;
    int rc = library->track_acquire(ID, ticket, &s);
    if (rc == AZ_BUSY || rc == AZ_PENDING) return;
    status = rc;
    if (rc != AZ_OK) {
        snprintf(lines[1], sizeof lines[1], "Track data status %d", rc);
        if (library->track_release(ID, ticket) == AZ_OK) ticket = 0;
        return;
    }
    snprintf(lines[1], sizeof lines[1], "%.80s", s->metadata.title);
    snprintf(lines[2], sizeof lines[2], "%u beats, %u phrases", s->beat_count, s->phrase_count);
    snprintf(lines[3], sizeof lines[3], "Grid availability %u, phrases %u", s->grid_info.availability, s->phrases_info.availability);
    if (!detail_ready) snprintf(lines[4], sizeof lines[4], "Wave: %u samples, %.6f seconds/sample", s->waveform.sample_count, s->waveform.seconds_per_sample);
    if (s->phrase_count) snprintf(lines[5], sizeof lines[5], "Raw phrase kind %u, mood %u", s->phrases[0].raw_kind, s->phrases[0].raw_mood);
    if (s->waveform.sample_count) snprintf(lines[6], sizeof lines[6], "First raw overview height %u", s->waveform.samples[0]);
    if (AZ_LIB_HAS(library, time_to_beat)) {
        double beat;
        int timing = library->time_to_beat(s, .25, &beat);
        if (timing == AZ_OK) snprintf(lines[7], sizeof lines[7], "At 0.25 source seconds: beat %.3f", beat);
    }
    if (!detail_ready) {
    preview_count = 0;
    if (s->waveform.sample_count && s->waveform.samples) {
        preview_count = s->waveform.sample_count < 64 ? s->waveform.sample_count : 64;
        for (unsigned i = 0; i < preview_count; i++) {
            uint32_t a = i * s->waveform.sample_count / preview_count;
            uint32_t b = (i + 1) * s->waveform.sample_count / preview_count;
            uint8_t peak = 0;
            for (uint32_t j = a; j < b; j++)
                if (s->waveform.samples[j] > peak) peak = s->waveform.samples[j];
            preview[i] = peak;
        }
        snprintf(lines[6], sizeof lines[6], "Overview chart: raw PWAV byte values");
    }
    }
    /* Copy into owned display text, then release the immutable snapshot. */
    if (library->track_release(ID, ticket) == AZ_OK) ticket = 0;
}
static void tick(void)
{
    tick_track();
    if (!wave_ticket) wave_ticket = atomic_exchange_explicit(&ready_wave, 0, memory_order_acq_rel);
    if (!wave_ticket) return;
    const az_wave_snapshot *w = NULL;
    int rc = library->waveform_acquire(ID, wave_ticket, &w);
    if (rc == AZ_BUSY || rc == AZ_PENDING) return;
    if (rc == AZ_OK) {
        detail_ready = 1;
        snprintf(lines[4], sizeof lines[4], "Detail: %u/%u columns, %.6f source seconds/column", w->column_count, w->total_columns, w->seconds_per_column);
        preview_count = w->column_count < 64 ? w->column_count : 64;
        for (unsigned i = 0; i < preview_count; i++) {
            uint32_t a = i * w->column_count / preview_count;
            uint32_t b = (i + 1) * w->column_count / preview_count;
            uint8_t peak = 0;
            for (uint32_t j = a; j < b; j++) if ((w->columns[j] & 31) > peak) peak = w->columns[j] & 31;
            preview[i] = (uint8_t)(peak * 255u / 31u);
        }
        snprintf(lines[6], sizeof lines[6], "Detail chart: PWV3 height, first %.2f source seconds", w->column_count * w->seconds_per_column);
    } else snprintf(lines[4], sizeof lines[4], "Detailed waveform unavailable (status %d)", rc);
    if (library->track_release(ID, wave_ticket) == AZ_OK) wave_ticket = 0;
}
static int init(const az_core_v1 *c)
{
    if (!AZ_LIB_HAS(library, data_capabilities) || !AZ_LIB_HAS(library, deck_snapshot)) return -1;
    core = c;
    if (!AZ_LIB_HAS(library, track_resolve) || !AZ_LIB_HAS(library, track_request) || !AZ_LIB_HAS(library, track_acquire) ||
        !AZ_LIB_HAS(library, track_release) || !AZ_HAS(c, thread_start) || !AZ_LIB_HAS(library, lib_tracks)) return -1;
    snprintf(lines[0], sizeof lines[0], "Beat, phrase and waveform viewer");
    /* Owner-scoped services authenticate their caller's return address. Keep this call
     * inside the mod even when init has no more work (see the Makefile's sibling flag). */
    return core->thread_start(ID, "track-browser", load, NULL);
}

const library_page library_page_analysis = { init, tick, draw, NULL };
