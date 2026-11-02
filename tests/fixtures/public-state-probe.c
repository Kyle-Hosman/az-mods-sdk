/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <limits.h>
static unsigned snapshots, starts, sleeps, logs, clock_calls, fail_clock_call;
static int start_failure, busy_deck = -1;
static int tempo_capabilities;
static int native_capabilities;
static int stable_state_receipts;
static uint64_t now_ns = UINT64_C(1000000000);
#ifndef PROBE_CAPTURE_LINES
#define PROBE_CAPTURE_LINES 24u
#endif
static char lines[PROBE_CAPTURE_LINES][768];
static int native_delayed;
static uint64_t delayed_load_ns;
static void *(*worker_fn)(void *);
static void *worker_arg;
static int __attribute__((unused)) probe_clock(clockid_t clock, struct timespec *out) {
    assert(clock == CLOCK_MONOTONIC);
    int failed = ++clock_calls == fail_clock_call;
    now_ns += 1000;
    out->tv_sec = (time_t) (now_ns / UINT64_C(1000000000));
    out->tv_nsec = (long) (now_ns % UINT64_C(1000000000)); return failed ? -1 : 0;
}
static int __attribute__((unused)) probe_sleep(const struct timespec *in, struct timespec *out) {
    (void) out; assert(in->tv_sec == 1 && in->tv_nsec == 0);
    sleeps++; now_ns += UINT64_C(1000000000); return 0;
}
static int snapshot(int deck, az_deck_snapshot *out) {
    assert(deck >= 0 && deck < 4 && out->size == sizeof *out);
    snapshots++; out->version = AZ_DATA_VERSION; out->deck = (uint32_t) deck;
    out->valid = deck == 0 ? AZ_LIVE_MASTER_TEMPO : deck == 1 ? AZ_LIVE_SYNC : deck == 2 ? AZ_LIVE_MASTER_TEMPO | AZ_LIVE_SYNC : 0;
    out->master_tempo = deck == 0 ? 1 : deck == 2 ? 0 : 99;
    out->sync = deck == 1 ? 1 : deck == 2 ? 0 : 99;
    out->live_unit_generation = 100u + (unsigned) deck;
    out->key_lock_revision = snapshots; out->key_lock_observed_ns = now_ns;
    out->sync_unit_generation = 200u + (unsigned) deck;
    out->sync_revision = snapshots + 100u; out->sync_observed_ns = now_ns;
    out->sync_raw_mode = deck == 1 ? 2 : deck == 2 ? 0 : 1;
    if (stable_state_receipts) {
        out->key_lock_revision = 10u + (unsigned) deck;
        out->key_lock_observed_ns = 1;
        out->sync_revision = 20u + (unsigned) deck;
        out->sync_observed_ns = 1;
    }
    if (tempo_capabilities) {
        out->valid |= AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED;
        out->tempo = 123.45 + deck;
        out->tempo_state_generation = 300u + (unsigned) deck;
        out->tempo_revision = snapshots + 200u;
        out->tempo_observed_ns = now_ns;
        out->tempo_slider_locked = deck & 1;
        out->tempo_lock_unit_generation = 400u + (unsigned) deck;
        out->tempo_lock_revision = snapshots + 300u;
        out->tempo_lock_observed_ns = now_ns;
    }
    if (native_capabilities) {
        out->valid |= AZ_LIVE_MASTER | AZ_LIVE_NATIVE_LOAD;
        out->master = deck == 0;
        out->master_revision = 9;
        out->master_observed_ns = now_ns;
        out->native_load_generation = 10u + (unsigned) deck;
        out->native_load_unit_generation = 100u + (unsigned) deck;
        out->native_load_invalidation_epoch = 1;
        out->native_load_observed_ns = now_ns;
    }
    if (native_delayed) {
        out->master_observed_ns = out->tempo_observed_ns = out->tempo_lock_observed_ns = 1;
        if (snapshots <= 2800) {
            out->native_load_generation = out->native_load_unit_generation = out->native_load_invalidation_epoch = out->native_load_observed_ns = 0;
        } else {
            if (!delayed_load_ns) delayed_load_ns = now_ns;
            out->native_load_observed_ns = delayed_load_ns;
        }
    }
    return deck == busy_deck ? AZ_BUSY : AZ_OK;
}
static int capabilities(az_data_capabilities *out) {
    assert(out->size == sizeof *out); out->version = AZ_DATA_VERSION;
    out->live_fields = AZ_LIVE_MASTER_TEMPO | AZ_LIVE_SYNC;
    if (tempo_capabilities) out->live_fields |= AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED;
    if (native_capabilities) out->live_fields |= AZ_LIVE_MASTER | AZ_LIVE_NATIVE_LOAD;
    out->commands = 0; return AZ_OK;
}
static az_library_v1 library = {.abi = AZ_LIBRARY_ABI, .size = sizeof library, .data_capabilities = capabilities, .deck_snapshot = snapshot};
static const az_library_v1 *service(uint32_t abi) { assert(abi == AZ_LIBRARY_ABI); return &library; }
static void log_line(const char *id, const char *format, ...) {
    assert(!strcmp(id, "sdk-state-probe") && logs < PROBE_CAPTURE_LINES);
    va_list args; va_start(args, format);
    int n = vsnprintf(lines[logs], sizeof lines[logs], format, args); va_end(args);
    assert(n >= 0 && (size_t) n < sizeof lines[logs]); logs++;
}
static int thread_start(const char *id, const char *name, void *(*fn)(void *), void *arg) {
    assert(!strcmp(id, "sdk-state-probe") && !strcmp(name, "sdk-public-state") && fn);
    starts++; if (start_failure) return -1; worker_fn = fn; worker_arg = arg; return 0;
}
#define clock_gettime probe_clock
#define nanosleep probe_sleep
#ifndef PROBE_MAX_RECORDS
#define PROBE_MAX_RECORDS 8u
#endif
#ifndef PROBE_MAX_TICKS
#define PROBE_MAX_TICKS 20u
#endif
#ifndef PUBLIC_PROBE_IMPL
#define PUBLIC_PROBE_IMPL "../../examples/public-state-probe/probe.c"
#endif
#include PUBLIC_PROBE_IMPL
#undef clock_gettime
#undef nanosleep
static void reset(void) {
    snapshots = starts = sleeps = logs = clock_calls = fail_clock_call = 0;
    busy_deck = -1; start_failure = 0; worker_fn = NULL; memset(lines, 0, sizeof lines);
    tempo_capabilities = native_capabilities = 0;
    stable_state_receipts = 0;
}
static void tempo_value_contracts(void) {
    for (int deck = 0; deck < 4; ++deck) {
        az_deck_snapshot s = {.size = sizeof s, .version = AZ_DATA_VERSION, .deck = (unsigned) deck,
            .valid = AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED, .tempo = 123.45,
            .tempo_state_generation = 30u + (unsigned) deck, .tempo_revision = 40,
            .tempo_observed_ns = 50, .tempo_lock_unit_generation = 60u + (unsigned) deck,
            .tempo_lock_revision = 70, .tempo_lock_observed_ns = 80, .tempo_slider_locked = deck & 1};
        struct probe_tempo_record v = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(v.tempo_valid && v.tempo == 123.45 && v.tempo_state == 30u + (unsigned) deck && v.tempo_rev == 40 && v.tempo_ns == 50);
        assert(v.slider_valid && v.slider_locked == (deck & 1) && v.slider_unit == 60u + (unsigned) deck && v.slider_rev == 70 && v.slider_ns == 80);
        struct probe_tempo_record newer = v;
        newer.tempo_rev++; newer.tempo_ns++; newer.slider_rev++; newer.slider_ns++;
        assert(!probe_tempo_changed(&newer, &v));
        newer.tempo_state++; assert(probe_tempo_changed(&newer, &v));
        newer = v; newer.slider_locked ^= 1; assert(probe_tempo_changed(&newer, &v));
        s.valid = 0;
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && newer.tempo == 0.0 && !newer.slider_valid && newer.slider_locked == -1);
        s.valid = AZ_LIVE_TEMPO | AZ_LIVE_TEMPO_SLIDER_LOCKED;
        const double invalid[] = {NAN, INFINITY, DBL_MAX, -1, 0, 20, 400};
        for (unsigned n = 0; n < sizeof invalid / sizeof invalid[0]; ++n) {
            s.tempo = invalid[n]; newer = probe_tempo_values(deck, 100, AZ_OK, &s);
            assert(!newer.tempo_valid && !newer.tempo && newer.slider_valid);
        }
        s.tempo = 123.45;
        for (int locked = -1; locked <= 2; ++locked) {
            s.tempo_slider_locked = locked; newer = probe_tempo_values(deck, 100, AZ_OK, &s);
            assert(newer.slider_valid == (locked == 0 || locked == 1));
            assert(newer.slider_locked == (newer.slider_valid ? locked : -1));
        }
        s.tempo_slider_locked = 1;
        s.tempo_observed_ns = 101; s.tempo_lock_observed_ns = 101;
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && !newer.slider_valid);
        s.tempo_observed_ns = s.tempo_lock_observed_ns = 0;
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && !newer.slider_valid);
        s.tempo_observed_ns = 50; s.tempo_lock_observed_ns = 80;
        s.tempo_state_generation = s.tempo_lock_unit_generation = 0;
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && !newer.slider_valid);
        s.tempo_state_generation = 30; s.tempo_lock_unit_generation = 60;
        s.tempo_revision = s.tempo_lock_revision = 0;
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && !newer.slider_valid);
        s.tempo_revision = 40; s.tempo_lock_revision = 70;
        s.deck = (unsigned) (deck + 1) % 4;
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && !newer.slider_valid);
        s.deck = (unsigned) deck;
        s.size = offsetof(az_deck_snapshot, tempo_state_generation);
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && !newer.tempo_state && newer.slider_valid);
        s.size = offsetof(az_deck_snapshot, tempo_slider_locked);
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && !newer.slider_valid);
        s.size = sizeof s; s.version = AZ_DATA_VERSION + 1;
        newer = probe_tempo_values(deck, 100, AZ_OK, &s);
        assert(!newer.tempo_valid && !newer.slider_valid);
        newer = probe_tempo_values(deck, 100, AZ_BUSY, &s);
        assert(!newer.tempo_valid && !newer.slider_valid);
    }
    uint32_t *short_record = malloc(sizeof *short_record); assert(short_record);
    *short_record = sizeof *short_record;
    struct probe_tempo_record short_value = probe_tempo_values(0, 100, AZ_OK, (const az_deck_snapshot *) short_record);
    assert(!short_value.tempo_valid && !short_value.slider_valid); free(short_record);
