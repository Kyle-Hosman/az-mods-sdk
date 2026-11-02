/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "azmod-mod.h"
#include "azmod-timestamps.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ID "sdk-wave-layer-probe"
/* Explicit USB1 selection. Never infer a source from a deck or retry controls. */
enum { PROBE_SOURCE_PORT = 0, PROBE_COLUMNS = 64, PROBE_POLLS = 24,
       PROBE_CALLS = 256, PROBE_LINES = 48, PROBE_SAMPLE_BYTES = 18 };
#define PROBE_DEADLINE_NS UINT64_C(30000000000)
#define PROBE_STOP 999
static const az_core_v1 *probe_core;
struct probe {
    const az_library_v1 *library;
    uint64_t started, query_ns;
    unsigned calls, lines, requests, releases, owned;
    const char *status, *reason;
};
static uint64_t probe_now(void) {
    struct timespec t = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &t) || t.tv_sec < 0 || t.tv_nsec < 0 || t.tv_nsec >= 1000000000L) return 0;
    uint64_t s = (uint64_t)t.tv_sec;
    if (s > (UINT64_MAX - (uint64_t)t.tv_nsec) / UINT64_C(1000000000)) return 0;
    return s * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void stop(struct probe *p, const char *status, const char *reason) {
    if (strcmp(p->status, "FAIL")) { p->status = status; p->reason = reason; }
}
static int pause_probe(struct probe *p) {
    const struct timespec delay = {0, 50000000};
    if (nanosleep(&delay, NULL) && errno != EINTR) { stop(p, "INCOMPLETE", "sleep"); return 0; }
    return 1;
}
static int allow(struct probe *p) {
    uint64_t now = probe_now();
    if (!now || compare_timestamps(now,p->started,1000000000,1000000000,1) == AZ_TIME_BEFORE || compare_timestamps(now,p->query_ns,1000000000,1000000000,1) == AZ_TIME_BEFORE) { stop(p, "INCOMPLETE", "clock"); return 0; }
    p->query_ns = now;
    if (compare_timestamps(now - p->started,PROBE_DEADLINE_NS,1000000000,1000000000,1) >= AZ_TIME_EQUAL) { stop(p, "INCOMPLETE", "deadline"); return 0; }
    if (p->calls >= PROBE_CALLS) { stop(p, "INCOMPLETE", "call_budget"); return 0; }
    p->calls++; return 1;
}
static int emit(struct probe *p, const char *event, const char *format, ...) {
    char detail[576], line[768];
    if (p->lines >= PROBE_LINES - 1) { stop(p, "INCOMPLETE", "log_budget"); return 0; }
    va_list a; va_start(a, format); int n = vsnprintf(detail, sizeof detail, format, a); va_end(a);
    if (n < 0 || (size_t)n >= sizeof detail) { stop(p, "INCOMPLETE", "format"); return 0; }
    n = snprintf(line, sizeof line, "SDK_PUBLIC_WAVE v=1 seq=%u port=%d event=%s %s",
                 p->lines + 1, PROBE_SOURCE_PORT, event, detail);
    if (n < 0 || (size_t)n >= sizeof line) { stop(p, "INCOMPLETE", "format"); return 0; }
    probe_core->log(ID, "%s", line); p->lines++; return 1;
}
static int same_ref(const az_track_ref *a, const az_track_ref *b) {
    return a->size >= sizeof *a && a->version == AZ_DATA_VERSION && !a->reserved &&
           a->source_id == b->source_id && a->source_generation == b->source_generation &&
           a->library_id == b->library_id && a->track_id == b->track_id;
}
static int choose(const az_track *t, void *user) {
    uint32_t *selected = user;
    if (t && t->size >= (int)(offsetof(az_track, id) + sizeof t->id) && t->id) *selected = t->id;
    return 1; /* First public callback only; copy no strings. */
}
static void release_ticket(struct probe *p, uint64_t *ticket) {
    if (!*ticket) return;
    int rc = AZ_BUSY;
    /* Cleanup has its own finite reserve, even after the read deadline/budget. */
    for (unsigned i = 0; i < PROBE_POLLS; ++i) {
        p->calls++; rc = p->library->track_release(ID, *ticket);
        if (rc != AZ_BUSY) break;
        if (i + 1 < PROBE_POLLS) (void)pause_probe(p);
    }
    (void)emit(p, "release", "ticket=%llu rc=%d", (unsigned long long)*ticket, rc);
    if (rc == AZ_OK) { p->releases++; p->owned--; *ticket = 0; }
    else stop(p, "INCOMPLETE", "release");
}
static int get_wave(struct probe *p, const az_track_ref *ref, const az_wave_query *q,
                    uint64_t *ticket, const az_wave_snapshot **view) {
    int rc = AZ_BUSY;
    *ticket = 0; *view = NULL;
    for (unsigned i = 0; i < PROBE_POLLS; ++i) {
        if (!allow(p)) return PROBE_STOP;
        rc = p->library->waveform_request(ID, ref, q, ticket);
        if (rc != AZ_BUSY) break;
        if (*ticket) { *ticket = 0; stop(p, "FAIL", "error_ticket"); return PROBE_STOP; }
        if (i + 1 < PROBE_POLLS && !pause_probe(p)) return PROBE_STOP;
    }
    if (rc == AZ_OK) {
        if (!*ticket) { stop(p, "FAIL", "zero_ticket"); return PROBE_STOP; }
        p->requests++; p->owned++;
        /* Pending work has no24-poll delivery guarantee. Keep the existing
         * overall call/deadline bounds while the core worker owns the job. */
        for (unsigned i = 0; i < PROBE_CALLS; ++i) {
            if (!allow(p)) return PROBE_STOP;
            *view = NULL; rc = p->library->waveform_acquire(ID, *ticket, view);
            if (rc != AZ_PENDING && rc != AZ_BUSY) break;
            if (*view) { stop(p, "FAIL", "pending_view"); return PROBE_STOP; }
            if (i + 1 < PROBE_CALLS && !pause_probe(p)) return PROBE_STOP;
        }
    } else if (*ticket) { *ticket = 0; stop(p, "FAIL", "error_ticket"); return PROBE_STOP; }
    (void)emit(p, "query", "query_ns=%llu ticket=%llu encoding=%u first=%u requested=%u rc=%d source=%llu generation=%llu library=%llu track=%u",
               (unsigned long long)p->query_ns, (unsigned long long)*ticket, q->encoding, q->first_column, q->column_count, rc,
               (unsigned long long)ref->source_id, (unsigned long long)ref->source_generation,
               (unsigned long long)ref->library_id, ref->track_id);
    if (rc == AZ_BUSY || rc == AZ_PENDING) { stop(p, "INCOMPLETE", "timeout"); return PROBE_STOP; }
    if (rc != AZ_OK && *view) { stop(p, "FAIL", "error_view"); return PROBE_STOP; }
    return rc;
}
static int geometry(struct probe *p, const az_track_ref *ref, const az_wave_query *q,
                    const az_wave_snapshot *v, uint32_t total) {
    if (!v || v->size < sizeof *v || v->version != AZ_DATA_VERSION || !same_ref(&v->track, ref)) goto bad;
    if (!v->total_columns || (total && v->total_columns != total) || q->first_column >= v->total_columns) goto bad;
    uint32_t count = v->total_columns - q->first_column;
    if (count > q->column_count) count = q->column_count;
    unsigned stride = q->encoding == AZ_WAVE_PWV7_BANDS ? 3u : 6u;
    if (v->encoding != q->encoding || v->first_column != q->first_column || v->column_count != count ||
        count > PROBE_COLUMNS || v->bytes_per_column != stride || v->data_bytes != count * stride || !v->columns ||
        v->info.availability != AZ_AVAIL_READY ||
        v->info.provenance != (stride == 3 ? AZ_PROV_FILE : AZ_PROV_DERIVED) ||
        !v->revision || !v->observed_ns || v->reserved[0] || v->reserved[1] ||
        v->start_s != q->first_column / 150.0 || v->seconds_per_column != 1.0 / 150.0) goto bad;
    return 1;
bad:
    stop(p, "FAIL", "geometry_ref_provenance"); return 0;
}
/* Inverse contract: reconstruct each input byte from layer membership. No
 * sorting, encoder invocation or forward conversion is used as an oracle. */
