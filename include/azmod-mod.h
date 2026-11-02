/* SPDX-License-Identifier: GPL-3.0-only
 *
 * azmod-mod.h: the one header a community mod includes.
 *
 * A community mod is a shared library (<id>.so) that the AZ-MODS core loads into EP147
 * (the XDJ-AZ's main program: its screens, controls and audio), after the core has started.
 * It is versioned by AZ_MOD_ABI; the core refuses a mod built for a different ABI, says why
 * on the Mod Menu's Community Mods page, and the deck carries on without it.
 *
 * Full contract and rationale: the AZ-MODS SDK's developer guide (docs/README.md), its
 * docs/GUIDELINES.md (the rules), and the comments below (the terse form of those rules).
 *
 * What the .so boundary does not give you: crash isolation. Your code runs inside EP147.
 * A crash in your mod stops the music on a DJ's set exactly as a crash in EP147 would.
 * The core switches off a mod whose code crashed EP147 at the next start (only that mod;
 * see docs/04-testing.md for how quick repeated crashes are handled), but the only real
 * protection is your code being careful.
 */
#ifndef AZMOD_MOD_H
#define AZMOD_MOD_H

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* clang-format off */
#define AZ_MOD_ABI 1
/* The AZ-MODS release this core ships in, as MAJOR.MINOR.PATCH: it equals the AZ-MODS
 * version and is bumped with every release, whether or not the mod interface changed. A mod
 * whose azmod.json "min_core" is above it is refused with "Needs AZ-MODS <min_core> or newer",
 * so set "min_core" to the first release that has everything your mod relies on. */
#define AZ_CORE_VERSION "1.2.3"

/* ---- on_frame ---------------------------------------------------------------------
 * A mod that only reads a frame returns AZ_FRAME_OK. A mod that wants later mods not to
 * see it returns AZ_FRAME_EATEN: the core stops the chain for that frame (an LED frame is
 * still sent, with the changes made so far: it is never dropped). A mod
 * that edits a frame in place returns AZ_FRAME_OK; the core recomputes the CRC once, after
 * the last mod. A mod never computes a CRC itself. */
#define AZ_FRAME_OK    0
#define AZ_FRAME_EATEN 1
#define AZ_BUS_ERP_LED 1   /* the 128-byte pad / mode-button LED frame (CRC over [0:0x78]) */
#define AZ_BUS_ERP_IN 2    /* deck buttons, pads and jog of decks 1/2 as EP147 reads them (CRC over [0:28]) */
#define AZ_BUS_MIXER_IN 3  /* the mixer/panel: knobs, faders, browse knob, screen buttons (CRC over [0:96]) */
#define AZ_BUS_MIXER_LED 4 /* the mixer's LED frame as EP147 writes it: read-only */
#define AZ_BUS_DECK_IN 5   /* the two deck MCU buses (jog platters): read-only */

/* ---- screens: a mod's own page in the Mod Menu --------------------------------------
 * The core owns the window, navigation and the theme. A mod registers a screen; the core
 * shows it as the mod's page under Mod Menu > Community Mods > Installed > <mod>, hands it the
 * rectangle to paint in, and passes it input only while it is the active screen. A mod
 * paints only through the draw helpers below, and only from on_draw. Returning 0 from
 * on_input for AZ_EV_BACK lets the core go back. */
typedef struct az_draw az_draw;                 /* opaque paint context, clipped by the core */
typedef struct { int x, y, w, h; } az_rect;     /* device pixels */
enum { AZ_EV_KNOB = 1, AZ_EV_CLICK, AZ_EV_TAP, AZ_EV_BACK,
       AZ_EV_DOWN, AZ_EV_UP, AZ_EV_LONG };   /* core 1.2.3: press, release, long press (surfaces) */
typedef struct { int kind; int dir; int x, y; } az_event;   /* dir: knob -1/+1; x,y: screen pixels */
/* theme color roles: the active theme's palette, so a mod matches Day and the dark themes */
enum { AZ_COL_TEXT = 0, AZ_COL_TEXT_DIM, AZ_COL_HI, AZ_COL_HI_TEXT, AZ_COL_BG };

typedef struct az_screen_v1 {
    int   size;                                              /* = sizeof(az_screen_v1) */
    void (*on_draw)(az_draw *d, az_rect area, void *user);   /* paint inside area only */
    int  (*on_input)(const az_event *ev, void *user);        /* 1 = handled, 0 = core decides */
    void (*on_enter)(void *user);                            /* became active; may be NULL */
    void  *user;                                             /* handed back to every callback */
} az_screen_v1;

/* Reserved for overlay windows in a later ABI; nothing in ABI 1 uses it. */
typedef struct az_win az_win;

/* ---- added in core 1.2.3 -----------------------------------------------------------
 * Types for the services marked "added in core 1.2.3" in az_core_v1. Every struct starts
 * with `size` (= sizeof at your build): the core reads only the fields your build knows, so
 * a mod built against an older header keeps working when fields are appended. */

/* Mod Menu rows: your mod's rows in the Mod Menu's main list, beside the official ones.
 * They appear after the official rows, grouped by mod, in load order then registration
 * order. A row's value is saved for you in azmod.conf as "<id>:<key>" and survives an
 * update or a reinstall (it is kept when the mod is removed). */
enum { AZ_ROW_TOGGLE = 0,  /* a checkbox; value 0/1 */
       AZ_ROW_GROUP,       /* a heading with no value; its children indent under it */
       AZ_ROW_ACTION,      /* a button; on_activate runs when it is pressed */
       AZ_ROW_SCREEN,      /* a ">" row that opens a page: `screen`, else your register_screen
                            * page; with neither it is grayed and does nothing */
       AZ_ROW_CHOICE };    /* one of `choices`; value = the index */
#define AZ_ROW_DEFAULT_ON 0x1u   /* TOGGLE: on until the DJ changes it */
#define AZ_ROW_ICON_N 96         /* a row icon's width and height, in pixels */
typedef struct az_row_v1 {
    int         size;      /* = sizeof(az_row_v1) */
    const char *key;       /* [a-z0-9-], 1..24 chars, unique within your mod */
    const char *label;     /* up to 31 bytes, drawn in the native row font */
    int         kind;      /* AZ_ROW_* */
    unsigned    flags;     /* AZ_ROW_DEFAULT_ON */
    const char *parent;    /* NULL = the main list, or the key of one of your earlier GROUP or
                            * TOGGLE rows: the row indents under it and is grayed while it is off */
    const char *help;      /* the description on the right, or NULL */
    const unsigned char *icon;           /* NULL = none, or an AZ_ROW_ICON_N x AZ_ROW_ICON_N
                                          * coverage mask (row-major, one byte a pixel, 0 = clear,
                                          * 255 = ink), drawn in the row's text color like the
                                          * official rows' icons. The core copies it at
                                          * register_row. */
    const char *const   *choices;        /* CHOICE: the option labels */
    int                  n_choices;      /* CHOICE: 2..8 */
    int                  default_choice; /* CHOICE: the index until the DJ changes it */
    const az_screen_v1  *screen;         /* SCREEN: the screen the row opens (on_draw, and every
                                          * callback, in your code); NULL = your register_screen
                                          * page. BACK from it returns to the row. */
    int  (*available)(void *user);         /* UI thread; 0 = grayed (say why in help). May be NULL. */
    void (*on_change)(int value, void *user); /* UI thread, after the value is saved. May be NULL. */
    void (*on_activate)(void *user);        /* ACTION: UI thread. */
    void *user;                             /* handed back to every callback */
} az_row_v1;

