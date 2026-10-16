/* SPDX-License-Identifier: GPL-3.0-only
 *
 * harness: load a community mod the way the AZ-MODS core does, against a stub az_core_v1,
 * and check that it behaves.
 *
 *     harness MOD.so ID MAX_REDRAWS_PER_SEC [--functional]
 *
 * The mod runs in a child process. Through a fake core it gets several soaks of 1000 ticks
 * (deck states that change every tick, odd values, page areas from full screen to tiny,
 * theme switches, clock steps, random knob, click, tap and BACK events, deck events, screen
 * changes, a reload), then,
 * with --functional, the scenarios written for that mod. Each check prints one line:
 *
 *     CHECK <name> PASS|FAIL|NOT-MEASURED <detail>
 *
 * and the exit status is 0 only if every check passed. What each check asserts:
 *
 *   init         init() returns 0, registers one screen with on_draw (or, for a mod with no
 *                page, a surface or a deck-event listener), keeps the ABI contract
 *   no-crash     the child ran to the end (no signal, no sanitizer abort)
 *   draw-area    every rectangle, checkbox, line, image and text box lies inside the page
 *                (or surface) area
 *   draw-context drawing services are called only from on_draw or on_paint, with the context
 *                it got
 *   text-size    every text size handed to draw_text / text_width is 8..96
 *   no-io        no file, process, sleep or network call from tick, a page or surface callback
 *                or a deck-event, confirm or post_ui callback
 *   no-alloc     no heap allocation from those callbacks
 *   redraw-rate  request_redraw from tick stays within MAX per simulated second (+slack)
 *   log-quiet    at most 30 log lines per 1000-tick soak (logging on change only)
 *   api          deck numbers 0..3, sane buffer sizes, valid settings keys, no hooks, no
 *                disk services (data_dir, assets_dir) outside init, surfaces and deck-event
 *                listeners registered from init, post_ui callbacks inside the mod
 *   functional   the mod's own scenarios (--functional, and a harness built with
 *                AZ_HARNESS_SCENARIOS; see harness_scenarios below)
 *
 * no-io and no-alloc are measured by iocount.so (LD_PRELOAD); without it they print
 * NOT-MEASURED. The clock is simulated: this program defines clock_gettime, time and
 * gettimeofday, and is linked with -rdynamic so the mod's calls resolve here.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "azmod-mod.h"

/* helpers for a mod's scenarios (AZ_HARNESS_SCENARIOS): unused in a harness built without */
#define SCENARIO_API __attribute__((unused))

#define T0_WALL 1791662400LL          /* the simulated wall clock starts here (UTC) */
#define TICK_NS 33333333LL
#define NS 1000000000LL

/* ---- the simulated clock ---------------------------------------------------------------- */

/* half a second past a whole second, so tick boundaries never sit exactly on one */
static int64_t mono_ns = 5000 * NS + NS / 2;
static int64_t wall_ns = T0_WALL * NS + NS / 2;

#ifdef CLOCK_REALTIME_COARSE
#define IS_WALL(id) ((id) == CLOCK_REALTIME || (id) == CLOCK_REALTIME_COARSE)
#else
#define IS_WALL(id) ((id) == CLOCK_REALTIME)
#endif

int clock_gettime(clockid_t id, struct timespec *ts)
{
    int64_t v = IS_WALL(id) ? wall_ns : mono_ns;
    ts->tv_sec = (time_t)(v / NS);
    ts->tv_nsec = (long)(v % NS);
    return 0;
}

time_t time(time_t *t)
{
    time_t v = (time_t)(wall_ns / NS);
    if (t) *t = v;
    return v;
}

#if defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 31))
int gettimeofday(struct timeval *__restrict tv, __timezone_ptr_t tz)
#else
int gettimeofday(struct timeval *__restrict tv, void *__restrict tz)
#endif
{
    (void)tz;
    tv->tv_sec = (time_t)(wall_ns / NS);
    tv->tv_usec = (suseconds_t)(wall_ns % NS / 1000);
    return 0;
}

static void advance(int64_t ns)
{
    mono_ns += ns;
    wall_ns += ns;
}

/* ---- checks ----------------------------------------------------------------------------- */

enum { K_INIT,
       K_AREA,
       K_CTX,
       K_PX,
       K_IO,
       K_ALLOC,
       K_REDRAW,
       K_LOG,
       K_API,
       K_FUNC,
       NK };
static const char *const KNAME[NK] = {
    "init",
    "draw-area",
    "draw-context",
    "text-size",
    "no-io",
    "no-alloc",
    "redraw-rate",
    "log-quiet",
    "api",
    "functional",
};
static unsigned long kfails[NK];
static char kfirst[NK][240];
static int kmeasured[NK];