static int inverse(const uint8_t raw[3], const uint8_t layered[6]) {
    unsigned header = layered[0] | (unsigned)layered[1] << 8;
    unsigned count = header >> 12, previous_mask = 0, previous_level = 256;
    uint8_t recovered[3] = {0};
    if (count < 1 || count > 3 || layered[5] != 0) return 0;
    if (!raw[0] && !raw[1] && !raw[2])
        return header == 0x1008 && !layered[2] && !layered[3] && !layered[4];
    for (unsigned i = 0; i < 3; ++i) {
        unsigned id = (header >> (4 * i)) & 15, level = layered[2 + i], mask;
        if (i >= count) { if (id || level) return 0; continue; }
        switch (id) {
            case 1: mask = 4; break; case 2: mask = 2; break; case 3: mask = 1; break;
            case 4: mask = 6; break; case 5: mask = 5; break; case 6: mask = 3; break;
            case 8: mask = 7; break; default: return 0;
        }
        if (!level || level >= previous_level || mask == previous_mask || (mask & previous_mask) != previous_mask) return 0;
        for (unsigned channel = 0; channel < 3; ++channel)
            if ((mask & (1u << channel)) && !recovered[channel]) recovered[channel] = (uint8_t)level;
        previous_mask = mask; previous_level = level;
    }
    return !memcmp(recovered, raw, 3);
}
static uint64_t hash_bytes(const uint8_t *data, uint32_t count) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint32_t i = 0; i < count; ++i) { hash ^= data[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}
static void hex_sample(const uint8_t *data, uint32_t bytes, char out[PROBE_SAMPLE_BYTES * 2 + 1]) {
    static const char hex[] = "0123456789abcdef";
    if (bytes > PROBE_SAMPLE_BYTES) bytes = PROBE_SAMPLE_BYTES;
    for (uint32_t i = 0; i < bytes; ++i) { out[2*i] = hex[data[i] >> 4]; out[2*i+1] = hex[data[i] & 15]; }
    out[bytes * 2] = 0;
}
static void view_log(struct probe *p, uint64_t ticket, const az_wave_snapshot *v) {
    (void)emit(p, "view", "ticket=%llu encoding=%u first=%u count=%u total=%u stride=%u bytes=%u availability=%u provenance=%u revision=%llu observed_ns=%llu source=%llu generation=%llu library=%llu track=%u start_s=%.9f step_s=%.12f",
               (unsigned long long)ticket, v->encoding, v->first_column, v->column_count, v->total_columns,
               v->bytes_per_column, v->data_bytes, v->info.availability, v->info.provenance,
               (unsigned long long)v->revision, (unsigned long long)v->observed_ns,
               (unsigned long long)v->track.source_id, (unsigned long long)v->track.source_generation,
               (unsigned long long)v->track.library_id, v->track.track_id, v->start_s, v->seconds_per_column);
}
static int pair(struct probe *p, const az_track_ref *ref, uint32_t total, uint32_t first, uint32_t requested) {
    uint64_t raw_ticket = 0, layer_ticket = 0;
    const az_wave_snapshot *raw = NULL, *layer = NULL;
    az_wave_query q = {sizeof q, AZ_DATA_VERSION, AZ_WAVE_PWV7_BANDS, first, requested, 0};
    int okay = 0, rc = get_wave(p, ref, &q, &raw_ticket, &raw);
    if (rc == PROBE_STOP) goto done;
    if (rc != AZ_OK) { stop(p, "INCOMPLETE", "raw_unavailable"); goto done; }
    if (!geometry(p, ref, &q, raw, total)) goto done;
    q.encoding = AZ_WAVE_PWV7_LAYERED_RAW;
    rc = get_wave(p, ref, &q, &layer_ticket, &layer);
    if (rc == PROBE_STOP) goto done;
    if (rc != AZ_OK) { stop(p, "INCOMPLETE", "layered_unavailable"); goto done; }
    if (!geometry(p, ref, &q, layer, total)) goto done;
    for (uint32_t i = 0; i < raw->column_count; ++i)
        if (!inverse(raw->columns + i*3, layer->columns + i*6)) { stop(p, "FAIL", "payload_inverse"); goto done; }
    view_log(p, raw_ticket, raw); view_log(p, layer_ticket, layer);
    char raw_hex[PROBE_SAMPLE_BYTES*2+1], layer_hex[PROBE_SAMPLE_BYTES*2+1];
    hex_sample(raw->columns, raw->data_bytes, raw_hex); hex_sample(layer->columns, layer->data_bytes, layer_hex);
    okay = emit(p, "payload", "first=%u count=%u raw_ticket=%llu layered_ticket=%llu raw_fnv64=%016llx layered_fnv64=%016llx raw_hex=%s layered_hex=%s",
                first, raw->column_count, (unsigned long long)raw_ticket, (unsigned long long)layer_ticket,
                (unsigned long long)hash_bytes(raw->columns, raw->data_bytes),
                (unsigned long long)hash_bytes(layer->columns, layer->data_bytes), raw_hex, layer_hex);
done:
    release_ticket(p, &layer_ticket); release_ticket(p, &raw_ticket);
    return okay && !strcmp(p->status, "PASS");
}
static int refusal(struct probe *p, const az_track_ref *ref, uint32_t encoding, uint32_t first, uint32_t count, int expected) {
    az_wave_query q = {sizeof q, AZ_DATA_VERSION, encoding, first, count, 0};
    uint64_t ticket = 0; const az_wave_snapshot *view = NULL;
    int rc = get_wave(p, ref, &q, &ticket, &view);
    if (rc != PROBE_STOP && rc != expected) stop(p, "FAIL", "refusal_status");
    release_ticket(p, &ticket);
    return rc == expected && !strcmp(p->status, "PASS");
}
static void *probe_worker(void *unused) {
    (void)unused;
    struct probe p = {.status="PASS", .reason="verified"};
    uint64_t ticket = 0; const az_wave_snapshot *view = NULL;
    az_track_ref ref = {.size=sizeof ref}; uint32_t selected = 0, total = 0;
    p.started = p.query_ns = probe_now();
    if (!p.started) { stop(&p, "INCOMPLETE", "clock"); goto done; }
    p.library = probe_core->library_service(AZ_LIBRARY_ABI);
    if (!AZ_LIB_HAS(p.library, lib_tracks) || !AZ_LIB_HAS(p.library, track_resolve) ||
        !AZ_LIB_HAS(p.library, waveform_request) || !AZ_LIB_HAS(p.library, waveform_acquire) || !AZ_LIB_HAS(p.library, track_release)) {
        stop(&p, "INCOMPLETE", "service"); goto done;
    }
    if (!emit(&p, "start", "window=%u max_polls=%u max_calls=%u deadline_ns=%llu", PROBE_COLUMNS, PROBE_CALLS, PROBE_CALLS, (unsigned long long)PROBE_DEADLINE_NS)) goto done;
    if (!allow(&p)) goto done;
    int rc = p.library->lib_tracks(ID, PROBE_SOURCE_PORT, 0, choose, &selected);
    if (rc < 0 || !selected) { stop(&p, "INCOMPLETE", "first_public_track"); goto done; }
    for (unsigned i = 0; i < PROBE_POLLS; ++i) {
        if (!allow(&p)) goto done;
        ref = (az_track_ref){.size=sizeof ref};
        rc = p.library->track_resolve(ID, PROBE_SOURCE_PORT, selected, &ref);
        if (rc != AZ_BUSY) break;
        if (i + 1 < PROBE_POLLS && !pause_probe(&p)) goto done;
    }
    if (rc != AZ_OK) { stop(&p, "INCOMPLETE", "resolve"); goto done; }
    if (ref.version != AZ_DATA_VERSION || ref.size < sizeof ref || !ref.source_id || !ref.source_generation || !ref.library_id || ref.track_id != selected || ref.reserved) {
        stop(&p, "FAIL", "resolved_ref"); goto done;
    }
    if (!emit(&p, "resolve", "source=%llu generation=%llu library=%llu track=%u",
              (unsigned long long)ref.source_id, (unsigned long long)ref.source_generation, (unsigned long long)ref.library_id, ref.track_id)) goto done;
    az_wave_query initial = {sizeof initial, AZ_DATA_VERSION, AZ_WAVE_PWV7_BANDS, 0, 1, 0};
    rc = get_wave(&p, &ref, &initial, &ticket, &view);
    if (rc == PROBE_STOP) goto done;
    if (rc != AZ_OK) { stop(&p, "INCOMPLETE", "initial_raw"); goto done; }
    if (view && view->size >= sizeof *view && (!view->total_columns || view->info.availability == AZ_AVAIL_EMPTY)) { stop(&p, "INCOMPLETE", "empty_waveform"); goto done; }
    if (!geometry(&p, &ref, &initial, view, 0)) goto done;
    total = view->total_columns; view_log(&p, ticket, view); release_ticket(&p, &ticket);
    if (strcmp(p.status, "PASS")) goto done;
    if (!pair(&p, &ref, total, total/2, PROBE_COLUMNS) || !pair(&p, &ref, total, total-1, AZ_WAVE_WINDOW_MAX)) goto done;
    const uint32_t formats[] = {AZ_WAVE_PWV7_BANDS, AZ_WAVE_PWV7_LAYERED_RAW};
    for (unsigned i = 0; i < sizeof formats / sizeof formats[0]; ++i) {
        uint32_t encoding = formats[i];
        if (!refusal(&p, &ref, encoding, 0, 0, AZ_INVALID) ||
            !refusal(&p, &ref, encoding, 0, AZ_WAVE_WINDOW_MAX+1, AZ_LIMIT) ||
            !refusal(&p, &ref, encoding, total, 1, AZ_INVALID)) goto done;
    }
    (void)refusal(&p, &ref, 99, 0, 1, AZ_UNSUPPORTED);
done:
    if (p.library && ticket) release_ticket(&p, &ticket);
    probe_core->log(ID, "SDK_PUBLIC_WAVE v=1 seq=%u port=%d event=final status=%s reason=%s calls=%u accepted=%u released=%u outstanding=%u",
                    p.lines+1, PROBE_SOURCE_PORT, p.status, p.reason, p.calls, p.requests, p.releases, p.owned);
    return NULL;
}
static int init(const az_core_v1 *core) {
    if (!core || core->abi != AZ_MOD_ABI || !AZ_HAS(core, log) || !AZ_HAS(core, thread_start) || !AZ_HAS(core, library_service)) return -1;
    probe_core = core;
    if (core->thread_start(ID, "sdk-public-wave", probe_worker, NULL) == 0) return 0;
    core->log(ID, "SDK_PUBLIC_WAVE v=1 seq=1 port=0 event=final status=INCOMPLETE reason=thread_start calls=0 accepted=0 released=0 outstanding=0");
    return -1;
}
static const char *const tags[] = {"any", NULL};
static const az_mod_v1 mod = {.abi=AZ_MOD_ABI, .size=sizeof mod, .id=ID, .fw_tags=tags, .init=init};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