/* Frame filters: see frame_filter. */
typedef int (*az_frame_fn)(int bus, unsigned char *frame, int len, void *user);
#define AZ_FILTER_RAW 0x80000000u /* in bus_mask: see input frames before the core edits them, read-only */

/* Touch: one contact on the 1280x800 panel, in screen pixels. */
typedef struct { int slot, x, y, down; } az_touch;
#define AZ_TOUCH_PASS 0   /* EP147 gets the touch */
#define AZ_TOUCH_TAKE 1   /* the touch is yours; EP147 never sees it */
typedef int (*az_touch_fn)(const az_touch *t, void *user);

/* EP147's current screen. */
enum { AZ_VIEW_OTHER = 0, AZ_VIEW_SOURCE, AZ_VIEW_BROWSE, AZ_VIEW_SEARCH, AZ_VIEW_DECK,
       AZ_VIEW_MENU };   /* the Tag List and Playlist screens count as AZ_VIEW_BROWSE */
#define AZ_VIEW_BIT(v) (1u << (v))

/* Surfaces: a rectangle you paint over EP147's screen, shown only on the views you name. */
typedef struct az_surface az_surface;
enum { AZ_LAYER_OVER_EP147 = 0 /* above EP147, below the Mod Menu */, AZ_LAYER_TOP /* above the Mod Menu */ };
typedef struct az_surface_v1 {
    int      size;      /* = sizeof(az_surface_v1) */
    int      layer;     /* AZ_LAYER_* */
    unsigned views;     /* AZ_VIEW_BIT(...) mask: shown only there */
    void (*on_paint)(az_draw *d, az_rect area, void *user); /* draw helpers only, inside area */
    int  (*on_input)(const az_event *ev, void *user);       /* taps inside it, plus knob and click while
                                                             * it has focus; 1 = handled */
    void *user;
} az_surface_v1;

/* A snapshot of one deck. Unknown fields are -1 / 0 / "". */
typedef struct az_deck_info {
    int      size;        /* = sizeof(az_deck_info), set by you before the call */
    int      loaded;      /* 1 = a track is loaded */
    int      playing;     /* 1 = the playhead moved forward in the last 250 ms, -1 until known */
    int      pad_mode;    /* 0 HOT CUE, 1 BEAT LOOP, 2 SLIP LOOP, 3 BEAT JUMP, -1 unknown */
    int      has_stems;   /* 1 = stems were found for the loaded track */
    int      stems_on;    /* 1 = the stems engine plays this deck */
    uint32_t content_id;  /* the rekordbox track id on the stick, 0 = unknown */
    double   bpm;         /* the current tempo */
    double   pos_s, len_s;/* playhead and track length, seconds; len_s -1 = unknown */
    char     title[128], artist[128], key[8];
} az_deck_info;
enum { AZ_DECK_LOADED = 1, AZ_DECK_EJECTED, AZ_DECK_PLAY, AZ_DECK_STOP,
       AZ_DECK_CUES_CHANGED /* native cue-set update/invalidation; no saved/source/load claim */ };

/* One library track (read-only). */
typedef struct az_track {
    int         size;
    uint32_t    id;            /* rekordbox content id */
    const char *title, *artist, *album, *key, *path;  /* valid during the callback only */
    double      bpm, len_s;
} az_track;

/* Launching other programs (core 1.2.3). */
typedef struct az_launch_opts {
    int         size;        /* = sizeof(az_launch_opts) */
    int         nice;        /* 0..19, applied to the program */
    unsigned    cpu_mask;    /* allowed cores, bit n = core n; 0 = any */
    unsigned    mem_mb;      /* memory limit in MB, 0 = none */
    const char *log_path;    /* stdout/stderr go here (a file of your data_dir, not a link),
                              * NULL = discarded */
} az_launch_opts;

/* Audio taps: one deck's output as EP147 renders it, before the mixer. The XDJ-AZ mixes in
 * hardware (faders, EQ, crossfader, master), so no software on the deck sees the master. */
#define AZ_TAP_DECK(n) (n)  /* 0..3 */

/* ---- Track data contract v1 (development; first release not assigned yet) ------------
 * Check AZ_HAS for every service. All new output records require size >= sizeof the v1
 * record; larger records retain their tail. Input version must be AZ_DATA_VERSION.
 * Times are original-track seconds. Validity bits, never zero values, establish presence.
 * IDs are session-local; never persist references or tickets across a core restart. */
#define AZ_DATA_VERSION 1u
#define AZ_DATA_MIN_CORE "1.2.3" /* development baseline; AZ_HAS is mandatory */
enum { AZ_OK = 0, AZ_PENDING = 1, AZ_EMPTY = 2, AZ_UNSUPPORTED = -2,
       AZ_INVALID = -3, AZ_STALE = -4, AZ_BUSY = -5, AZ_FAILED = -6,
       AZ_CANCELLED = -7, AZ_LIMIT = -8, AZ_NOT_FOUND = -9 };
enum { AZ_DATA_METADATA = 1u, AZ_DATA_CUES = 2u, AZ_DATA_GRID = 4u,
       AZ_DATA_PHRASES = 8u, AZ_DATA_WAVEFORM = 16u };
enum { AZ_PROV_NONE, AZ_PROV_NATIVE, AZ_PROV_FILE, AZ_PROV_DERIVED };
enum { AZ_AVAIL_UNSUPPORTED, AZ_AVAIL_LOADING, AZ_AVAIL_READY, AZ_AVAIL_EMPTY,
       AZ_AVAIL_FAILED, AZ_AVAIL_STALE };
