/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "azmod-mod.h"
#include "azmod-timestamps.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <time.h>
#include <stdatomic.h>
#define ID "sdk-state-probe"
#ifndef PROBE_MAX_RECORDS
#define PROBE_MAX_RECORDS 512u
#endif
#ifndef PROBE_MAX_TICKS
#define PROBE_MAX_TICKS 1800u
#endif
#ifndef PROBE_NATIVE_ONLY
#define PROBE_NATIVE_ONLY 0
#endif
#ifndef PROBE_CUE_ONLY
#define PROBE_CUE_ONLY 0
#endif
static const az_core_v1 *probe_core;
static const az_library_v1 *probe_library;
static int probe_tempo_fields, probe_native_fields;
#ifndef PROBE_TEMPO_LINE_BYTES
#define PROBE_TEMPO_LINE_BYTES 512u
#endif
struct probe_record {
    int rc, mt, sync, raw;
    uint32_t valid, returned;
    uint64_t mt_unit, mt_rev, mt_ns, sync_unit, sync_rev, sync_ns;
};
struct probe_tempo_record {
    int rc, tempo_valid, slider_valid, slider_locked;
    uint32_t valid, returned;
    double tempo;
    uint64_t tempo_state, tempo_rev, tempo_ns, slider_unit, slider_rev, slider_ns;
};
struct probe_native_record {
    int rc, master_valid, master, load_valid;
    uint32_t valid, returned;
    uint64_t master_rev, master_ns, native_load, load_unit, load_epoch, load_ns, source_load;
    struct probe_tempo_record tempo;
};
static uint64_t probe_now(void) {
    struct timespec time = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &time) || time.tv_sec < 0 || time.tv_nsec < 0 || time.tv_nsec >= 1000000000L) return 0;
    uint64_t seconds = (uint64_t) time.tv_sec;
    if (seconds > (UINT64_MAX - (uint64_t) time.tv_nsec) / UINT64_C(1000000000)) return 0;
    return seconds * UINT64_C(1000000000) + (uint64_t) time.tv_nsec;
}
static _Atomic uint64_t probe_cue_events[4];
static void probe_cue_event(int deck,int what,void *user) {
    (void)user;
    if(deck<0 || deck>=4 || what!=AZ_DECK_CUES_CHANGED)return;
    uint64_t stamp=probe_now();
    if(compare_timestamps(stamp,atomic_load_explicit(&probe_cue_events[deck],memory_order_relaxed),1000000000,1000000000,1)==AZ_TIME_AFTER)
        atomic_store_explicit(&probe_cue_events[deck],stamp,memory_order_release);
}
struct probe_cue_record {
    int rc,available;
    uint32_t valid,returned;
    uint64_t unit,revision,observed_ns,event_ns,source_load;
};
static struct probe_cue_record probe_cue_values(int deck,uint64_t query,int rc,const az_deck_snapshot *s) {
    struct probe_cue_record v={0};v.rc=rc;v.returned=UINT32_MAX;
    v.event_ns=atomic_load_explicit(&probe_cue_events[deck],memory_order_acquire);
    if(compare_timestamps(v.event_ns, query, 1000000000, 1000000000, 1) == AZ_TIME_AFTER)v.event_ns=0; /* Callback may follow this poll's query. */
    if(rc!=AZ_OK || !s || s->size<offsetof(az_deck_snapshot,cue_observed_ns)+sizeof s->cue_observed_ns || s->version!=AZ_DATA_VERSION)return v;
    v.valid=s->valid;v.returned=s->deck;v.source_load=s->load_generation;
    v.unit=s->cue_unit_generation;v.revision=s->cue_revision;v.observed_ns=s->cue_observed_ns;
    v.available=!!((s->valid&AZ_LIVE_CUE_REVISION) && s->deck==(uint32_t)deck && v.unit && v.revision && v.observed_ns && compare_timestamps(v.observed_ns, query, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL);
    return v;
}
static int probe_cue_changed(const struct probe_cue_record *a,const struct probe_cue_record *b) {
    return a->rc != b->rc ||
           a->available != b->available ||
           a->valid != b->valid ||
           a->returned != b->returned ||
           a->unit != b->unit ||
           a->revision != b->revision ||
           a->source_load != b->source_load ||
           compare_timestamps(a->observed_ns,b->observed_ns,1000000000,1000000000,1) != AZ_TIME_EQUAL ||
           compare_timestamps(a->event_ns,b->event_ns,1000000000,1000000000,1) != AZ_TIME_EQUAL;
}
static void *probe_cue_worker(void *unused) {
    (void)unused;unsigned records=0;int seen[4]={0};
    struct probe_cue_record previous[4]={{0}};
    uint64_t started=probe_now();const char *reason="ticks";
    if(!started){reason="clock";goto done;}
    for(unsigned tick=0;tick<PROBE_MAX_TICKS && records<PROBE_MAX_RECORDS;tick++) {
        for(int deck=0;deck<4 && records<PROBE_MAX_RECORDS;deck++) {
            az_deck_snapshot s={.size=sizeof s};int rc=probe_library->deck_snapshot(deck,&s);uint64_t query=probe_now();
            if(!query || compare_timestamps(query, started, 1000000000, 1000000000, 1) == AZ_TIME_BEFORE){reason="clock";goto done;}
            if(compare_timestamps(query-started, UINT64_C(1800000000000), 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL){reason="deadline";goto done;}
            struct probe_cue_record v=probe_cue_values(deck,query,rc,&s);
            if(!seen[deck] || tick%10u==0 || probe_cue_changed(&v,&previous[deck])) {
                char line[512];
                int n=snprintf(line,sizeof line,
                    "SDK_PUBLIC_CUE v=1 seq=%u deck=%d returned=%u query_ns=%llu rc=%d valid=%u cue_valid=%d cue_unit=%llu cue_rev=%llu cue_ns=%llu event_ns=%llu source_load=%llu",
                    records+1,deck+1,v.returned<4?v.returned+1:0,(unsigned long long)query,v.rc,v.valid,v.available,
                    (unsigned long long)v.unit,(unsigned long long)v.revision,(unsigned long long)v.observed_ns,
                    (unsigned long long)v.event_ns,(unsigned long long)v.source_load);
                if(n<0 || (size_t)n>=sizeof line){reason="format_limit";goto done;}
                probe_core->log(ID,"%s",line);records++;previous[deck]=v;seen[deck]=1;
            }
        }
        if(records<PROBE_MAX_RECORDS && tick+1<PROBE_MAX_TICKS) {
            const struct timespec delay={1,0};
            if(nanosleep(&delay,NULL) && errno!=EINTR){reason="sleep";goto done;}
        }
    }
    if(records==PROBE_MAX_RECORDS)reason="record_limit";
done:
    probe_core->log(ID,"SDK_PUBLIC_CUE complete reason=%s records=%u",reason,records);return NULL;
}
static struct probe_record probe_values(int rc, const az_deck_snapshot *s) {
    struct probe_record value = {0};
    value.rc = rc;
    value.mt = value.sync = value.raw = -1;
    if (rc == AZ_OK) {
        value.valid = s->valid;
        value.returned = s->deck;
        if (s->valid & AZ_LIVE_MASTER_TEMPO) value.mt = s->master_tempo;
        if (s->valid & AZ_LIVE_SYNC) value.sync = s->sync;
        value.mt_unit = s->live_unit_generation;
        value.mt_rev = s->key_lock_revision;
        value.mt_ns = s->key_lock_observed_ns;
        value.sync_unit = s->sync_unit_generation;
        value.sync_rev = s->sync_revision;
        value.sync_ns = s->sync_observed_ns;
        if (value.sync_unit && value.sync_rev && value.sync_ns) value.raw = s->sync_raw_mode;
    }
    return value;
}
static int probe_changed(const struct probe_record *a, const struct probe_record *b) {
    return a->rc != b->rc || a->valid != b->valid || a->returned != b->returned ||
           a->mt != b->mt || a->sync != b->sync || a->raw != b->raw ||
           a->mt_unit != b->mt_unit || a->mt_rev != b->mt_rev || compare_timestamps(a->mt_ns, b->mt_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL ||
           a->sync_unit != b->sync_unit || a->sync_rev != b->sync_rev || compare_timestamps(a->sync_ns, b->sync_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL;
}
static int probe_emit(unsigned seq, int deck, uint64_t query_ns, const struct probe_record *v) {
    char line[384];
    int n = snprintf(line, sizeof line,
                     "SDK_PUBLIC_STATE v=1 seq=%u deck=%d returned=%u query_ns=%llu rc=%d valid=%u mt=%d mt_unit=%llu mt_rev=%llu mt_ns=%llu sync=%d sync_unit=%llu sync_rev=%llu sync_ns=%llu raw=%d",
                     seq, deck + 1, v->returned + 1, (unsigned long long) query_ns, v->rc, v->valid, v->mt,
                     (unsigned long long) v->mt_unit, (unsigned long long) v->mt_rev, (unsigned long long) v->mt_ns,
                     v->sync, (unsigned long long) v->sync_unit, (unsigned long long) v->sync_rev, (unsigned long long) v->sync_ns, v->raw);
    if (n < 0 || (size_t) n >= sizeof line) return 0;
    probe_core->log(ID, "%s", line);
    return 1;
}
static struct probe_tempo_record probe_tempo_values(int deck, uint64_t query_ns, int rc, const az_deck_snapshot *s) {
    struct probe_tempo_record value = {0};
    value.rc = rc;
    value.returned = UINT32_MAX;
    value.slider_locked = -1;
    if (rc != AZ_OK || !s || s->size < offsetof(az_deck_snapshot, deck) + sizeof s->deck ||
        s->version != AZ_DATA_VERSION) return value;
    value.valid = s->valid;
    value.returned = s->deck;
    if (s->size >= offsetof(az_deck_snapshot, tempo_observed_ns) + sizeof s->tempo_observed_ns) {
        value.tempo_state = s->tempo_state_generation;
        value.tempo_rev = s->tempo_revision;
        value.tempo_ns = s->tempo_observed_ns;
        if ((s->valid & AZ_LIVE_TEMPO) && s->deck == (uint32_t) deck && query_ns &&
            value.tempo_state && value.tempo_rev && value.tempo_ns && compare_timestamps(value.tempo_ns, query_ns, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL &&
            isfinite(s->tempo) && s->tempo > 20.0 && s->tempo < 400.0) {
            value.tempo_valid = 1;
            value.tempo = s->tempo;
        }
    }
    if (s->size >= offsetof(az_deck_snapshot, tempo_slider_locked) + sizeof s->tempo_slider_locked) {
        value.slider_unit = s->tempo_lock_unit_generation;
        value.slider_rev = s->tempo_lock_revision;
        value.slider_ns = s->tempo_lock_observed_ns;
        if ((s->valid & AZ_LIVE_TEMPO_SLIDER_LOCKED) && s->deck == (uint32_t) deck && query_ns &&
            value.slider_unit && value.slider_rev && value.slider_ns && compare_timestamps(value.slider_ns, query_ns, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL &&
            (s->tempo_slider_locked == 0 || s->tempo_slider_locked == 1)) {
            value.slider_valid = 1;
            value.slider_locked = s->tempo_slider_locked;
        }
    }
    return value;
}
static int probe_tempo_changed(const struct probe_tempo_record *a, const struct probe_tempo_record *b) {
    /* GUI BPM poll receipts advance even when the value is unchanged. Log the
     * newest clocks periodically, without spending the budget on receipt-only bumps. */
    const uint32_t fields = AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED;
    return a->rc != b->rc || (a->valid & fields) != (b->valid & fields) || a->returned != b->returned ||
           a->tempo_valid != b->tempo_valid || a->tempo != b->tempo || a->tempo_state != b->tempo_state ||
           a->slider_valid != b->slider_valid || a->slider_locked != b->slider_locked || a->slider_unit != b->slider_unit;
}
static int probe_tempo_emit(unsigned seq, int deck, uint64_t query_ns, const struct probe_tempo_record *v) {
    char line[PROBE_TEMPO_LINE_BYTES];
    /* Qualified BPM is0 or20..400. Integer formatting keeps the decimal point
     * stable without changing the host process's numeric locale. */
    uint32_t decimal = (uint32_t) (v->tempo * 1000000.0 + 0.5);
    int n = snprintf(line, sizeof line,
                     "SDK_PUBLIC_TEMPO v=1 seq=%u deck=%d returned=%u query_ns=%llu rc=%d valid=%u tempo_valid=%d tempo=%u.%06u tempo_state=%llu tempo_rev=%llu tempo_ns=%llu slider_valid=%d slider_locked=%d slider_unit=%llu slider_rev=%llu slider_ns=%llu",
                     seq, deck + 1, v->returned < 4 ? v->returned + 1 : 0, (unsigned long long) query_ns,
                     v->rc, v->valid, v->tempo_valid, decimal / 1000000u, decimal % 1000000u, (unsigned long long) v->tempo_state,
                     (unsigned long long) v->tempo_rev, (unsigned long long) v->tempo_ns,
                     v->slider_valid, v->slider_locked, (unsigned long long) v->slider_unit,
                     (unsigned long long) v->slider_rev, (unsigned long long) v->slider_ns);
    if (n < 0 || (size_t) n >= sizeof line) return 0;
    probe_core->log(ID, "%s", line);
    return 1;
}
static struct probe_native_record probe_native_values(int deck, uint64_t query_ns, int rc, const az_deck_snapshot *s) {
    struct probe_native_record v = {0};
    v.rc = rc; v.returned = UINT32_MAX; v.master = -1;
    v.tempo = probe_tempo_values(deck,query_ns,rc,s);
    if (rc != AZ_OK || !s || s->size < offsetof(az_deck_snapshot, deck) + sizeof s->deck || s->version != AZ_DATA_VERSION) return v;
    v.valid = s->valid; v.returned = s->deck;
    if (s->size >= offsetof(az_deck_snapshot, load_generation) + sizeof s->load_generation) v.source_load = s->load_generation;
    if (s->size >= offsetof(az_deck_snapshot, master_observed_ns) + sizeof s->master_observed_ns) {
        v.master_rev = s->master_revision; v.master_ns = s->master_observed_ns;
        if ((s->valid & AZ_LIVE_MASTER) && s->deck == (uint32_t) deck && query_ns &&
            v.master_rev && v.master_ns && compare_timestamps(v.master_ns, query_ns, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL && (s->master == 0 || s->master == 1)) {
            v.master_valid = 1; v.master = s->master;
        }
    }
    if (s->size >= offsetof(az_deck_snapshot, native_load_observed_ns) + sizeof s->native_load_observed_ns) {
        v.load_unit = s->native_load_unit_generation; v.load_epoch = s->native_load_invalidation_epoch; v.load_ns = s->native_load_observed_ns;
        if ((s->valid & AZ_LIVE_NATIVE_LOAD) && s->deck == (uint32_t) deck && query_ns &&
            s->native_load_generation && v.load_unit && v.load_epoch && v.load_ns && compare_timestamps(v.load_ns, query_ns, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL) {
            v.load_valid = 1; v.native_load = s->native_load_generation;
        }
    }
    return v;
}
static int probe_native_changed(const struct probe_native_record *a, const struct probe_native_record *b) {
    const uint32_t fields = AZ_LIVE_MASTER | AZ_LIVE_NATIVE_LOAD | AZ_LIVE_TRACK;
    return a->rc != b->rc || (a->valid & fields) != (b->valid & fields) || a->returned != b->returned ||
           a->master_valid != b->master_valid || a->master != b->master || a->master_rev != b->master_rev || compare_timestamps(a->master_ns, b->master_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL ||
           a->load_valid != b->load_valid || a->native_load != b->native_load || a->load_unit != b->load_unit ||
           a->load_epoch != b->load_epoch || compare_timestamps(a->load_ns, b->load_ns, 1000000000, 1000000000, 1) != AZ_TIME_EQUAL || a->source_load != b->source_load || probe_tempo_changed(&a->tempo,&b->tempo);
}
static int probe_native_emit(unsigned seq, int deck, uint64_t query_ns, const struct probe_native_record *v) {
    char line[768];
    uint32_t decimal=(uint32_t)(v->tempo.tempo*1000000.0+0.5);
    int n = snprintf(line, sizeof line,
        "SDK_PUBLIC_NATIVE v=1 seq=%u deck=%d returned=%u query_ns=%llu rc=%d valid=%u master_valid=%d master=%d master_rev=%llu master_ns=%llu load_valid=%d native_load=%llu load_unit=%llu load_epoch=%llu load_ns=%llu source_load=%llu tempo_valid=%d tempo=%u.%06u tempo_state=%llu tempo_rev=%llu tempo_ns=%llu slider_valid=%d slider_locked=%d slider_unit=%llu slider_rev=%llu slider_ns=%llu",
        seq, deck + 1, v->returned < 4 ? v->returned + 1 : 0, (unsigned long long) query_ns, v->rc, v->valid,
        v->master_valid, v->master, (unsigned long long) v->master_rev, (unsigned long long) v->master_ns,
        v->load_valid, (unsigned long long) v->native_load, (unsigned long long) v->load_unit,
        (unsigned long long) v->load_epoch, (unsigned long long) v->load_ns, (unsigned long long) v->source_load,
        v->tempo.tempo_valid,decimal/1000000u,decimal%1000000u,(unsigned long long)v->tempo.tempo_state,
        (unsigned long long)v->tempo.tempo_rev,(unsigned long long)v->tempo.tempo_ns,v->tempo.slider_valid,v->tempo.slider_locked,
        (unsigned long long)v->tempo.slider_unit,(unsigned long long)v->tempo.slider_rev,(unsigned long long)v->tempo.slider_ns);
    if (n < 0 || (size_t) n >= sizeof line) return 0;
    probe_core->log(ID, "%s", line); return 1;
}
static void *probe_worker(void *unused) {
    (void) unused;
    unsigned records = 0, state_records = 0, tempo_records = 0, native_records = 0;
    int tempo_fields = probe_tempo_fields, native_fields = probe_native_fields;
    uint64_t started = probe_now();
    const char *reason = "ticks";
    struct probe_record previous[4] = {{0}};
    struct probe_tempo_record previous_tempo[4] = {{0}};
    int seen[4] = {0};
    int tempo_seen[4] = {0};
    struct probe_native_record previous_native[4] = {{0}};
    int native_seen[4] = {0};
    if (!started) { reason = "clock"; goto done; }
    for (unsigned tick = 0; tick < PROBE_MAX_TICKS && records < PROBE_MAX_RECORDS; ++tick) {
        for (int deck = 0; deck < 4 && records < PROBE_MAX_RECORDS; ++deck) {
            az_deck_snapshot snapshot = {0};
            snapshot.size = sizeof snapshot;
            int rc = probe_library->deck_snapshot(deck, &snapshot);
            uint64_t query_ns = probe_now();
            if (!query_ns || compare_timestamps(query_ns, started, 1000000000, 1000000000, 1) == AZ_TIME_BEFORE) { reason = "clock"; goto done; }
            if (compare_timestamps(query_ns - started, UINT64_C(1800000000000), 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL) { reason = "deadline"; goto done; }
            struct probe_record value = probe_values(rc, &snapshot);
            if (!PROBE_NATIVE_ONLY && (!seen[deck] || tick % 10u == 0 || probe_changed(&value, &previous[deck]))) {
                if (!probe_emit(state_records + 1, deck, query_ns, &value)) { reason = "format_limit"; goto done; }
                records++;
                state_records++;
                previous[deck] = value;
                seen[deck] = 1;
            }
            struct probe_tempo_record tempo = probe_tempo_values(deck, query_ns, rc, &snapshot);
            if ((tempo.valid & (AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED)) || tempo.tempo_rev || tempo.slider_rev)
                tempo_fields = 1;
            if (!PROBE_NATIVE_ONLY && tempo_fields && records < PROBE_MAX_RECORDS &&
                (!tempo_seen[deck] || tick % 10u == 0 || probe_tempo_changed(&tempo, &previous_tempo[deck]))) {
                if (!probe_tempo_emit(tempo_records + 1, deck, query_ns, &tempo)) { reason = "format_limit"; goto done; }
                records++;
                tempo_records++;
                previous_tempo[deck] = tempo;
                tempo_seen[deck] = 1;
            }
            struct probe_native_record native = probe_native_values(deck, query_ns, rc, &snapshot);
            if ((native.valid & (AZ_LIVE_MASTER | AZ_LIVE_NATIVE_LOAD)) || native.master_rev || native.load_ns) native_fields = 1;
            if (native_fields && records < PROBE_MAX_RECORDS &&
                (!native_seen[deck] || tick % 10u == 0 || probe_native_changed(&native, &previous_native[deck]))) {
                if (!probe_native_emit(native_records + 1, deck, query_ns, &native)) { reason = "format_limit"; goto done; }
                records++; native_records++; previous_native[deck] = native; native_seen[deck] = 1;
            }
        }
        if (records < PROBE_MAX_RECORDS && tick + 1 < PROBE_MAX_TICKS) {
            const struct timespec delay = {1, 0};
            if (nanosleep(&delay, NULL) && errno != EINTR) { reason = "sleep"; goto done; }
        }
    }
    if (records == PROBE_MAX_RECORDS) reason = "record_limit";
done:
    probe_core->log(ID, "SDK_PUBLIC_STATE complete reason=%s records=%u", reason, state_records);
    if (tempo_fields) probe_core->log(ID, "SDK_PUBLIC_TEMPO complete reason=%s records=%u", reason, tempo_records);
    if (native_fields) probe_core->log(ID, "SDK_PUBLIC_NATIVE complete reason=%s records=%u", reason, native_records);
    return NULL;
}
static int init(const az_core_v1 *core) {
    if (!core || core->abi != AZ_MOD_ABI || !AZ_HAS(core, library_service) || !AZ_HAS(core, thread_start) || !AZ_HAS(core, log)) return -1;
    const az_library_v1 *shared = core->library_service(AZ_LIBRARY_ABI);
    if (!AZ_LIB_HAS(shared, data_capabilities) || !AZ_LIB_HAS(shared, deck_snapshot)) return -1;
    az_data_capabilities caps = {.size = sizeof caps};
    if (shared->data_capabilities(&caps) != AZ_OK) return -1;
    probe_core = core;
    probe_library = shared;
    probe_tempo_fields = !!(caps.live_fields & (AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED));
    probe_native_fields = !!(caps.live_fields & (AZ_LIVE_MASTER | AZ_LIVE_NATIVE_LOAD));
    /* live_fields reports current receipt availability. Empty units must be
     * allowed to subscribe before the first genuine cue change arrives. */
    if(PROBE_CUE_ONLY) {
        if(!AZ_HAS(core,on_deck_event)){core->log(ID,"SDK_PUBLIC_CUE listener unsupported");return -1;}
        int listener_rc=core->on_deck_event(ID,probe_cue_event,NULL);
        core->log(ID,"SDK_PUBLIC_CUE listener rc=%d current_fields=%u",listener_rc,caps.live_fields);
        if(listener_rc!=0)return -1;
    }
    core->log(ID, "SDK_PUBLIC_STATE capabilities version=%u fields=%u commands=%llu", caps.version, caps.live_fields, (unsigned long long) caps.commands);
    return core->thread_start(ID, "sdk-public-state", PROBE_CUE_ONLY?probe_cue_worker:probe_worker, NULL) == 0 ? 0 : -1;
}
static const char *const tags[] = {"any", NULL};
static const az_mod_v1 mod = {.abi = AZ_MOD_ABI, .size = sizeof(az_mod_v1), .id = ID, .fw_tags = tags, .init = init};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