static void fail(int k, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void fail(int k, const char *fmt, ...)
{
    if (kfails[k]++ == 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(kfirst[k], sizeof kfirst[k], fmt, ap);
        va_end(ap);
    }
}

/* ---- the counters in iocount.so (absent in the sanitizer run) ----------------------------- */

static void (*azt_arm)(int);
static void (*azt_counts)(unsigned long *, unsigned long *, const char **, const char **);

static void arm(int on)
{
    if (azt_arm) azt_arm(on);
}

/* ---- the stub core ------------------------------------------------------------------------ */

struct az_draw {
    unsigned magic;
};
enum { CTX_NONE,
       CTX_INIT,
       CTX_TICK,
       CTX_DRAW,
       CTX_INPUT,
       CTX_ENTER,
       CTX_UI };
static const char *const CTXNAME[] = { "outside", "init", "tick", "on_draw", "on_input", "on_enter",
                                       "a UI callback" };

static const char *mod_id;
static int ctx = CTX_NONE;
static struct az_draw draw_ctx = { 0xA2D7A2D7u }, *cur_d;
static az_rect cur_area;
static int cur_row = 60;
static int theme = 2;
static const az_screen_v1 *screen;
static int screens_registered;

/* what tick and the page asked for */
static unsigned long redraw_tick, redraw_input, redraw_pending;
static unsigned long log_lines;

/* the last frame's text, for the functional scenarios */
#define FRAME_MAX 160
static char frame[FRAME_MAX][200];
static int frame_n;

/* the decks. The mod's own threads read them (deck_info, deck_grid) while the soaks change
 * them, so every change and every read holds dmu. */
static struct {
    int count;
    int loaded[4];
    int playing[4];
    double bpm[4];
    double phase[4];       /* position in the beat, 0..1; -1 = no grid */
    char title[4][400];
    char artist[4][128];
    int view;           /* AZ_VIEW_* */
} D;
static pthread_mutex_t dmu = PTHREAD_MUTEX_INITIALIZER;

/* settings, kept across reloads as the core keeps them */
static struct {
    char key[40];
    int val;
} cfg_i[32];
static struct {
    char key[40];
    char val[128];
} cfg_s[16];
static int ncfg_i, ncfg_s;

static int key_ok(const char *key)
{
    if (!key) return 0;
    size_t n = strlen(key);
    if (n == 0 || n > 31) return 0;
    for (const char *p = key; *p; p++)
        if (*p == '=' || *p == '$' || *p == '\n' || *p == '\r') return 0;
    return 1;
}

/* A reload stands for a restart of the program, which ends every thread the mod started.
 * The harness cannot stop a thread from outside, so once a reload begins, each thread from
 * an earlier lifetime is parked for good at its next core call, and load_mod waits for that
 * before calling init again. Without this, the new init and an old thread would share the
 * mod's globals, which never happens on the deck. */
static int lifetime = 1;                /* atomic: bumped by each reload of a mod with threads */
static __thread int thread_lifetime;    /* 0 on threads the harness did not start for the mod */
static pthread_mutex_t park_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t park_cv = PTHREAD_COND_INITIALIZER;
static int nstarted, nparked;           /* under park_mu: mod threads started and parked */

static void lifetime_gate(void)
{
    if (!thread_lifetime || thread_lifetime == __atomic_load_n(&lifetime, __ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&park_mu);
    nparked++;
    pthread_cond_broadcast(&park_cv);
    for (;;) pthread_cond_wait(&park_cv, &park_mu);
}

static void check_id(const char *id, const char *what)
{
    lifetime_gate();
    if (!id || strcmp(id, mod_id) != 0) fail(K_API, "%s called with id \"%s\"", what, id ? id : "(null)");
}

static int s_cfg_get(const char *id, const char *key, int def)
{
    check_id(id, "cfg_get");
    if (!key_ok(key)) {
        fail(K_API, "cfg_get with a bad key");
        return def;
    }
    for (int i = 0; i < ncfg_i; i++)
        if (!strcmp(cfg_i[i].key, key)) return cfg_i[i].val;
    return def;
}

static void s_cfg_set(const char *id, const char *key, int val)
{
    check_id(id, "cfg_set");
    if (!key_ok(key)) {
        fail(K_API, "cfg_set with a bad key");
        return;
    }
    for (int i = 0; i < ncfg_i; i++)
        if (!strcmp(cfg_i[i].key, key)) {
            cfg_i[i].val = val;
            return;
        }
    if (ncfg_i < 32) {
        snprintf(cfg_i[ncfg_i].key, 40, "%s", key);
        cfg_i[ncfg_i++].val = val;
    }
}

static int s_cfg_get_str(const char *id, const char *key, char *buf, int n, const char *def)
{
    check_id(id, "cfg_get_str");
    if (!buf || n <= 0 || n > 65536) {
        fail(K_API, "cfg_get_str with buffer size %d", n);
        return 0;
    }
    memset(buf, 'U', (size_t)n);                /* the whole buffer is the core's to fill */
    const char *v = def ? def : "";
    if (!key_ok(key)) fail(K_API, "cfg_get_str with a bad key");
    else
        for (int i = 0; i < ncfg_s; i++)
            if (!strcmp(cfg_s[i].key, key)) v = cfg_s[i].val;
    int len = (int)strlen(v);
    if (len > n - 1) len = n - 1;
    memcpy(buf, v, (size_t)len);
    buf[len] = '\0';
    return len;
}

static void s_cfg_set_str(const char *id, const char *key, const char *val)
{
    check_id(id, "cfg_set_str");
    if (!key_ok(key) || !val) {
        fail(K_API, "cfg_set_str with a bad key or value");
        return;
    }
    char clean[128];
    int j = 0;
    for (const char *p = val; *p && j < 127; p++)
        if ((unsigned char)*p >= 0x20) clean[j++] = *p;
    clean[j] = '\0';
    for (int i = 0; i < ncfg_s; i++)
        if (!strcmp(cfg_s[i].key, key)) {
            memcpy(cfg_s[i].val, clean, 128);
            return;
        }
    if (ncfg_s < 16) {
        snprintf(cfg_s[ncfg_s].key, 40, "%s", key);
        memcpy(cfg_s[ncfg_s++].val, clean, 128);
    }
}

SCENARIO_API static const char *cfg_str(const char *key)
{
    for (int i = 0; i < ncfg_s; i++)
        if (!strcmp(cfg_s[i].key, key)) return cfg_s[i].val;
    return NULL;
}

SCENARIO_API static int cfg_int(const char *key, int def)
{
    for (int i = 0; i < ncfg_i; i++)
        if (!strcmp(cfg_i[i].key, key)) return cfg_i[i].val;
    return def;
}

static char last_log[400];
static void s_log(const char *id, const char *fmt, ...)
{
    check_id(id, "log");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_log, sizeof last_log, fmt, ap);
    va_end(ap);
    log_lines++;
}

static int s_claim_hook(const char *id, uintptr_t addr, void *hook, void **orig)
{
    (void)id;
    (void)addr;
    (void)hook;
    (void)orig;
    fail(K_API, "claim_hook called by a \"firmware\": [\"any\"] mod");
    return 1;
}

static int s_deck_for(void *p)
{
    (void)p;
    return -1;
}

static const char *s_fw_tag(void) { return "ep147-0123456789abcdef"; }

static int s_register_screen(const char *id, const az_screen_v1 *scr)
{
    check_id(id, "register_screen");
    if (ctx != CTX_INIT) fail(K_API, "register_screen from %s", CTXNAME[ctx]);
    if (!scr || scr->size < (int)sizeof(az_screen_v1) || !scr->on_draw) return 1;
    screen = scr;
    screens_registered++;
    return 0;
}

static int in_draw(const az_draw *d, const char *what)
{
    if (ctx != CTX_DRAW) {
        fail(K_CTX, "%s from %s", what, CTXNAME[ctx]);
        return 0;
    }
    if (d != cur_d || !d || d->magic != draw_ctx.magic) {
        fail(K_CTX, "%s with a stale paint context", what);
        return 0;
    }
    return 1;
}

static void check_rgb(uint32_t rgb, const char *what)
{
    if (rgb > 0xFFFFFF) fail(K_API, "%s color 0x%" PRIx32 " is not 0xRRGGBB", what, rgb);
}

static void check_box(const char *what, int x, int y, int w, int h, const char *s)
{
    az_rect a = cur_area;
    if (w < 0 || h < 0 || x < a.x || y < a.y || (int64_t)x + w > (int64_t)a.x + a.w ||
        (int64_t)y + h > (int64_t)a.y + a.h)
        fail(K_AREA, "%s %s%s%sat (%d,%d %dx%d) outside area (%d,%d %dx%d)", what,
             s ? "\"" : "", s ? s : "", s ? "\" " : "", x, y, w, h, a.x, a.y, a.w, a.h);
}

/* Glyph widths close enough to the menu font for layout checks: digits and capitals are
 * wide, spaces narrow, anything outside ASCII full width. */
static int s_text_width(const char *s, int px)
{
    if (ctx != CTX_DRAW) fail(K_CTX, "text_width from %s", CTXNAME[ctx]);
    if (px < 8 || px > 96) fail(K_PX, "text_width at %d px", px);
    if (!s) {
        fail(K_API, "text_width(NULL)");
        return 0;
    }
    int64_t w = 0;
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++, n++) {
        if (n > 4096) {
            fail(K_API, "text_width on a string over 4 KB");
            break;
        }
        if ((*p & 0xC0) == 0x80) continue;                  /* UTF-8 continuation */
        if (*p >= 0x80) w += px;
        else if (*p == ' ') w += px * 3 / 10;
        else if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z')) w += px * 62 / 100;
        else w += px * 52 / 100;
    }
    return (int)w;
}