typedef struct az_track_ref {
    uint32_t size, version;
    uint64_t source_id, source_generation, library_id;
    uint32_t track_id, reserved;
} az_track_ref;
typedef struct az_component_info {
    uint32_t availability, provenance;
    uint64_t revision, observed_ns;
} az_component_info;
#define AZ_META_TITLE 1u
#define AZ_META_ARTIST 2u
#define AZ_META_ALBUM 4u
#define AZ_META_KEY 8u
#define AZ_META_DURATION 16u
#define AZ_META_TEMPO 32u
#define AZ_META_PATH 64u
#define AZ_META_FORMAT 128u
typedef struct az_track_metadata {
    uint32_t size, version, valid, reserved;
    char title[192], artist[192], album[192], key[32], path[512], format[32];
    double duration_s, original_bpm;
} az_track_metadata;
enum { AZ_CUE_MAIN, AZ_CUE_HOT, AZ_CUE_MEMORY };
enum { AZ_CUE_POINT = 1, AZ_CUE_LOOP = 2 };
#define AZ_CUE_START 1u
#define AZ_CUE_END 2u
#define AZ_CUE_COLOR 4u
#define AZ_CUE_COMMENT 8u
#define AZ_CUE_AUTO_ACTIVE 16u
#define AZ_CUE_SAVED 32u
#define AZ_CUE_COLOR_ID 64u /* raw saved memory-cue palette ID; not an RGB value */
/* Hot slot IDs are 1..8. Memory entry IDs are scoped to the cue component revision;
 * ordering, edits or reloads can change them. No list offset is a permanent identity. */
typedef struct az_cue_entry {
    uint32_t size, version, valid, classification;
    uint32_t entry_id, slot, type, rgb;
    double start_s, end_s;
    int32_t saved, modified, auto_active;
    union { uint32_t reserved; uint32_t color_id; };
    char comment[128];
} az_cue_entry;
typedef struct az_beat {
    double time_s, bpm;
    uint32_t beat_in_bar, reserved; /* 0 = no downbeat information; no inferred meter */
} az_beat;
typedef struct az_phrase {
    uint32_t start_beat, end_beat; /* zero-based grid indexes, end exclusive */
    uint32_t raw_kind, raw_mood, category, reserved; /* category 0 = unclassified */
} az_phrase;
enum { AZ_WAVE_PREVIEW = 1, AZ_WAVE_DETAIL = 2 };
typedef struct az_waveform {
    uint32_t size, version, representation, sample_count;
    double start_s, seconds_per_sample;
    const uint8_t *samples; /* raw PWAV height bytes; lifetime = snapshot */
} az_waveform;
/* Bounded source-time detail windows. PWV3 columns encode height in bits 0..4 and
 * intensity in bits 5..7. PWV5 is BE16: RGB 3-bit fields at bits13,10,7; height bits2..6.
 * PWV7 is three raw bytes: low, mid, high bands. These are stored analysis,
 * not calibrated PCM or player pixels. */
enum { AZ_WAVE_PWV3_MONO = 1, AZ_WAVE_PWV5_RGB16 = 2, AZ_WAVE_PWV7_BANDS = 3,
       AZ_WAVE_PWV4_PREVIEW = 4, AZ_WAVE_PWV6_PREVIEW = 5,
       AZ_WAVE_PWV2_TINY_PREVIEW = 6, AZ_WAVE_PWAV_PREVIEW = 7,
       AZ_WAVE_PWV7_LAYERED_RAW = 8 };
/* LAYERED_RAW derives six-byte columns from PWV7 at150 columns/s. Bytes0..1
 * contain a little-endian header: layer count1..3 in bits12..15 and three
 * symbolic IDs in successive low nibbles. Bytes2..4 contain the corresponding
 * absolute raw magnitudes in descending order; unused magnitudes are zero.
 * IDs1/2/3/4/5/6/8 denote input-byte subsets C/B/A/BC/AC/AB/ABC, where A/B/C
 * mean PWV7 bytes0/1/2. Allzero input is one ID8 layer with magnitude0. Byte5
 * is SDK-reserved zero; native allocation padding has no defined value.
 * Provenance is AZ_PROV_DERIVED. IDs are not RGB, frequency cutoffs or pixels. */
#define AZ_WAVE_WINDOW_MAX 4096u
typedef struct az_wave_query {
    uint32_t size, version, encoding, first_column, column_count, reserved;
} az_wave_query;
typedef struct az_wave_snapshot {
    uint32_t size, version;
    az_track_ref track;
    uint64_t revision, observed_ns;
    az_component_info info;
    uint32_t encoding, first_column, column_count, total_columns;
    uint32_t bytes_per_column, data_bytes, reserved[2];
    double start_s, seconds_per_column;
    const uint8_t *columns; /* immutable until track_release(ticket), even after staleness */
} az_wave_snapshot;
/* Immutable, bounded data. Pointers remain valid until track_release(ticket), including
 * after invalidation. Components can have different observation times. Saved file cues
 * never claim to describe current unsaved deck edits. */