#ifndef PROBE_EXPECT_TEMPO_FORMAT_LIMIT
    struct probe_tempo_record large = {.rc = INT_MIN, .valid = UINT32_MAX, .returned = 3,
        .tempo_valid = 1, .tempo = 399.999999, .tempo_state = UINT64_MAX, .tempo_rev = UINT64_MAX,
        .tempo_ns = UINT64_MAX, .slider_valid = 1, .slider_locked = 1, .slider_unit = UINT64_MAX,
        .slider_rev = UINT64_MAX, .slider_ns = UINT64_MAX};
    reset(); assert(probe_tempo_emit(UINT_MAX, 3, UINT64_MAX, &large));
    assert(logs == 1 && strlen(lines[0]) < PROBE_TEMPO_LINE_BYTES && strstr(lines[0], "tempo_ns=18446744073709551615"));
#endif
}
#ifdef PROBE_NATIVE_TEST
static void native_value_contracts(void) {
    for (int deck=0;deck<4;deck++) {
        az_deck_snapshot s={.size=sizeof s,.version=AZ_DATA_VERSION,.deck=(unsigned)deck,
            .valid=AZ_LIVE_MASTER|AZ_LIVE_NATIVE_LOAD,.master=deck==0,.master_revision=3,.master_observed_ns=40,
            .native_load_generation=7,.native_load_unit_generation=10u+(unsigned)deck,.native_load_invalidation_epoch=2,.native_load_observed_ns=50};
        struct probe_native_record v=probe_native_values(deck,100,AZ_OK,&s);
        assert(v.master_valid && v.master==(deck==0) && v.load_valid && v.native_load==7 && v.load_unit==10u+(unsigned)deck && v.load_epoch==2 && v.source_load==0);
        s.valid=0;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.master_valid && v.master==-1 && !v.load_valid && !v.native_load);
        s.valid=AZ_LIVE_MASTER|AZ_LIVE_NATIVE_LOAD;
        s.master=2;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.master_valid && v.master==-1 && v.load_valid);s.master=0;
        s.master_observed_ns=s.native_load_observed_ns=101;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.master_valid && !v.load_valid);
        s.master_observed_ns=40;s.native_load_observed_ns=50;
        s.master_revision=0;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.master_valid && v.load_valid);s.master_revision=3;
        s.native_load_generation=0;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.load_valid && !v.native_load);s.native_load_generation=7;
        s.native_load_unit_generation=0;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.load_valid);s.native_load_unit_generation=10u+(unsigned)deck;
        s.native_load_invalidation_epoch=0;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.load_valid);s.native_load_invalidation_epoch=2;
        s.load_generation=99;v=probe_native_values(deck,100,AZ_OK,&s);assert(v.native_load==7 && v.source_load==99);
        s.deck=(unsigned)(deck+1)%4;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.master_valid && !v.load_valid);s.deck=(unsigned)deck;
        s.size=offsetof(az_deck_snapshot,master_revision);v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.master_valid && !v.load_valid);
        s.size=offsetof(az_deck_snapshot,native_load_generation);v=probe_native_values(deck,100,AZ_OK,&s);assert(v.master_valid && !v.load_valid);
        s.size=sizeof s;s.version++;v=probe_native_values(deck,100,AZ_OK,&s);assert(!v.master_valid && !v.load_valid);s.version--;
        v=probe_native_values(deck,100,AZ_BUSY,&s);assert(!v.master_valid && !v.load_valid);
    }
    uint32_t *short_record=malloc(sizeof *short_record);assert(short_record);*short_record=sizeof *short_record;
    struct probe_native_record v=probe_native_values(0,100,AZ_OK,(const az_deck_snapshot *)short_record);
    assert(!v.master_valid && !v.load_valid);free(short_record);
    struct probe_native_record large={.rc=INT_MIN,.valid=UINT32_MAX,.returned=3,.master_valid=1,.master=1,.master_rev=UINT64_MAX,.master_ns=UINT64_MAX,
        .load_valid=1,.native_load=UINT64_MAX,.load_unit=UINT64_MAX,.load_epoch=UINT64_MAX,.load_ns=UINT64_MAX,.source_load=UINT64_MAX,
        .tempo={.tempo_valid=1,.tempo=399.999999,.tempo_state=UINT64_MAX,.tempo_rev=UINT64_MAX,.tempo_ns=UINT64_MAX,.slider_valid=1,.slider_locked=1,.slider_unit=UINT64_MAX,.slider_rev=UINT64_MAX,.slider_ns=UINT64_MAX}};
    reset();assert(probe_native_emit(UINT_MAX,3,UINT64_MAX,&large));assert(logs==1 && strlen(lines[0])<768);
}
#endif
int main(void) {
    az_core_v1 core = {.abi = AZ_MOD_ABI, .size = sizeof core, .log = log_line, .thread_start = thread_start, .library_service = service};
    const az_mod_v1 *descriptor = az_mod_v1_entry();
    assert(descriptor->abi == AZ_MOD_ABI && descriptor->size == sizeof *descriptor && !strcmp(descriptor->id, "sdk-state-probe"));
    assert(!descriptor->tick && !descriptor->on_frame);
#ifdef PROBE_NATIVE_DELAY_TEST
    native_capabilities=tempo_capabilities=stable_state_receipts=native_delayed=1;
    assert(descriptor->init(&core)==0);worker_fn(worker_arg);
    for (int deck=0;deck<4;deck++) {
        int found=0;char who[40],value[60];snprintf(who,sizeof who,"deck=%d returned=%d",deck+1,deck+1);
        snprintf(value,sizeof value,"load_valid=1 native_load=%d",10+deck);
        for (unsigned n=0;n<logs;n++) if (strstr(lines[n],"SDK_PUBLIC_NATIVE v=1") && strstr(lines[n],who) && strstr(lines[n],value)) found=1;
        assert(found && "finite loaded collection must retain each delayed genuine receipt");
    }
    assert(snapshots==3000);
    return 0;
#endif
#ifdef PROBE_NATIVE_TEST
    native_capabilities = 1;
    assert(descriptor->init(&core) == 0); worker_fn(worker_arg);
    assert(strstr(lines[2], "SDK_PUBLIC_NATIVE v=1 seq=1 deck=1 returned=1"));
    assert(strstr(lines[2], "master_valid=1 master=1") && strstr(lines[2], "load_valid=1 native_load=10"));
    assert(strstr(lines[2], "source_load=0"));
    assert(snapshots == 4 && logs == 11 && sleeps == 0);
    for (int deck = 0; deck < 4; ++deck) {
        char expected[80];snprintf(expected,sizeof expected,"deck=%d returned=%d",deck+1,deck+1);
        assert(strstr(lines[2+deck*2],"SDK_PUBLIC_NATIVE v=1") && strstr(lines[2+deck*2],expected));
    }
    native_value_contracts();
    return 0;
#endif
#ifdef PROBE_CADENCE_TEST
    tempo_capabilities = stable_state_receipts = 1;
    assert(descriptor->init(&core) == 0); worker_fn(worker_arg);
    assert(snapshots == 48 && sleeps == 11 && logs == 19);
    assert(strstr(lines[17], "SDK_PUBLIC_STATE complete reason=ticks records=8"));
    assert(strstr(lines[18], "SDK_PUBLIC_TEMPO complete reason=ticks records=8"));
    puts("public tempo probe: receipt-only bumps ignored, periodic snapshot preserved");
    return 0;
#endif
    assert(descriptor->init(&core) == 0 && starts == 1 && worker_fn); worker_fn(worker_arg);
    assert(snapshots == 8 && logs == 10 && sleeps == 1);
    assert(strstr(lines[0], "capabilities") && strstr(lines[0], "commands=0"));
    assert(strstr(lines[1], "deck=1") && strstr(lines[1], "mt=1") && strstr(lines[1], "sync=-1"));
    assert(strstr(lines[2], "deck=2") && strstr(lines[2], "mt=-1") && strstr(lines[2], "sync=1") && strstr(lines[2], "raw=2"));
    assert(strstr(lines[3], "deck=3") && strstr(lines[3], "mt=0") && strstr(lines[3], "sync=0"));
    assert(strstr(lines[4], "deck=4") && strstr(lines[4], "mt=-1") && strstr(lines[4], "sync=-1"));
    assert(strstr(lines[9], "complete reason=record_limit records=8"));
    reset(); tempo_capabilities = 1;
    assert(descriptor->init(&core) == 0); worker_fn(worker_arg);
#ifdef PROBE_EXPECT_TEMPO_FORMAT_LIMIT
    assert(snapshots == 1 && sleeps == 0 && logs == 4);
    assert(strstr(lines[2], "SDK_PUBLIC_STATE complete reason=format_limit records=1"));
    assert(strstr(lines[3], "SDK_PUBLIC_TEMPO complete reason=format_limit records=0"));
#else
    assert(strstr(lines[2], "SDK_PUBLIC_TEMPO v=1 seq=1 deck=1 returned=1"));
    assert(strstr(lines[2], "tempo_valid=1 tempo=123.450000") && strstr(lines[2], "slider_valid=1 slider_locked=0"));
    for (int deck = 0; deck < 4; ++deck) {
        char expected[80];
        snprintf(expected, sizeof expected, "deck=%d returned=%d", deck + 1, deck + 1);
        assert(strstr(lines[2 + deck * 2], "SDK_PUBLIC_TEMPO v=1") && strstr(lines[2 + deck * 2], expected));
    }
    assert(snapshots == 4 && logs == 11 && sleeps == 0);
    assert(strstr(lines[9], "SDK_PUBLIC_STATE complete reason=record_limit records=4"));
    assert(strstr(lines[10], "SDK_PUBLIC_TEMPO complete reason=record_limit records=4"));
#endif
    tempo_value_contracts();
    reset(); busy_deck = 2; assert(descriptor->init(&core) == 0); worker_fn(worker_arg);
    assert(strstr(lines[3], "rc=-5") && strstr(lines[3], "mt=-1") && strstr(lines[3], "sync=-1"));
    reset(); fail_clock_call = 2; assert(descriptor->init(&core) == 0); worker_fn(worker_arg);
    assert(snapshots == 1 && logs == 2 && strstr(lines[1], "complete reason=clock records=0"));
    reset(); start_failure = 1; assert(descriptor->init(&core) == -1 && starts == 1 && !worker_fn);
    reset(); core.abi++; assert(descriptor->init(&core) == -1 && !starts); core.abi--;
    core.size = offsetof(az_core_v1, library_service); assert(descriptor->init(&core) == -1 && !starts); core.size = sizeof core;
    library.abi++; assert(descriptor->init(&core) == -1 && !starts); library.abi--;
    library.size = offsetof(az_library_v1, deck_snapshot); assert(descriptor->init(&core) == -1 && !starts); library.size = sizeof library;
    library.deck_snapshot = NULL; assert(descriptor->init(&core) == -1 && !starts);
    puts("public state probe: SDK entry, four decks, validity, finite budget and clock failure passed");
    return 0;
}