static void record_text(const char *s)
{
    if (frame_n < FRAME_MAX) snprintf(frame[frame_n++], sizeof frame[0], "%s", s);
}

static void s_draw_header(az_draw *d, const char *title)
{
    if (!in_draw(d, "draw_header")) return;
    if (!title) fail(K_API, "draw_header(NULL)");
    else record_text(title);
}

static void s_draw_text(az_draw *d, int x, int y, const char *s, int px, uint32_t rgb)
{
    if (!in_draw(d, "draw_text")) return;
    if (!s) {
        fail(K_API, "draw_text(NULL)");
        return;
    }
    if (px < 8 || px > 96) fail(K_PX, "draw_text \"%s\" at %d px", s, px);
    check_rgb(rgb, "draw_text");
    int saved = ctx;
    int w = s_text_width(s, px < 8 ? 8 : px > 96 ? 96
                                                 : px);
    ctx = saved;
    check_box("text", x, y, w, px * 3 / 2, s);
    record_text(s);
}

static void s_fill_rect(az_draw *d, int x, int y, int w, int h, uint32_t rgb)
{
    if (!in_draw(d, "fill_rect")) return;
    check_rgb(rgb, "fill_rect");
    check_box("fill_rect", x, y, w, h, NULL);
}

static void s_draw_rect(az_draw *d, int x, int y, int w, int h, int width, uint32_t rgb)
{
    if (!in_draw(d, "draw_rect")) return;
    check_rgb(rgb, "draw_rect");
    if (width < 1 || width > 64) fail(K_API, "draw_rect line width %d", width);
    check_box("draw_rect", x, y, w, h, NULL);
}

static void s_draw_checkbox(az_draw *d, int x, int y, int size, int on, int hl)
{
    (void)on;
    (void)hl;
    if (!in_draw(d, "draw_checkbox")) return;
    check_box("checkbox", x, y, size, size, NULL);
}

static const uint32_t PALETTE[2][5] = {
    { 0xECECEC, 0x8A8F99, 0xF28C28, 0x111111, 0x16171B },    /* dark */
    { 0x1A1A1A, 0x6B6F78, 0xF28C28, 0xFFFFFF, 0xF4F1EA },    /* Day */
};

static uint32_t s_color(int role)
{
    if (ctx != CTX_DRAW) fail(K_CTX, "color from %s", CTXNAME[ctx]);
    if (role < AZ_COL_TEXT || role > AZ_COL_BG) {
        fail(K_API, "color(%d)", role);
        return 0;
    }
    return PALETTE[theme == 1][role];
}

static int s_row_height(az_draw *d)
{
    if (!in_draw(d, "row_height")) return cur_row;
    return cur_row;
}

static void s_request_redraw(void)
{
    if (ctx == CTX_TICK) redraw_tick++;
    else redraw_input++;
    redraw_pending = 1;
}

static int deck_ok(int deck, const char *what)
{
    lifetime_gate();
    if (ctx == CTX_NONE) fail(K_API, "%s from outside a callback", what);
    if (deck < 0 || deck > 3) {
        fail(K_API, "%s(%d): decks are 0..3", what, deck);
        return 0;
    }
    return 1;
}

static int s_deck_count(void)
{
    lifetime_gate();
    return D.count;
}
static int s_deck_loaded(int deck) { return deck_ok(deck, "deck_loaded") ? D.loaded[deck] : 0; }
static double s_deck_bpm(int deck) { return deck_ok(deck, "deck_bpm") ? D.bpm[deck] : 0.0; }

static int s_deck_title(int deck, char *buf, int n)
{
    if (!buf || n <= 0 || n > 65536) {
        fail(K_API, "deck_title buffer size %d", n);
        return 0;
    }
    memset(buf, 'U', (size_t)n);                /* the whole buffer is the core's to fill */
    buf[0] = '\0';
    if (!deck_ok(deck, "deck_title") || !D.loaded[deck]) return 0;
    int len = (int)strlen(D.title[deck]);
    if (len > n - 1) len = n - 1;
    memcpy(buf, D.title[deck], (size_t)len);
    buf[len] = '\0';
    return len;
}

static int s_ui_theme(void) { return theme; }

static const char *s_disk_dir(const char *id)
{
    check_id(id, "data_dir/assets_dir");
    if (ctx != CTX_INIT) fail(K_API, "data_dir/assets_dir from %s (they touch the disk)", CTXNAME[ctx]);
    return NULL;
}

/* ---- core 1.2.3: the library, audio taps, threads ----------------------------------------- */

/* A small library in USB 1 ("Sets" folder holding "Peak"; "Warmup" at the top, under a parent
 * id the listing does not contain, which is how the core reports the top level); none in
 * USB 2. Peak's track list comes back cut (return 1), which a mod must still show.
 * Called on the mod's own thread: pure data, no harness state. */
static int s_lib_playlists(const char *id, int stick, int (*fn)(int, int, const char *, int, void *),
                           void *user)
{
    check_id(id, "lib_playlists");
    if (stick < 0 || stick > 1) fail(K_API, "lib_playlists stick %d (0 = USB 1, 1 = USB 2)", stick);
    if (stick != 0 || !fn) return -1;
    if (fn(1, 0, "Sets", 1, user)) return 0;
    if (fn(3, 77, "Warmup", 0, user)) return 0;
    fn(2, 1, "Peak", 0, user);
    return 0;
}