typedef struct az_track_snapshot {
    uint32_t size, version;
    az_track_ref track;
    uint64_t revision, observed_ns;
    az_component_info metadata_info, cues_info, grid_info, phrases_info, waveform_info;
    az_track_metadata metadata;
    uint32_t cue_count, beat_count, phrase_count, reserved;
    const az_cue_entry *cues;
    const az_beat *beats;
    const az_phrase *phrases;
    az_waveform waveform;
} az_track_snapshot;
#define AZ_LOOP_DEFINED 1u
#define AZ_LOOP_ENGAGED 2u
#define AZ_LOOP_RELOOP 4u
#define AZ_LOOP_MODE 8u
#define AZ_LOOP_SAVED_ENTRY 16u
#define AZ_LOOP_BOUNDS 32u /* in_s/out_s describe the current working loop, independently of retained points */
typedef struct az_loop_state {
    uint32_t size, version, valid, mode;
    int32_t defined, engaged, reloop_available, saved_classification;
    uint32_t saved_entry_id, reserved;
    double in_s, out_s;
} az_loop_state;
#define AZ_LIVE_LOADED 1u
#define AZ_LIVE_TRACK 2u
#define AZ_LIVE_POSITION 4u
#define AZ_LIVE_PLAYBACK 8u
#define AZ_LIVE_RATE 16u
#define AZ_LIVE_DIRECTION 32u
#define AZ_LIVE_TEMPO 64u
#define AZ_LIVE_KEY_SHIFT 128u
#define AZ_LIVE_MASTER_TEMPO 256u
#define AZ_LIVE_SYNC 512u
#define AZ_LIVE_MASTER 1024u /* capabilities: observer installed; snapshot validity: current confirmation */
#define AZ_LIVE_QUANTIZE 2048u
#define AZ_LIVE_SLIP 4096u
#define AZ_LIVE_LOOP_ENGAGED 8192u /* native notification; other loop fields have separate validity */
#define AZ_LIVE_LOOP_BOUNDS 16384u /* current working IN/OUT in source seconds; inspect AZ_LOOP_BOUNDS */
#define AZ_LIVE_PLAY_FLAGS 32768u /* raw native play notification; not normalized playback */
#define AZ_LIVE_KEY_LOCK 65536u /* raw setting; verified Master Tempo meaning has its own validity */
#define AZ_LIVE_TEMPO_SLIDER_LOCKED 131072u /* native slider-lock setting, not playback rate */
#define AZ_LIVE_NATIVE_LOAD 262144u /* matched native load completion, not a track/source association */
#define AZ_LIVE_CUE_REVISION 524288u /* GUI-collected native cue-set invalidation, not saved-file data */
/* Native playback states; the legacy deck_info.playing movement heuristic is not used. */
enum { AZ_PLAYBACK_UNKNOWN, AZ_PLAYBACK_PAUSED, AZ_PLAYBACK_PLAYING, AZ_PLAYBACK_CUEING };
typedef struct az_deck_snapshot {
    uint32_t size, version, valid, deck;
    az_track_ref track;
    uint64_t load_generation, revision, observed_ns;
    int32_t loaded, playback, direction, key_shift;
    int32_t master_tempo, sync, master, quantize, slip, reserved;
    double position_s, rate, tempo, slip_position_s;
    az_loop_state loop;
    az_cue_entry main_cue;
    /* Separate observation stream: receipt time and player UNIT lifetime, not track load.
     * The loop notification does not establish a source-qualified track association. */
    uint64_t loop_unit_generation, loop_revision, loop_observed_ns;
    /* Independent unit-scoped receipts, with no track-load association. Play flags
     * are copied only when native play_pause changes; mode/aux can change without
     * a notification. Unknown scalars are -1. Each stream has its own validity bit. */
    uint64_t live_unit_generation;
    uint64_t play_revision, play_observed_ns;
    uint64_t quantize_revision, quantize_observed_ns;
    uint64_t key_lock_revision, key_lock_observed_ns; /* also the Master Tempo receipt */
    int32_t play_mode_is_2, play_pause, play_aux, key_lock;
    /* Native Sync enabled setting, independent of beat lock/master selection.
     * Mode0=off,2=on; other observed modes have no AZ_LIVE_SYNC validity. */
    uint64_t sync_unit_generation, sync_revision, sync_observed_ns;
    int32_t sync_raw_mode;
    uint32_t sync_reserved;
    uint64_t tempo_lock_unit_generation, tempo_lock_revision, tempo_lock_observed_ns;
    int32_t tempo_slider_locked;
    uint32_t tempo_lock_reserved;
    /* Native gui::BpmState tempo-adjusted BPM in tempo (conservative20..400 range).
     * Independent GUI poll receipts; state lifetime is not a track/load identity.
     * No playback rate/direction or Master Tempo/Sync implication. */
    uint64_t tempo_state_generation, tempo_revision, tempo_observed_ns;
    /* Confirmed MasterPlayer post-exchange event, shared by all local decks.
     * master=0/1 is valid only with AZ_LIVE_MASTER; no event/foreign or unknown
     * member yields -1. This revision is not a player-unit or track-load ID. */
    uint64_t master_revision, master_observed_ns;
    /* native_load_generation is usable only with AZ_LIVE_NATIVE_LOAD. Pending,
     * failed, unloaded or retired units clear it. The receipt's unit/epoch/time
     * does not qualify track, source, metadata, LTI revision or playback and
     * can target transient EXIT/RELOOP with AZ_COMMAND_NATIVE_LOAD. The existing
     * source-qualified load_generation remains unavailable until that join. */
    uint64_t native_load_generation, native_load_unit_generation, native_load_invalidation_epoch, native_load_observed_ns;
    /* Collected native cue-set changes. This counter/time belongs to the cue
     * listener's player registration and GUI collection, not native edit time,
     * saved cue identity, LTI revision, persistence, track or load association. */
    uint64_t cue_unit_generation, cue_revision, cue_observed_ns;
} az_deck_snapshot;
typedef struct az_data_capabilities {
    uint32_t size, version, stored_components, live_fields;
    uint64_t commands; /* bit operation; zero until native state and dispatch are verified */
    uint32_t max_requests_per_mod, max_snapshots, max_wave_samples, reserved;
} az_data_capabilities;
#define AZ_INTERVAL_SOURCE 1u
#define AZ_INTERVAL_BEATS 2u
#define AZ_INTERVAL_BARS 4u
#define AZ_INTERVAL_PLAYBACK 8u
typedef struct az_time_interval {
    uint32_t size, version, valid, reserved;
    double source_s, beats, bars, playback_s, playback_rate;
} az_time_interval;
enum { AZ_CHANGE_TRACK_DATA = 1, AZ_CHANGE_INVALIDATED, AZ_CHANGE_COMMAND, AZ_CHANGE_DECK };
typedef struct az_data_change {
    uint32_t size, version, kind, fields;
    az_track_ref track;
    uint64_t ticket, revision, sequence, observed_ns;
    int32_t status, missed;
} az_data_change;
enum { AZ_CMD_LOOP_IN = 1, AZ_CMD_LOOP_OUT, AZ_CMD_LOOP_ENTER, AZ_CMD_LOOP_EXIT,
       AZ_CMD_RELOOP, AZ_CMD_LOOP_CLEAR, AZ_CMD_LOOP_DOUBLE, AZ_CMD_LOOP_HALVE,
       AZ_CMD_LOOP_LENGTH, AZ_CMD_LOOP_MOVE, AZ_CMD_LOOP_ADJUST_IN, AZ_CMD_LOOP_ADJUST_OUT,
       AZ_CMD_SAVED_RECALL, AZ_CMD_SAVED_SAVE, AZ_CMD_SAVED_DELETE, AZ_CMD_SAVED_AUTO,
       AZ_CMD_HOT_TRIGGER, AZ_CMD_HOT_SET, AZ_CMD_HOT_CLEAR, AZ_CMD_MEMORY_RECALL,
       AZ_CMD_MEMORY_SET, AZ_CMD_MEMORY_DELETE, AZ_CMD_MAIN_SET, AZ_CMD_MAIN_RECALL,
       AZ_CMD_CUE_COMMENT, AZ_CMD_CUE_COLOR };
enum { AZ_QUANTIZE_NATIVE, AZ_QUANTIZE_OFF, AZ_QUANTIZE_ON };
enum { AZ_COMMAND_ACCEPTED, AZ_COMMAND_APPLIED, AZ_COMMAND_REJECTED,
       AZ_COMMAND_CANCELLED, AZ_COMMAND_UNCERTAIN };
#define AZ_COMMAND_PERSIST 1u
/* Transient native loop commands (IDs LOOP_IN through LOOP_ADJUST_OUT).
 * expected_load_generation names native_load_generation; expected_revision=0,
 * quantization=NATIVE, PERSIST disallowed. IN/OUT mark the current playhead;
 * ENTER uses native reloop (seek to retained IN). CLEAR resets the default
 * retained cue, not working engagement. Other unused arguments must be zero.
 * LENGTH takes positive beats exactly representable as a uint16 rational.
 * MOVE and ADJUST_IN use position_s as signed seconds delta; ADJUST_OUT uses
 * end_s. MOVE is two ordered boundary edits; partial execution is uncertain,
 * without rollback. IN adjustment/MOVE require the default selected cue and
 * can update its transient retained points. Native adjustments may seek.
 * Deferred Slip/Sync/Quantize work remains pending until correlated settlement.
 * EXIT is idempotent; ENTER/RELOOP already engaged yield uncertainty because
 * engagement alone cannot confirm a seek. Firmware acceptance is separate. */
