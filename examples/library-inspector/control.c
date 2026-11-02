/* SPDX-License-Identifier: GPL-3.0-only */
#include "pages.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#define ID LIBRARY_INSPECTOR_ID
static const az_core_v1 *core;
static int status = AZ_PENDING, attempted;
static uint64_t pending_request;
static char lines[8][112];
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
}
static void tick(void)
{
    if (!core) return;
    az_data_capabilities caps = { 0 };
    caps.size = sizeof caps;
    if (library->data_capabilities(&caps) != AZ_OK) return;
    if (pending_request && AZ_LIB_HAS(library, command_poll) && AZ_LIB_HAS(library, command_release)) {
        az_command_result result = { 0 };
        result.size = sizeof result;
        status = library->command_poll(ID, pending_request, &result);
        if (status == AZ_OK) {
            snprintf(lines[3], sizeof lines[3], "Request %llu: outcome %u, reason %d",
                     (unsigned long long)result.request_id, result.outcome, result.reason);
            if (result.outcome != AZ_COMMAND_ACCEPTED && library->command_release(ID, pending_request) == AZ_OK)
                pending_request = 0;
        }
    }
    snprintf(lines[1], sizeof lines[1], "Loop EXIT capability: %s", caps.commands & (UINT64_C(1) << AZ_CMD_LOOP_EXIT) ? "available" : "unavailable");
    if (attempted) snprintf(lines[2], sizeof lines[2], "Last attempt status %d", status);
    else snprintf(lines[2], sizeof lines[2], "No command submitted");
}
static int input(const az_event *event, void *user)
{
    (void)user;
    if (event->kind != AZ_EV_CLICK) return 0;
    if (pending_request) return 1;
    attempted = 1;
    az_data_capabilities caps = { 0 };
    caps.size = sizeof caps;
    if (library->data_capabilities(&caps) != AZ_OK || !(caps.commands & (UINT64_C(1) << AZ_CMD_LOOP_EXIT))) {
        status = AZ_UNSUPPORTED;
        return 1;
    }
    az_deck_snapshot deck = { 0 };
    deck.size = sizeof deck;
    if (library->deck_snapshot(0, &deck) != AZ_OK || !(deck.valid & AZ_LIVE_NATIVE_LOAD) || !deck.native_load_generation) {
        status = AZ_STALE;
        return 1;
    }
    static uint64_t next_request;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        status = AZ_FAILED;
        return 1;
    }
    az_command_request request = { 0 };
    request.size = sizeof request;
    request.version = AZ_DATA_VERSION;
    request.operation = AZ_CMD_LOOP_EXIT;
    request.expected_load_generation = deck.native_load_generation;
    request.flags = AZ_COMMAND_NATIVE_LOAD;
    if (next_request == UINT64_MAX) {
        status = AZ_LIMIT;
        return 1;
    }
    request.request_id = ++next_request;
    request.deadline_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + now.tv_nsec + UINT64_C(5000000000);
    az_command_result result = { 0 };
    result.size = sizeof result;
    if (!AZ_LIB_HAS(library, command_submit)) {
        status = AZ_UNSUPPORTED;
        return 1;
    }
    status = library->command_submit(ID, &request, &result);
    if (status == AZ_OK && result.outcome == AZ_COMMAND_ACCEPTED) pending_request = request.request_id;
    snprintf(lines[3], sizeof lines[3], "Request %llu: outcome %u, reason %d",
             (unsigned long long)result.request_id, result.outcome, result.reason);
    return 1;
}
static int init(const az_core_v1 *c)
{
    if (!AZ_LIB_HAS(library, data_capabilities) || !AZ_LIB_HAS(library, deck_snapshot) ||
        !AZ_LIB_HAS(library, command_poll) || !AZ_LIB_HAS(library, command_release)) return -1;
    core = c;
    snprintf(lines[0], sizeof lines[0], "Cue and loop command results");
    snprintf(lines[4], sizeof lines[4], "Click to EXIT the loop on deck 1, when available");
    return 0;
}

const library_page library_page_control = { init, tick, draw, input };