static int s_lib_tracks(const char *id, int stick, int playlist, int (*fn)(const az_track *, void *),
                        void *user)
{
    check_id(id, "lib_tracks");
    if (stick < 0 || stick > 1) fail(K_API, "lib_tracks stick %d (0 = USB 1, 1 = USB 2)", stick);
    static const struct {
        int pl;
        const char *title, *artist, *key;
        double bpm;
    } T[] = {
        { 2, "Opening Track", "Artist A", "8A", 128.0 },
        { 2, "Second Track", "Artist B", "9A", 126.5 },
        { 3, "Slow Start", "Artist C", "", 0.0 },
    };
    if (stick != 0 || !fn) return -1;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        if (playlist && T[i].pl != playlist) continue;
        az_track t = { sizeof t, (uint32_t)(10 + i), T[i].title, T[i].artist, "", T[i].key, "/media/usb/x.mp3",
                       T[i].bpm, 200.0 };
        if (fn(&t, user)) break;
    }
    return playlist == 2 ? 1 : 0;
}

/* Taps are recorded; the functional scenario plays blocks into them. */
#define MAX_TAPS 8
static struct {
    int source, frames;
    void (*fn)(const float *, int, void *);
    void *user;
} taps[MAX_TAPS];
static int ntaps;
static int keep_loaded;     /* a thread or tap holds the library: never unloaded, as on the deck */

static int s_audio_tap(const char *id, int source, int frames, void (*fn)(const float *, int, void *),
                       void *user)
{
    check_id(id, "audio_tap");
    if (source < 0 || source > 3) fail(K_API, "audio_tap source %d (decks are 0..3)", source);
    if (frames < 64 || frames > 8192) fail(K_API, "audio_tap frames %d (64..8192)", frames);
    if (ntaps >= 4) return -1;   /* the core's limit: 4 per mod (taps outlive a reload) */
    if (!fn || ntaps == MAX_TAPS || source < 0 || source > 3 || frames < 64 || frames > 8192) return -1;
    taps[ntaps].source = source;
    taps[ntaps].frames = frames;
    taps[ntaps].fn = fn;
    taps[ntaps].user = user;
    ntaps++;
    keep_loaded = 1;
    return 0;
}

/* ---- core 1.2.3: deck state and events, banners, confirm, post_ui -------------------------- */

static int s_deck_info(int deck, az_deck_info *out)
{
    lifetime_gate();
    if (!out || out->size < (int)offsetof(az_deck_info, title)) {
        fail(K_API, "deck_info with size %d", out ? out->size : -1);
        return -1;
    }
    if (deck < 0 || deck > 3) {
        fail(K_API, "deck_info(%d): decks are 0..3", deck);
        return -1;
    }
    int size = out->size;
    pthread_mutex_lock(&dmu);
    memset(out, 0, sizeof *out);
    out->size = size;
    out->loaded = D.loaded[deck];
    out->playing = D.loaded[deck] ? D.playing[deck] : 0;
    out->pad_mode = -1;
    out->bpm = D.loaded[deck] ? D.bpm[deck] : 0.0;
    out->len_s = -1;
    if (D.loaded[deck]) {
        snprintf(out->title, sizeof out->title, "%s", D.title[deck]);
        snprintf(out->artist, sizeof out->artist, "%s", D.artist[deck]);
    }
    pthread_mutex_unlock(&dmu);
    return 0;
}

static int s_deck_grid(int deck, double *beat, double *bar)
{
    lifetime_gate();
    if (deck < 0 || deck > 3) {
        fail(K_API, "deck_grid(%d): decks are 0..3", deck);
        return -1;
    }
    pthread_mutex_lock(&dmu);
    double ph = D.loaded[deck] ? D.phase[deck] : -1.0;
    pthread_mutex_unlock(&dmu);
    if (bar) *bar = -1.0;
    if (ph < 0) return -1;
    if (beat) *beat = ph;
    return 0;
}

static int s_view(void)
{
    lifetime_gate();
    return __atomic_load_n(&D.view, __ATOMIC_RELAXED);
}

#define MAX_LISTEN 4
static struct {
    void (*fn)(int, int, void *);
    void *user;
} listeners[MAX_LISTEN];
static int nlisten;

static int s_on_deck_event(const char *id, void (*fn)(int, int, void *), void *user)
{
    check_id(id, "on_deck_event");
    if (ctx != CTX_INIT) {
        fail(K_API, "on_deck_event from %s", CTXNAME[ctx]);
        return -1;
    }
    if (!fn || nlisten >= MAX_LISTEN) return -1;
    listeners[nlisten].fn = fn;
    listeners[nlisten].user = user;
    nlisten++;
    return 0;
}

static void fire(int deck, int what)
{
    for (int i = 0; i < nlisten; i++) {
        ctx = CTX_UI;
        arm(1);
        listeners[i].fn(deck, what, listeners[i].user);
        arm(0);
        ctx = CTX_NONE;
    }
}

#define BANNERS 8
static char banners[BANNERS][112];
static int nbanner;

static void s_banner(const char *id, const char *text, int ms)
{
    check_id(id, "banner");
    if (!text || ms <= 0) {
        fail(K_API, "banner(%s, %d ms)", text ? "text" : "NULL", ms);
        return;
    }
    snprintf(banners[nbanner % BANNERS], sizeof banners[0], "%s", text);   /* the core keeps 111 bytes */
    nbanner++;
}

SCENARIO_API static const char *last_banner(void) { return nbanner ? banners[(nbanner - 1) % BANNERS] : ""; }

static struct {
    int open;
    char title[128], text[256], yes[64];
    void (*fn)(int, void *);
    void *user;
} Q;

static int s_confirm(const char *id, const char *title, const char *text, const char *yes,
                     void (*fn)(int, void *), void *user)
{
    check_id(id, "confirm");
    if (ctx == CTX_NONE || ctx == CTX_DRAW) fail(K_API, "confirm from %s", CTXNAME[ctx]);
    if (!title || !text || !yes || !fn) {
        fail(K_API, "confirm with a NULL argument");
        return -1;
    }
    if (Q.open) return -1;
    Q.open = 1;
    snprintf(Q.title, sizeof Q.title, "%s", title);
    snprintf(Q.text, sizeof Q.text, "%s", text);
    snprintf(Q.yes, sizeof Q.yes, "%s", yes);
    Q.fn = fn;
    Q.user = user;
    return 0;
}