#define AZ_COMMAND_NATIVE_LOAD 2u
/* Native cue commands use quantization=NATIVE and NATIVE_LOAD. MAIN_SET,
 * MAIN_RECALL and HOT_TRIGGER are transient: no PERSIST or expected_revision.
 * MAIN_SET marks the playhead; MAIN_RECALL seeks and pauses; HOT_TRIGGER seeks
 * and plays. Hot slots are 1..8. HOT_SET captures the current playhead/working
 * loop with rgb; HOT_CLEAR clears and deletes the selected slot.
 * Storage writes require PERSIST. MEMORY_SET saves the retained main cue;
 * SAVED_SAVE requires its complete loop. Recall/delete/auto and comment/color
 * select native_cues entries, require NATIVE_CUE_IDS and its exact revision.
 * File snapshot entry IDs cannot be used here. Memory IDs are snapshot-local;
 * hot metadata uses its slot ID with the same revision fence.
 * SAVED_AUTO position_s is exactly 0 (off) or 1 (on). Enabling disables other
 * active saved loops through ordered writes under the original deadline.
 * CUE_COMMENT copies UTF-8 through the first NUL (at most 127 bytes).
 * CUE_COLOR takes 24-bit rgb. Hot cues support arbitrary RGB; memory cues use
 * the native palette: 000000 (default), ff00c6, ff0000, ff7100, ffff00, 00e300,
 * 00c3ff, 0051ff, 9c08ff. Other memory colors are unsupported before writing.
 * Native memory storage holds at most ten cues per track.
 * Other unused arguments are zero. Saves settle only after native storage
 * completion and observation of the requested effect; partial edits can be
 * UNCERTAIN and are not rolled back. */
/* Select entry_id from native_cues, fenced by its exact revision. */
#define AZ_COMMAND_NATIVE_CUE_IDS 4u
#define AZ_NATIVE_CUE_LIMIT 64u
typedef struct az_native_cue_snapshot {
    uint32_t size, version, deck, count;
    uint64_t native_load_generation, revision;
    az_cue_entry entries[AZ_NATIVE_CUE_LIMIT];
} az_native_cue_snapshot;
typedef struct az_command_request {
    uint32_t size, version, operation, deck;
    uint64_t request_id, expected_load_generation, expected_revision, deadline_ns;
    uint32_t quantization, flags, classification, entry_id;
    double position_s, end_s, beats;
    uint32_t rgb, reserved;
    char comment[128];
} az_command_request;
typedef struct az_command_result {
    uint32_t size, version, outcome;
    int32_t reason; /* AZ_* status */
    uint64_t request_id, revision, observed_ns;
} az_command_result;

/* ---- services the core hands every mod at init() ------------------------------------
 * Append-only: a mod built against a newer core checks AZ_HAS(core, field) before calling
 * an entry this core may not have. Every deck number is 0..3, and -1 means unknown: never
 * act on a guessed deck. Services that take an `id` take your mod id, and the core checks
 * that the call comes from your library's code; a call made with another mod's id is
 * refused. */
/* One shared library service. All consumers use the same source identities, bounded cache,
 * snapshots, event journal and native command policy. No per-mod data backend is created. */
#define AZ_LIBRARY_ABI 1u
typedef struct az_library_v1 {
    uint32_t abi, size;
    int  (*lib_playlists)(const char *id, int stick,
                          int (*fn)(int pl_id, int parent, const char *name, int folder,
                                    void *user), void *user);
    int  (*lib_tracks)(const char *id, int stick, int playlist_id,
                       int (*fn)(const az_track *t, void *user), void *user);
    int (*data_capabilities)(az_data_capabilities *out);
    int (*track_resolve)(const char *id, int stick, uint32_t track_id, az_track_ref *out);
    int (*track_request)(const char *id, const az_track_ref *track, uint32_t components,
                         uint64_t *ticket);
    int (*track_acquire)(const char *id, uint64_t ticket, const az_track_snapshot **out);
    int (*track_release)(const char *id, uint64_t ticket);
    int (*track_cancel)(const char *id, uint64_t ticket);
    int (*data_event)(const char *id, uint64_t after_sequence, az_data_change *out);
    int (*deck_snapshot)(int deck, az_deck_snapshot *out);
    int (*time_to_beat)(const az_track_snapshot *snapshot, double time_s, double *beat);
    int (*beat_to_time)(const az_track_snapshot *snapshot, double beat, double *time_s);
    int (*time_interval)(const az_track_snapshot *snapshot, double start_s, double end_s,
                         double playback_rate, az_time_interval *out);
    int (*command_submit)(const char *id, const az_command_request *request,
                          az_command_result *out);
    int (*waveform_request)(const char *id, const az_track_ref *track,
                            const az_wave_query *query, uint64_t *ticket);
    int (*waveform_acquire)(const char *id, uint64_t ticket, const az_wave_snapshot **out);
    /* Accepted request IDs increase per mod and are never reused, including after
     * release. Retained exact retries return the current immutable result. */
    int (*command_poll)(const char *id, uint64_t request_id, az_command_result *out);
    int (*command_cancel)(const char *id, uint64_t request_id, az_command_result *out);
    int (*command_release)(const char *id, uint64_t request_id);
    /* Copied native saved cues; nonblocking. Memory IDs belong only to this revision. */
    int (*native_cues)(int deck, az_native_cue_snapshot *out);
} az_library_v1;
#define AZ_LIB_HAS(service, field) \
    ((service) && (service)->abi == AZ_LIBRARY_ABI && \
     (service)->size >= offsetof(az_library_v1, field) + sizeof((service)->field) && (service)->field)