static void answer(int yes)
{
    if (!Q.open) return;
    Q.open = 0;
    ctx = CTX_UI;
    arm(1);
    Q.fn(yes, Q.user);
    arm(0);
    ctx = CTX_NONE;
}

/* post_ui: queued from any thread, run on the UI thread before the next tick. */
#define POSTQ 256
static struct {
    void (*fn)(void *);
    void *user;
} postq[POSTQ];
static unsigned post_head, post_tail;
static pthread_mutex_t post_mu = PTHREAD_MUTEX_INITIALIZER;
static void *mod_base;                  /* the mod's load address, for "fn must be in your code" */

static void s_post_ui(const char *id, void (*fn)(void *), void *user)
{
    check_id(id, "post_ui");
    Dl_info di;
    if (!fn || !dladdr((void *)fn, &di) || di.dli_fbase != mod_base) {
        fail(K_API, "post_ui with a function outside the mod");
        return;
    }
    pthread_mutex_lock(&post_mu);
    if (post_tail - post_head < POSTQ) {
        postq[post_tail % POSTQ].fn = fn;
        postq[post_tail % POSTQ].user = user;
        post_tail++;
    }
    pthread_mutex_unlock(&post_mu);
}

static void drain_post(void)
{
    for (;;) {
        pthread_mutex_lock(&post_mu);
        if (post_head == post_tail) {
            pthread_mutex_unlock(&post_mu);
            return;
        }
        void (*fn)(void *) = postq[post_head % POSTQ].fn;
        void *user = postq[post_head % POSTQ].user;
        post_head++;
        pthread_mutex_unlock(&post_mu);
        ctx = CTX_UI;
        arm(1);
        fn(user);
        arm(0);
        ctx = CTX_NONE;
    }
}

/* ---- core 1.2.3: surfaces, lines and images ------------------------------------------------- */

#define MAX_SURF 4
struct az_surface {
    az_surface_v1 d;
    az_rect r;
    int shown, placed, dirty;
};
static struct az_surface surfs[MAX_SURF];
static int nsurf, focus_surf = -1;

static int surf_index(az_surface *sp, const char *what)
{
    for (int i = 0; i < nsurf; i++)
        if (sp == &surfs[i]) return i;
    fail(K_API, "%s with a surface the core did not create", what);
    return -1;
}

static az_surface *s_surface_create(const char *id, const az_surface_v1 *d)
{
    check_id(id, "surface_create");
    if (ctx != CTX_INIT) {
        fail(K_API, "surface_create from %s", CTXNAME[ctx]);
        return NULL;
    }
    if (!d || d->size < (int)sizeof(az_surface_v1) || !d->on_paint || nsurf >= MAX_SURF) return NULL;
    memset(&surfs[nsurf], 0, sizeof surfs[nsurf]);
    surfs[nsurf].d = *d;
    return &surfs[nsurf++];
}

static int ui_ctx(const char *what)
{
    if (ctx == CTX_NONE || ctx == CTX_DRAW) {
        fail(K_API, "%s from %s", what, CTXNAME[ctx]);
        return 0;
    }
    return 1;
}

static void s_surface_place(az_surface *sp, az_rect r)
{
    int i = surf_index(sp, "surface_place");
    if (i < 0 || !ui_ctx("surface_place")) return;
    if (r.w <= 0 || r.h <= 0 || r.x < 0 || r.y < 0 || r.x + r.w > 1280 || r.y + r.h > 800)
        fail(K_API, "surface_place (%d,%d %dx%d) off the 1280x800 screen", r.x, r.y, r.w, r.h);
    surfs[i].r = r;
    surfs[i].placed = 1;
    surfs[i].dirty = 1;
}

static void s_surface_show(az_surface *sp, int on)
{
    int i = surf_index(sp, "surface_show");
    if (i < 0 || !ui_ctx("surface_show")) return;
    surfs[i].shown = on != 0;
    surfs[i].dirty = 1;
}

static void s_surface_redraw(az_surface *sp)
{
    int i = surf_index(sp, "surface_redraw");
    if (i < 0 || !ui_ctx("surface_redraw")) return;
    surfs[i].dirty = 1;
    s_request_redraw();                 /* counted like request_redraw: the same rate limit */
}

static int s_surface_focus(az_surface *sp, int on)
{
    int i = surf_index(sp, "surface_focus");
    if (i < 0 || !ui_ctx("surface_focus")) return 0;
    if (!on) {
        if (focus_surf == i) focus_surf = -1;
        return 1;
    }
    if (focus_surf >= 0 && focus_surf != i) return 0;
    focus_surf = i;
    return 1;
}

static void s_draw_line(az_draw *d, int x0, int y0, int x1, int y1, int width, uint32_t rgb)
{
    if (!in_draw(d, "draw_line")) return;
    check_rgb(rgb, "draw_line");
    if (width < 1 || width > 64) fail(K_API, "draw_line width %d", width);
    int h = width / 2;
    int lx = (x0 < x1 ? x0 : x1) - h, ly = (y0 < y1 ? y0 : y1) - h;
    int hx = (x0 > x1 ? x0 : x1) + width - h, hy = (y0 > y1 ? y0 : y1) + width - h;
    check_box("draw_line", lx, ly, hx - lx, hy - ly, NULL);
}

static int s_draw_image(az_draw *d, int x, int y, const unsigned char *rgba, int w, int h)
{
    if (!in_draw(d, "draw_image")) return -1;
    if (!rgba || w <= 0 || h <= 0 || w > 1280 || h > 800) {
        fail(K_API, "draw_image %dx%d", w, h);
        return -1;
    }
    for (int i = 0; i < w * h; i++) {
        const unsigned char *p = rgba + i * 4;
        if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) {
            fail(K_API, "draw_image pixel %d is not premultiplied", i);
            break;
        }
    }
    check_box("draw_image", x, y, w, h, NULL);
    return 0;
}

static int nthreads;                    /* started in this lifetime */

struct mod_thread {
    void *(*fn)(void *);
    void *arg;
    int lifetime;
};

static void *mod_thread_main(void *p)
{
    struct mod_thread t = *(struct mod_thread *)p;
    free(p);
    thread_lifetime = t.lifetime;
    return t.fn(t.arg);
}

static int s_thread_start(const char *id, const char *name, void *(*fn)(void *), void *arg)
{
    check_id(id, "thread_start");
    if (!name || !fn || nthreads >= 4) return -1;
    struct mod_thread *mt = malloc(sizeof *mt);
    if (!mt) return -1;
    mt->fn = fn;
    mt->arg = arg;
    mt->lifetime = __atomic_load_n(&lifetime, __ATOMIC_ACQUIRE);
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&t, &a, mod_thread_main, mt);
    pthread_attr_destroy(&a);
    if (rc != 0) {
        free(mt);
        return -1;
    }
    nthreads++;
    keep_loaded = 1;
    pthread_mutex_lock(&park_mu);
    nstarted++;
    pthread_mutex_unlock(&park_mu);
    return 0;
}