typedef struct az_core_v1 {
    int abi;
    int size;          /* sizeof(az_core_v1) at the core's build */

    /* Deck identity for EP147's objects; -1 = unknown. */
    int  (*deck_for_ds)(void *ds);        /* a DeckState* -> 0..3, or -1 */
    int  (*deck_for_modeobj)(void *obj);  /* a pad-mode / widget object -> 0..3, or -1 */

    /* Hooks: one address, one owner. Only for mods whose azmod.json says
     *    "hooks": true and that list this firmware's tag (never for "any" mods).
     *    From init() only. Returns 0 on success; non-zero = refused (already owned, not
     *    allowed, or the installer declined), with the reason in the mod's log. `id` = your
     *    mod id. */
    int  (*claim_hook)(const char *id, uintptr_t addr, void *hook, void **orig);

    /* Settings, per mod, stored for you (settings/azmod-community/<id>.cfg). Ints only;
     *    booleans are 0/1. cfg_set is cheap (written later, off your thread). */
    int  (*cfg_get)(const char *id, const char *key, int def);
    void (*cfg_set)(const char *id, const char *key, int val);

    /* Log: one line per call, flushed (to /tmp/azmod-community/<id>.log, size-capped;
     *    only written while the DJ has logs on, except the core's own lines about your
     *    mod: the load result and any refused claim_hook or service call). */
    void (*log)(const char *id, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

    /* Firmware: the running EP147's tag, "ep147-<16 hex>": a hash of EP147's code, so it
     *    is the same on the emulator and a deck running the same firmware, and changes
     *    whenever the code your addresses point into changes. Branch on it if one build of
     *    your mod supports several firmware versions. */
    const char *(*fw_tag)(void);

    /* Your page in the Mod Menu. Call from init(). */
    int  (*register_screen)(const char *id, const az_screen_v1 *scr);

    /* Drawing, from on_draw only, in absolute device pixels, clipped to your area. Colors
     *    are 0xRRGGBB. draw_text's y is the top of a px*3/2 tall line box; px is limited to
     *    8..96. draw_header takes the first row of your area. Screen callbacks (on_draw,
     *    on_input, on_enter) run on the same thread as tick. The az_draw you are handed is
     *    valid only inside that on_draw (or on_paint) call: one kept and used later draws
     *    nothing. */
    void (*draw_header)(az_draw *d, const char *title);
    void (*draw_text)(az_draw *d, int x, int y, const char *s, int px, uint32_t rgb);
    void (*fill_rect)(az_draw *d, int x, int y, int w, int h, uint32_t rgb);
    void (*draw_rect)(az_draw *d, int x, int y, int w, int h, int width, uint32_t rgb);
    void (*draw_checkbox)(az_draw *d, int x, int y, int size, int on, int highlighted);
    uint32_t (*color)(int role);          /* AZ_COL_* in the active theme */
    int  (*row_height)(az_draw *d);       /* a standard Mod Menu row, for consistent layout */
    int  (*text_width)(const char *s, int px);
    void (*request_redraw)(void);         /* your page changed; repaint soon */

    /* Read-only deck state (no reverse engineering needed). -1 / 0 / "" = unknown. */
    int    (*deck_count)(void);                    /* decks in the current view: 2 or 4 */
    int    (*deck_loaded)(int deck);               /* 1 = a track is loaded */
    double (*deck_bpm)(int deck);                  /* current BPM, or 0 if unknown */
    int    (*deck_title)(int deck, char *buf, int n);  /* the loaded file's name without extension when
                                                       * known, else the track's title; n includes the
                                                       * terminating zero; returns length, 0 = unknown */
    int    (*ui_theme)(void);                      /* 0 = themes off (stock), 1 = Day (light),
                                                    * 2..5 = AZ-MODS, Midnight, Ember, Forest
                                                    * (dark) */

    /* ---- added in core 1.2.3 (check with AZ_HAS before calling) ---- */

    /* Text settings, beside the numbers above (same file). Up to 127 bytes, one line.
     *    get copies into buf (n includes the terminating zero) and returns the length. */
    int  (*cfg_get_str)(const char *id, const char *key, char *buf, int n, const char *def);
    void (*cfg_set_str)(const char *id, const char *key, const char *val);

    /* Your files, on the USB stick your mod was installed from. The deck's own storage
     *    is small and shared (see the guide), the stick has room. Both return an absolute
     *    path with no trailing slash, or NULL when that stick is not in the deck right now
     *    (your mod keeps running from the deck's copy after an eject; carry on without your
     *    files). Call them each time you need a file instead of keeping the pointer, and
     *    never create these folders yourself (data_dir does; after an eject the empty mount
     *    point stays on the deck, so a folder made there lands on the deck itself).
     *    data_dir:   <stick>/MODS/community-mods/<id>/data, yours to read and write (created for
     *                you; kept when your mod is updated or reinstalled, deleted with it when it is
     *                removed; never part of a package). It belongs
     *                to the repository your mod was installed from: a mod with the same id
     *                from another repository gets an empty folder, never yours.
     *    assets_dir: <stick>/MODS/community-mods/<id>/assets, the assets/ you shipped (NULL if
     *                none). Treat it as read-only: an update replaces the whole folder.
     *    They touch the disk: call them from init() or when you need a file, never from
     *    on_frame/on_draw. Open, read or write, close: a file left open on the stick holds
     *    up the deck's eject. */
    const char *(*data_dir)(const char *id);
    const char *(*assets_dir)(const char *id);

    /* Mod Menu rows (types above). From init() only. register_row returns a handle (>= 0)
     *    or -1 with the reason in your log (bad key, unknown parent, over the limit of 16 rows
     *    per mod). row_value is one atomic load, safe from any thread: TOGGLE = its own value
     *    and every parent's, CHOICE = the index, -1 = bad handle. row_set changes one of your
     *    own rows as if the DJ had (UI thread only; another mod's row is refused). */
    int  (*register_row)(const char *id, const az_row_v1 *row);
    int  (*row_value)(int handle);
    int  (*row_set)(int handle, int value);
    /* A read-only handle to an official row by its azmod.conf key (e.g. "fps60-waveforms"),
     * for row_value only; -1 = no such row. The official keys are listed in the guide
     * (docs/MOD-MENU-KEYS.md). */
    int  (*row_lookup)(const char *official_key);

    /* Frame filters. fn sees every frame of the buses in bus_mask (1u << AZ_BUS_*), on the
     *    bus thread, after the core's own handling and the filters of mods loaded before you.
     *    Same rules as on_frame: quick, no blocking, log() is queued. AZ_BUS_MIXER_LED and
     *    AZ_BUS_DECK_IN hand you a copy (edits and the return value are ignored). Input
     *    frames reach you only if they arrived with a valid CRC; edit in place and return
     *    AZ_FRAME_OK, or AZ_FRAME_EATEN to stop the chain; the core recomputes the CRC once.
     *    Each call is timed: a filter that overruns repeatedly is switched off until EP147
     *    restarts, and its frames go to EP147 untouched (a fault stops EP147 like any crash
     *    in your code, and your mod is switched off at the next start). A filter on two buses
     *    can be called from two threads at once. With AZ_FILTER_RAW in bus_mask you see
     *    input frames before the core edits them, read-only (the return is ignored).
     *    From init() only; 0 = registered. */
    int  (*frame_filter)(const char *id, unsigned bus_mask, az_frame_fn fn, void *user);

    /* Touch. fn is offered the first contact (slot 0) before EP147 sees it (bus thread
     *    rules, timed like a frame filter). Taking or passing is decided when the finger comes
     *    down and holds until it lifts: EP147 is never left with half a touch. A taken touch
     *    stays yours (blanked for EP147) until it lifts, even if your filter is switched off.
     *    pointer_inject sends real pointer input to the X server (for another program's
     *    window you show with xwin_show), delivered on the overlay thread. From init() only
     *    for touch_filter; up to 16 in all, offered in load order until one takes the touch. */
    int  (*touch_filter)(const char *id, az_touch_fn fn, void *user);
    /* button: 0 = move only, 1 left, 2 middle, 3 right; down: 1 press, 0 release.
     * 0 = queued; -1 = refused (the X test extension is missing, or the queue is full). */
    int  (*pointer_inject)(int x, int y, int button, int down);
    void (*pointer_cursor)(int visible);   /* show or hide the X pointer */

    /* What EP147 shows (any thread, one atomic load), and your surfaces. The core owns
     *    the X connection, stacking and repaint timing; on_paint runs on the overlay thread
     *    when a repaint is due. surface_create from init() only; the rest from the UI thread.
     *    surface_focus gives your surface the browse knob, its click and BACK (EP147's list
     *    holds still, as it does for the Mod Menu); 1 = granted, 0 = refused (the Mod Menu is
     *    open, or another surface has focus). Returning 0 from on_input for AZ_EV_BACK gives
     *    the knob back to EP147. Up to 4 surfaces per mod (16 in all), and 8 windows shown
     *    with xwin_show at once. */
    int         (*view)(void);
    az_surface *(*surface_create)(const char *id, const az_surface_v1 *s);
    void        (*surface_place)(az_surface *s, az_rect r);
    void        (*surface_show)(az_surface *s, int on);
    void        (*surface_redraw)(az_surface *s);
    int         (*surface_focus)(az_surface *s, int on);
    /* Another program's X window (one you launched): shown at r above EP147 while on, below
     * AZ_LAYER_TOP surfaces. A width or height of 0 in r keeps the window's own size (only its
     * position is set): for a program that resizes its own window. The core absorbs that
     * window's X errors. 0 = done. xwin_find looks for a top-level window of a program you
     * launched (pid), whose title starts with prefix and that is at least min_w x min_h;
     * 0 = found (*xid set). xwin_geometry reports its size. */
    int  (*xwin_show)(const char *id, unsigned long xid, az_rect r, int on);
    int  (*xwin_find)(const char *id, int pid, const char *title_prefix, int min_w, int min_h,
                      unsigned long *xid);
    int  (*xwin_geometry)(const char *id, unsigned long xid, az_rect *out);
    /* An RGBA image (premultiplied, w*h*4 bytes), and a line, from on_draw / on_paint only.
     * Both are clipped to your area. draw_image: 0 = drawn, -1 = refused (NULL, or not called
     * from on_draw / on_paint). draw_line's width is 1..32 pixels (clamped). */
    int  (*draw_image)(az_draw *d, int x, int y, const unsigned char *rgba, int w, int h);
    void (*draw_line)(az_draw *d, int x0, int y0, int x1, int y1, int width, uint32_t rgb);

    /* Deck state. deck_info fills a snapshot (set out->size first); any thread, never
     *    blocks; 0 = filled, -1 = no such deck. Each field is read atomically, but the fields
     *    are not one snapshot of the same instant. deck_grid gives the playhead's phase in the
     *    beat, 0..1, and in the bar, or -1 where the grid does not say (EP147's grid has no
     *    bar position, so *bar_phase is -1 today); returns -1 = no grid.
     *    on_deck_event calls fn on the UI thread when a deck loads, ejects, plays, stops,
     *    or its native cue set changes (including load/reset, not only user edits).
     *    (AZ_DECK_*); from init() only, up to 4 per mod. */
    int  (*deck_info)(int deck, az_deck_info *out);
    int  (*deck_grid)(int deck, double *beat_phase, double *bar_phase);
    int  (*on_deck_event)(const char *id, void (*fn)(int deck, int what, void *user), void *user);

    /* Feedback and threads. "The UI thread" is the core's thread that runs tick, screens,
     *    surfaces and row callbacks. banner shows text (up to 111 bytes) in EP147's own
     *    caution box for ms milliseconds. confirm asks a yes/no question in a dialog over
     *    whatever is on screen, Cancel focused first, BACK = no; fn runs on the UI thread with
     *    the answer. It returns 0 when shown, -1 when refused (another question is open).
     *    post_ui runs fn on the UI thread soon, from any of your threads (not from on_frame or a
     *    filter: there, set an atomic and read it in tick); fn must be in your code.
     *    thread_start starts a normal-priority thread of your own, with an alternate signal
     *    stack, named in the crash log; up to 4 per mod; 0 = started. A thread cannot be
     *    stopped by the core: end it yourself, and keep it out of on_frame's state. */
    void (*banner)(const char *id, const char *text, int ms);
    int  (*confirm)(const char *id, const char *title, const char *text, const char *yes,
                    void (*fn)(int yes, void *user), void *user);
    void (*post_ui)(const char *id, void (*fn)(void *user), void *user);
    int  (*thread_start)(const char *id, const char *name, void *(*fn)(void *), void *arg);

    /* Other programs, never a copy of EP147. `path` is inside your assets_dir or your
     *    data_dir (anything else is refused). The program starts without the mods' preload, in
     *    its own systemd scope outside EP147's, with opts applied (NULL = defaults). launch
     *    returns its pid (-1 = refused); launch_alive and launch_stop work only on pids you
     *    launched (stop = SIGTERM, then SIGKILL after 2 s). handoff hands the screen and
     *    controls to the program and brings EP147 back when it exits: refused while a track
     *    plays, and the DJ confirms first. Holding BACK for 3 seconds ends the program and
     *    returns to the deck, whatever the program does (it gets SIGTERM, then SIGKILL 2 s
     *    later); the core's confirm dialog says so. If EP147 does not come back up, the core
     *    tries once more, then restarts the deck. The log at opts->log_path is capped at 1 MB
     *    (rotated once). argv holds up to 32 entries. reload is the Mod Menu's Reload, refused
     *    while any track is loaded. */
    int  (*launch)(const char *id, const char *path, const char *const argv[],
                   const az_launch_opts *opts);
    int  (*launch_alive)(const char *id, int pid);
    int  (*launch_stop)(const char *id, int pid);
    int  (*handoff)(const char *id, const char *path, const char *const argv[]);
    void (*reload)(const char *id);

    /* The rekordbox library on a stick, read-only. stick 0 = the deck's USB 1 port,
     *    1 = USB 2. The callbacks run on your thread, from inside the call; return nonzero to
     *    stop. They read the stick: call them from your own thread (thread_start), never from
     *    the UI thread, on_frame or on_paint. 0 = done, -1 = no library or refused, 1 = done
     *    but the list was cut at 20,000 entries (one call never copies more).
     *    lib_playlists calls fn once per playlist and folder, grouped by parent, in the DJ's
     *    order within each folder; an entry whose parent is no pl_id it reports is at the top
     *    level. lib_tracks lists a playlist's tracks in the DJ's order. */
    int  (*lib_playlists)(const char *id, int stick,
                          int (*fn)(int pl_id, int parent, const char *name, int folder,
                                    void *user), void *user);
    int  (*lib_tracks)(const char *id, int stick, int playlist_id,
                       int (*fn)(const az_track *t, void *user), void *user);

    /* Audio, read-only: fn gets interleaved stereo float blocks of `frames` frames of one
     *    deck's output (AZ_TAP_DECK(n), before the hardware mixer), on a core thread, never the
     *    audio thread. A slow fn loses blocks (counted in your log); it never delays the audio.
     *    frames is 64..8192. Up to 4 taps per mod, 16 in all; 0 = tapping.
     *    Filters, touch filters, deck-event listeners and taps stay registered until EP147
     *    restarts, and threads run until they return: give each a way to go idle (a flag
     *    your tick or your settings clear) instead of expecting to remove it. */
    int  (*audio_tap)(const char *id, int source, int frames,
                      void (*fn)(const float *lr, int frames, void *user), void *user);

    /* A virtual method hook: like claim_hook, for one vtable slot (the slot's address).
     *    The core checks the slot holds a function in EP147's code first. "hooks": true only. */
    int  (*claim_vslot)(const char *id, uintptr_t slot, void *fn, void **orig);

    /* The library spool: rows for a stick's rekordbox library, written by a program of yours
     *    as <stick>/PIONEER/azspool/<name>.sql (write <name>.sql.tmp, then rename). Each line is
     *    one INSERT [OR IGNORE] INTO or UPDATE statement on the library tables (content, artist,
     *    album, genre, key, image, playlist, playlist_content, label); blank and "--" lines are
     *    skipped. The core applies a file as one transaction on the deck program's own database
     *    connection, then the lists on screen re-read it: no eject, no restart. A file with any
     *    other statement is renamed .rejected, one that fails is rolled back and renamed .failed,
     *    an applied one is deleted. By itself the core applies spools only for its own copies;
     *    lib_spool_kick asks it to apply what is on the sticks now (stick 0 = USB 1, 1 = USB 2).
     *    Call it from your own thread (it watches the decks for 250 ms). The core then applies
     *    every stick's spool, so it is refused, with the reason in your log, while a deck plays
     *    a track from that stick, from any stick with a spool waiting, or from one it cannot
     *    place. 0 = asked. */
    int  (*lib_spool_kick)(const char *id, int stick);
    /* ---- Track data v1: development services, use AZ_HAS and capability checks ----
     * capabilities/deck_snapshot are bounded copies, any thread. track_resolve performs
     * file/DB work: a mod worker only. Other track calls try-lock and return AZ_BUSY on
     * contention, with no I/O. Never call them from an audio or frame callback.
     * At most four outstanding tickets per mod. Release every ticket, also after cancel.
     * track_acquire returns AZ_PENDING until published. A released ticket is always stale.
     * data_event polls an ordered, bounded journal; missed=1 requires reacquiring state.
     * track_cancel only cancels this consumer, never another consumer's shared load. */
    int (*data_capabilities)(az_data_capabilities *out);
    int (*deck_snapshot)(int deck, az_deck_snapshot *out);
    int (*track_resolve)(const char *id, int stick, uint32_t track_id, az_track_ref *out);
    int (*track_request)(const char *id, const az_track_ref *track, uint32_t components,
                         uint64_t *ticket);
    int (*track_acquire)(const char *id, uint64_t ticket, const az_track_snapshot **out);
    int (*track_release)(const char *id, uint64_t ticket);
    int (*track_cancel)(const char *id, uint64_t ticket);
    int (*data_event)(const char *id, uint64_t after_sequence, az_data_change *out);
    int (*time_to_beat)(const az_track_snapshot *snapshot, double time_s, double *beat);
    int (*beat_to_time)(const az_track_snapshot *snapshot, double beat, double *time_s);
    int (*time_interval)(const az_track_snapshot *snapshot, double start_s, double end_s,
                         double playback_rate, az_time_interval *out);
    int (*command_submit)(const char *id, const az_command_request *request,
                          az_command_result *out);
    /* Shares track tickets, quotas, cancellation and data_event. Request on a mod worker;
     * acquire/release are nonblocking. Unknown encodings return AZ_UNSUPPORTED. */
    int (*waveform_request)(const char *id, const az_track_ref *track,
                            const az_wave_query *query, uint64_t *ticket);
    int (*waveform_acquire)(const char *id, uint64_t ticket, const az_wave_snapshot **out);
    /* Preferred entry point for the shared data/read/control service. Unsupported ABI -> NULL.
     * The individual development data entries above remain compatibility aliases. */
    const az_library_v1 *(*library_service)(uint32_t abi);

} az_core_v1;

#define AZ_HAS(core, field) \
    ((core) && (size_t)(core)->size >= offsetof(az_core_v1, field) + sizeof((core)->field) && (core)->field)

/* ---- your mod's descriptor --------------------------------------------------------
 * Define exactly one, and export exactly one symbol returning it (below). */
typedef struct az_mod_v1 {
    int         abi;       /* = AZ_MOD_ABI */
    int         size;      /* = sizeof(az_mod_v1) */
    const char *id;        /* the same id as azmod.json: [a-z0-9-], 3..32 chars, stable */
    /* The EP147 builds you tested on, NULL-terminated: { "ep147-...", "ep147-...", NULL }.
     * Or { "any", NULL } for a mod that uses only the services above and claims no hooks:
     * it then loads on every firmware, and claim_hook is refused for it. */
    const char *const *fw_tags;

    /* The core is up and its services are live. Claim hooks, register your screen and read
     * your settings here, never in a C constructor. Return 0 to stay loaded. Anything else
     * and the core never calls you again and shows the number as the reason. Your library is
     * never unloaded (a thread or pointer you left behind may still point into it), and a
     * hook you already claimed stays live until the next start. So claim hooks last, after
     * everything that can fail. */
    int  (*init)(const az_core_v1 *core);

    /* Periodically, up to about 30 times a second (the rate varies; time things with a clock),
     * on the core's UI thread. Bookkeeping only: no blocking, no file or network I/O, no big
     * allocations. May be NULL. */
    void (*tick)(void);

    /* The pad / mode-button LED frame (AZ_BUS_ERP_LED, the only bus on_frame receives; see
     * frame_filter for the others). Called on the deck's I/O path, at real-time
     * priority: be quick and never block. From here log() is queued and written by the core
     * later; the settings, file-folder and register_screen services are refused (they return
     * the default, or NULL / -1). May be NULL. */
    int  (*on_frame)(int bus, unsigned char *frame, int len);
} az_mod_v1;

/* Export exactly this. Build with -fvisibility=hidden; this attribute keeps the one
 * required symbol visible so the core can find it. */
#define AZ_MOD_EXPORT __attribute__((visibility("default")))
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void);
/* clang-format on */

#ifdef __cplusplus
}
#endif

#endif /* AZMOD_MOD_H */