/* End the previous lifetime's threads (see lifetime_gate). A thread that makes no core call
 * within 2 s keeps running; that is reported, and its accesses stay visible to the
 * sanitizers. */
static void end_lifetime(void)
{
    __atomic_add_fetch(&lifetime, 1, __ATOMIC_ACQ_REL);
    pthread_mutex_lock(&park_mu);
    for (int i = 0; i < 200 && nparked < nstarted; i++) {
        pthread_mutex_unlock(&park_mu);
        struct timespec ts = { 0, 10000000L };
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&park_mu);
    }
    if (nparked < nstarted)
        printf("  note: %d mod thread(s) made no core call in 2 s and still run\n", nstarted - nparked);
    pthread_mutex_unlock(&park_mu);
    nthreads = 0;
}

static const az_core_v1 CORE = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_core_v1),
    .deck_for_ds = s_deck_for,
    .deck_for_modeobj = s_deck_for,
    .claim_hook = s_claim_hook,
    .cfg_get = s_cfg_get,
    .cfg_set = s_cfg_set,
    .log = s_log,
    .fw_tag = s_fw_tag,
    .register_screen = s_register_screen,
    .draw_header = s_draw_header,
    .draw_text = s_draw_text,
    .fill_rect = s_fill_rect,
    .draw_rect = s_draw_rect,
    .draw_checkbox = s_draw_checkbox,
    .color = s_color,
    .row_height = s_row_height,
    .text_width = s_text_width,
    .request_redraw = s_request_redraw,
    .deck_count = s_deck_count,
    .deck_loaded = s_deck_loaded,
    .deck_bpm = s_deck_bpm,
    .deck_title = s_deck_title,
    .ui_theme = s_ui_theme,
    .cfg_get_str = s_cfg_get_str,
    .cfg_set_str = s_cfg_set_str,
    .data_dir = s_disk_dir,
    .assets_dir = s_disk_dir,
    .thread_start = s_thread_start,
    .lib_playlists = s_lib_playlists,
    .lib_tracks = s_lib_tracks,
    .audio_tap = s_audio_tap,
    .view = s_view,
    .surface_create = s_surface_create,
    .surface_place = s_surface_place,
    .surface_show = s_surface_show,
    .surface_redraw = s_surface_redraw,
    .surface_focus = s_surface_focus,
    .draw_image = s_draw_image,
    .draw_line = s_draw_line,
    .deck_info = s_deck_info,
    .deck_grid = s_deck_grid,
    .on_deck_event = s_on_deck_event,
    .banner = s_banner,
    .confirm = s_confirm,
    .post_ui = s_post_ui,
};

/* ---- loading and calling the mod ---------------------------------------------------------- */

static const char *mod_path;
static void *handle;
static const az_mod_v1 *mod;

static int load_mod(void)
{
    if (handle && !keep_loaded) dlclose(handle);   /* the core never unloads a mod with a thread or tap */
    if (nthreads) end_lifetime();
    handle = dlopen(mod_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fail(K_INIT, "dlopen: %s", dlerror());
        return 0;
    }
    const az_mod_v1 *(*entry)(void) = (const az_mod_v1 *(*)(void))dlsym(handle, "az_mod_v1_entry");
    if (!entry) {
        fail(K_INIT, "no az_mod_v1_entry");
        return 0;
    }
    mod = entry();
    if (!mod || mod->abi != AZ_MOD_ABI || mod->size < (int)sizeof(az_mod_v1) || !mod->init) {
        fail(K_INIT, "bad descriptor");
        return 0;
    }
    if (!mod->id || strcmp(mod->id, mod_id) != 0) {
        fail(K_INIT, "descriptor id is not %s", mod_id);
        return 0;
    }
    if (!mod->fw_tags || !mod->fw_tags[0] || strcmp(mod->fw_tags[0], "any") != 0 || mod->fw_tags[1])
        fail(K_INIT, "fw_tags is not { \"any\", NULL }");
    Dl_info di;
    mod_base = dladdr((void *)entry, &di) ? di.dli_fbase : NULL;
    screen = NULL;
    screens_registered = 0;
    nsurf = nlisten = 0;
    focus_surf = -1;
    Q.open = 0;
    ctx = CTX_INIT;
    int rc = mod->init(&CORE);
    ctx = CTX_NONE;
    if (rc != 0) {
        fail(K_INIT, "init returned %d", rc);
        return 0;
    }
    if (screens_registered > 1 || (screens_registered == 1 && !screen) ||
        (screens_registered == 0 && nsurf == 0 && nlisten == 0)) {
        fail(K_INIT, "init registered %d screens, %d surfaces, %d deck-event listeners",
             screens_registered, nsurf, nlisten);
        return 0;
    }
    return 1;
}

static void call_tick(void)
{
    drain_post();                       /* post_ui callbacks run on the UI thread, before tick */
    if (!mod->tick) return;
    ctx = CTX_TICK;
    arm(1);
    mod->tick();
    arm(0);
    ctx = CTX_NONE;
}

/* Paint every surface that is shown on the current screen, as the core does when one is due. */
static void paint_surfaces(int force)
{
    frame_n = 0;
    for (int i = 0; i < nsurf; i++) {
        struct az_surface *sp = &surfs[i];
        if (!sp->shown || !sp->placed || !(sp->d.views & AZ_VIEW_BIT(s_view()))) continue;
        if (!sp->dirty && !force) continue;
        sp->dirty = 0;
        cur_area = sp->r;
        cur_d = &draw_ctx;
        ctx = CTX_DRAW;
        arm(1);
        sp->d.on_paint(cur_d, sp->r, sp->d.user);
        arm(0);
        ctx = CTX_NONE;
        cur_d = NULL;
    }
    redraw_pending = 0;
}

static int surface_input(int i, int kind, int dir, int x, int y)
{
    struct az_surface *sp = &surfs[i];
    if (!sp->d.on_input) return 0;
    az_event ev = { kind, dir, x, y };
    ctx = CTX_INPUT;
    arm(1);
    int r = sp->d.on_input(&ev, sp->d.user);
    arm(0);
    ctx = CTX_NONE;
    return r;
}

static void call_draw(az_rect area, int row)
{
    if (!screen) {
        paint_surfaces(1);
        return;
    }
    cur_area = area;
    cur_row = row;
    cur_d = &draw_ctx;
    frame_n = 0;
    redraw_pending = 0;
    ctx = CTX_DRAW;
    arm(1);
    screen->on_draw(cur_d, area, screen->user);
    arm(0);
    ctx = CTX_NONE;
    cur_d = NULL;
}

static int call_input(int kind, int dir, int x, int y)
{
    if (!screen) {
        /* no page: taps go to the surface under them, the knob, click and BACK to the focused one */
        for (int i = 0; i < nsurf; i++) {
            az_rect r = surfs[i].r;
            int on = surfs[i].shown && (surfs[i].d.views & AZ_VIEW_BIT(s_view()));
            if (kind == AZ_EV_TAP && on && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)
                return surface_input(i, kind, dir, x, y);
            if (kind != AZ_EV_TAP && focus_surf == i)
                return surface_input(i, kind, dir, x, y);
        }
        return 0;
    }
    az_event ev = { kind, dir, x, y };
    ctx = CTX_INPUT;
    arm(1);
    int r = screen->on_input ? screen->on_input(&ev, screen->user) : 0;
    arm(0);
    ctx = CTX_NONE;
    return r;
}

static void call_enter(void)
{
    if (!screen || !screen->on_enter) return;
    ctx = CTX_ENTER;
    arm(1);
    screen->on_enter(screen->user);
    arm(0);
    ctx = CTX_NONE;
}

/* ---- soaks ------------------------------------------------------------------------------- */

static unsigned rng = 12345;
static unsigned rnd(void)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) & 0xFFFFFF;
}
static int chance(int n) { return rnd() % (unsigned)n == 0; }

static const char *const TITLES[] = {
    "Daft Punk - One More Time",
    "Ünïcødé Tïtlé – Ünïcødé Tïtlé – Ünïcødé Tïtlé – Ünïcødé Tïtlé – Ünïcødé Tïtlé",
    "",
    "a",
    "tab\there and\nnewline \x7f and \x01 control",
    "\xff\xfe invalid UTF-8 \xc3",
    "日本語のトラック名 日本語のトラック名 日本語のトラック名",
};

static void random_title(int deck, int weird)
{
    if (weird && chance(3)) {
        int n = 300 + (int)(rnd() % 99);        /* longer than any buffer a mod should keep */
        for (int i = 0; i < n; i++) D.title[deck][i] = (char)('A' + i % 26);
        D.title[deck][n] = '\0';
        return;
    }
    snprintf(D.title[deck], sizeof D.title[deck], "%s",
             TITLES[rnd() % (weird ? 7u : 2u)]);
}

static double random_bpm(int weird)
{
    if (weird) {
        switch (rnd() % 8) {
        case 0: return 0.0;
        case 1: return NAN;
        case 2: return INFINITY;
        case 3: return -120.0;
        case 4: return 1e12;
        case 5: return 999.995;
        default: break;
        }
    }
    return 60.0 + (rnd() % 12000) / 100.0;
}

struct soak {
    const char *name;
    az_rect area;
    int row;
    int ticks;
    int inputs;
    int weird;
    int bpm_drift;          /* nudge every loaded deck's BPM every tick (a moving pitch fader) */
};

static void soak(const struct soak *s, double max_rate)
{
    unsigned long r0 = redraw_tick, l0 = log_lines;
    int active = 1, away = 0;
    call_enter();
    for (int t = 0; t < s->ticks; t++) {
        advance(TICK_NS);
        if (t == s->ticks / 2) wall_ns += (s->weird ? -600 : 3600) * NS;    /* a clock step */
        if (t % 300 == 150) D.count = s->weird && chance(2) ? (int)(rnd() % 9) - 1 : (D.count == 4 ? 2 : 4);
        if (t % 200 == 100) theme = theme == 1 ? 2 + (int)(rnd() % 4) : 1;
        int events[4] = { 0, 0, 0, 0 };
        pthread_mutex_lock(&dmu);
        if (t % 120 == 60) D.view = (int)(rnd() % 6);
        for (int deck = 0; deck < 4; deck++) {
            if (chance(90)) {
                D.loaded[deck] = !D.loaded[deck];
                D.playing[deck] = 0;
                if (D.loaded[deck]) {
                    random_title(deck, s->weird);
                    snprintf(D.artist[deck], sizeof D.artist[deck], "%s", chance(2) ? "Artist" : "");
                    D.bpm[deck] = random_bpm(s->weird);
                    events[deck] = AZ_DECK_LOADED;
                } else {
                    D.title[deck][0] = '\0';
                    D.artist[deck][0] = '\0';
                    D.bpm[deck] = 0.0;
                    events[deck] = AZ_DECK_EJECTED;
                }
            } else if (D.loaded[deck] && chance(60)) {
                D.playing[deck] = !D.playing[deck];
                events[deck] = D.playing[deck] ? AZ_DECK_PLAY : AZ_DECK_STOP;
            }
            if (D.loaded[deck] && s->bpm_drift) D.bpm[deck] += 0.013;
            if (D.loaded[deck] && s->weird && chance(40)) D.bpm[deck] = random_bpm(1);
            D.phase[deck] = D.loaded[deck] ? (t % 16) / 16.0 : -1.0;
        }
        pthread_mutex_unlock(&dmu);
        for (int deck = 0; deck < 4; deck++)
            if (events[deck]) fire(deck, events[deck]);
        if (Q.open && chance(20)) answer(chance(2));

        call_tick();

        if (!screen) paint_surfaces(t % 15 == 0);
        else if (active && (redraw_pending || t % 15 == 0)) call_draw(s->area, s->row);
        if (!active && ++away > 30) {
            active = 1;
            away = 0;
            call_enter();
        }

        if (active && s->inputs && chance(5)) {
            int x = s->area.x - 50 + (int)(rnd() % (unsigned)(s->area.w + 100));
            int y = s->area.y - 50 + (int)(rnd() % (unsigned)(s->area.h + 100));
            unsigned k = rnd() % 20;
            int r;
            if (!screen && nsurf && chance(2)) {      /* aim at a surface, sometimes a long press */
                az_rect sr = surfs[rnd() % (unsigned)nsurf].r;
                x = sr.x + (int)(rnd() % (unsigned)(sr.w > 0 ? sr.w : 1));
                y = sr.y + (int)(rnd() % (unsigned)(sr.h > 0 ? sr.h : 1));
                if (chance(4)) {
                    for (int i = 0; i < nsurf; i++)
                        if (surfs[i].shown && x >= surfs[i].r.x && x < surfs[i].r.x + surfs[i].r.w &&
                            y >= surfs[i].r.y && y < surfs[i].r.y + surfs[i].r.h)
                            surface_input(i, AZ_EV_LONG, 0, x, y);
                    continue;
                }
            }
            if (k < 8) r = call_input(AZ_EV_KNOB, chance(2) ? -1 : 1, 0, 0);
            else if (k < 11) r = call_input(AZ_EV_CLICK, 0, 0, 0);
            else if (k < 17) r = call_input(AZ_EV_TAP, 0, x, y);
            else if (k < 19) r = call_input(AZ_EV_BACK, 0, 0, 0);
            else r = call_input(99, 7, x, y);
            if (k >= 17 && k < 19 && r == 0) active = 0;    /* the core went back */
        }
    }
    double secs = s->ticks * (double)TICK_NS / NS;
    unsigned long redraws = redraw_tick - r0, logs = log_lines - l0;
    double limit = max_rate * secs + secs / 60.0 + 3.0;
    if (redraws > limit)
        fail(K_REDRAW, "%s: %lu redraws from tick in %.0f s (limit %.0f)", s->name, redraws, secs, limit);
    if (logs > 30) fail(K_LOG, "%s: %lu log lines in %d ticks (last: %s)", s->name, logs, s->ticks, last_log);
    printf("  soak %-10s %4d ticks  %4lu redraws from tick  %3lu log lines\n", s->name, s->ticks, redraws, logs);
}

/* ---- for a mod's functional scenarios ----------------------------------------------------- */

SCENARIO_API static int frame_has(const char *s)
{
    for (int i = 0; i < frame_n; i++)
        if (strstr(frame[i], s)) return 1;
    return 0;
}

SCENARIO_API static void expect(int ok, const char *what)
{
    printf("    %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fail(K_FUNC, "%s", what);
}

SCENARIO_API static const az_rect FULL = { 0, 0, 1280, 800 };

SCENARIO_API static void ticks(int n)
{
    for (int i = 0; i < n; i++) {
        advance(TICK_NS);
        call_tick();
    }
}

static void reset_world(void)
{
    pthread_mutex_lock(&dmu);
    memset(&D, 0, sizeof D);
    for (int d = 0; d < 4; d++) D.phase[d] = -1.0;
    pthread_mutex_unlock(&dmu);
    nbanner = 0;
    D.count = 2;
    ncfg_i = ncfg_s = 0;
    mono_ns = 5000 * NS + NS / 2;
    wall_ns = T0_WALL * NS + NS / 2;
    theme = 2;
}

SCENARIO_API static void dump_frame(void)
{
    for (int i = 0; i < frame_n; i++) printf("      | %s\n", frame[i]);
}

/* A mod's own scenarios come from the file named by AZ_HARNESS_SCENARIOS, compiled into
 * this one (-DAZ_HARNESS_SCENARIOS='"path/scenarios.c"'), so they can use every helper above.
 * It defines harness_scenarios(id): run the scenarios for that mod id and return 1, or
 * return 0 when it has none (functional then prints NOT-MEASURED). */
#ifdef AZ_HARNESS_SCENARIOS
#include AZ_HARNESS_SCENARIOS
#else
static int harness_scenarios(const char *id)
{
    (void)id;
    return 0;
}
#endif

/* ---- main --------------------------------------------------------------------------------- */

static int run(double max_rate, int functional)
{
    azt_arm = (void (*)(int))dlsym(RTLD_DEFAULT, "azt_arm");
    azt_counts = (void (*)(unsigned long *, unsigned long *, const char **, const char **))
        dlsym(RTLD_DEFAULT, "azt_counts");
    kmeasured[K_IO] = kmeasured[K_ALLOC] = azt_counts != NULL;
    for (int k = 0; k < NK; k++)
        if (k != K_IO && k != K_ALLOC) kmeasured[k] = 1;
    kmeasured[K_FUNC] = functional;

    static const struct soak SOAKS[] = {
        { "full", { 0, 0, 1280, 800 }, 60, 1000, 1, 0, 0 },
        { "odd", { 37, 91, 800, 480 }, 48, 1000, 1, 1, 0 },
        { "small", { 0, 0, 320, 200 }, 40, 1000, 1, 0, 0 },
        { "tiny", { 5, 5, 120, 60 }, 24, 1000, 1, 1, 0 },
        { "huge-rows", { 0, 0, 1280, 800 }, 200, 1000, 1, 0, 0 },
        { "drift", { 0, 0, 1280, 800 }, 60, 1000, 0, 0, 1 },
    };
    reset_world();
    D.count = 4;
    if (!load_mod()) goto report;
    for (size_t i = 0; i < sizeof SOAKS / sizeof SOAKS[0]; i++) {
        rng = 1000u + (unsigned)i;
        soak(&SOAKS[i], max_rate);
        if (i == 2) {                           /* a reload in the middle, settings kept */
            advance(20 * NS);
            if (!load_mod()) goto report;
        }
    }
    if (functional) {
        if (!harness_scenarios(mod_id)) kmeasured[K_FUNC] = 0;
    }

report:
    if (azt_counts) {
        unsigned long io, alloc;
        const char *io_name, *alloc_name;
        azt_counts(&io, &alloc, &io_name, &alloc_name);
        if (io) fail(K_IO, "%lu calls, the first %s()", io, io_name);
        if (alloc) fail(K_ALLOC, "%lu calls, the first %s()", alloc, alloc_name);
    }
    int ok = 1;
    for (int k = 0; k < NK; k++) {
        if (!kmeasured[k]) {
            printf("CHECK %s NOT-MEASURED\n", KNAME[k]);
            continue;
        }
        printf("CHECK %s %s %s\n", KNAME[k], kfails[k] ? "FAIL" : "PASS", kfails[k] ? kfirst[k] : "");
        if (kfails[k]) ok = 0;
    }
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s MOD.so ID MAX_REDRAWS_PER_SEC [--functional]\n", argv[0]);
        return 2;
    }
    mod_path = argv[1];
    mod_id = argv[2];
    double max_rate = atof(argv[3]);
    int functional = argc > 4 && !strcmp(argv[4], "--functional");

    setvbuf(stdout, NULL, _IONBF, 0);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 2;
    }
    if (pid == 0) _exit(run(max_rate, functional));

    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (WIFSIGNALED(st)) {
        printf("CHECK no-crash FAIL killed by signal %d\n", WTERMSIG(st));
        return 1;
    }
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : 2;
    if (code > 1) {
        printf("CHECK no-crash FAIL exit status %d\n", code);
        return 1;
    }
    printf("CHECK no-crash PASS\n");
    return code;
}
