# API reference (ABI version 1)

Everything a mod can use is declared in [`include/azmod-mod.h`](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/include/azmod-mod.h),
along with what each piece does, where it may be called from and how fast it must return.
Where the header's comments and this page disagree, the header is correct; an issue on the SDK
repository gets the page fixed. For crash-safety rules, see [Guidelines](GUIDELINES.md).

- [Threads and timing](#threads-and-timing)
- [Your descriptor: `az_mod_v1`](#your-descriptor-az_mod_v1)
- [Core services: `az_core_v1`](#core-services-az_core_v1)
  - [Rows in the Mod Menu](#rows-in-the-mod-menu), [control frames](#control-frames-filters),
    [touch](#touch-and-pointer), [surfaces](#what-is-on-screen-and-surfaces),
    [deck state](#deck-state-and-events), [threads](#feedback-and-threads),
    [other programs](#other-programs), [the library](#the-library-on-a-usb),
    [audio taps](#audio-taps)
- [Your page: `az_screen_v1`](#your-page-az_screen_v1)
- [Control frames: `on_frame`](#control-frames-on_frame)
- [Versioning and `AZ_HAS`](#versioning-and-az_has)

## Threads and timing

| Callback | Called | Thread | Budget |
|---|---|---|---|
| `az_mod_v1_entry()` | once, right after the core loads the library | the core's UI thread | return a pointer to a static struct, nothing else |
| `init(core)` | once, before any other callback | the core's UI thread | setup only; keep it under a few milliseconds |
| `tick()` | periodically, up to about 30 times a second; the rate varies | the core's UI thread | well under 1 ms |
| `on_draw`, `on_input`, `on_enter` | while your page is the active Mod Menu screen | the core's UI thread | `on_draw` well under a frame (a few ms) |
| `on_frame(bus, frame, len)` | for each control-surface frame | the deck's I/O path | microseconds |
| frame and touch filters | for each frame or touch report | the deck's I/O path | microseconds; a filter over 200 µs on 8 calls is switched off for the session |
| row callbacks, surfaces (`on_paint`, `on_input`), `confirm` and `post_ui` callbacks, deck events | when they happen | the core's UI thread | well under 1 ms |
| audio tap callbacks | for each block of a deck's audio | the core's tap thread (not the audio thread) | under the block's duration |
| threads you start (`thread_start`) | whenever you like | your own | no limit, but don't make the deck wait |
| hooks you claim | whenever the deck runs the hooked code | whichever thread the deck uses | as little as possible |

`tick()` is optional and may be `NULL`. Its rate varies, so time anything with a monotonic
clock (`clock_gettime(CLOCK_MONOTONIC, ...)`), not by counting ticks. A mod can also run
entirely from callbacks it registers in `init` (deck events, filters, surfaces, `post_ui`, its
own threads); see [How a mod runs](03-first-mod.md#5-how-a-mod-runs).

Rules that follow from this table:

- `tick`, the screen, row and surface callbacks and `init` all run on the core's UI
  thread, one at a time, so state shared only between them needs no locking.
- `on_frame`, filters, audio taps, your own threads and hooks run on other threads. State
  they share with `tick` or your page must be a plain word read and written with atomics
  (`__atomic_load_n`, `__atomic_store_n`). Don't hold a lock for long, or share a
  half-updated structure.
- Which services you can call from where:

  | From | Settings (`cfg_*`), `data_dir`, `assets_dir` | `log` | Drawing |
  |---|---|---|---|
  | `init`, `tick`, page and surface callbacks | yes | yes | only in `on_draw` / `on_paint` |
  | a thread the mod started | yes (they are thread-safe) | yes | no |
  | `on_frame`, frame and touch filters | refused (the control bus must not block) | queued: 8 lines per mod, written at the next tick; more are dropped and counted | no |
  | a hook | avoid: it runs on EP147's own threads | on a change only | no |

- Don't block, sleep, do file or network I/O, or allocate heavily in any callback
  ([Guidelines](GUIDELINES.md#3-keep-the-busy-paths-fast)).
- Mods are loaded once, when EP147 starts, and aren't unloaded. If `init` returns
  non-zero, the core stops calling the mod but the library stays mapped. There is no shutdown
  callback.
- The core loads up to 16 mods: the first 16 `community-mods.<id> = on` lines in `MODS/azmods.cfg`.

## Your descriptor: `az_mod_v1`

Define one as a `static const` global and return it from `az_mod_v1_entry()`.

```c
static const char *const FW_TAGS[] = { "any", NULL };
static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI, .size = sizeof(az_mod_v1), .id = "beat-counter",
    .fw_tags = FW_TAGS, .init = init, .tick = tick, .on_frame = NULL,
};
const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
```

- `abi`: `AZ_MOD_ABI`. The core refuses a mod built for another ABI.
- `size`: `sizeof(az_mod_v1)`. Lets a newer core read an older mod safely.
- `id`: the same `id` as in `azmod.json`: 3 to 32 characters from `a-z`, `0-9` and `-`.
  Keep it stable for the life of the mod: it prefixes your settings in `MODS/azmods.cfg` and
  names your log.
- `fw_tags`: a `NULL`-terminated list of the firmware tags you tested on, or
  `{ "any", NULL }` for a mod that uses only core services. It must agree with `"firmware"`
  in `azmod.json`. See [Hooks and firmware tags](05-hooks-and-firmware.md).
- `init`: called once with the core's services. Claim hooks, register the page and read
  settings here.
    - Return `0` to stay loaded. Any other value means the core doesn't call the mod again
      and shows the number as the reason (`Its init() returned 2`).
    - The library isn't unloaded, because a thread or pointer it left behind may still point
      into it. The `core` pointer stays valid for the life of the process.
    - **Claim hooks last**, after everything else in `init` that can fail
      ([why](05-hooks-and-firmware.md#claim-from-init-last)).
- `tick`: called periodically, up to about 30 times a second at a varying rate; time with a
  monotonic clock, not by counting calls. For bookkeeping only. May be `NULL`.
- `on_frame`: control-surface frames (below). May be `NULL`.

`az_mod_v1_entry` is the only exported symbol. The header declares it with `AZ_MOD_EXPORT`,
so a definition stays visible under `-fvisibility=hidden`.

## Core services: `az_core_v1`

`init` receives a pointer to this table; keep it in a static. Every service that takes an `id`
expects the mod's own id (the same string as `az_mod_v1.id`), and the core checks that the
call comes from that mod's code. A call with another mod's id is refused and logged once.

Deck numbers are `0..3` everywhere (deck 1 is `0`). **`-1` means unknown. Don't
replace an unknown with a guess.**

### Header fields

| Field | Meaning |
|---|---|
| `abi` | The core's ABI version (`1`). |
| `size` | `sizeof(az_core_v1)` in the core you are running on. Used by `AZ_HAS`. |

### Deck identity

These are for hook mods, which receive EP147's own objects. They give the core's answer to
"which deck is this?", so a mod doesn't have to derive it again.

**`int deck_for_ds(void *ds)`**

Given one of the deck program's per-deck state objects (something a hook receives), returns
its deck `0..3`, or `-1`.

**`int deck_for_modeobj(void *obj)`**

Given a pad-mode or widget object, returns its deck `0..3`, or `-1`.

### Hooks

**`int claim_hook(const char *id, uintptr_t addr, void *hook, void **orig)`**

Redirects the function at `addr` to `hook` and stores a pointer that runs the original in
`*orig`. The reason for a refusal is always written to the mod's log, even with logs off.

- Returns `0` on success.
- Non-zero means refused: the address is within 16 bytes of an existing hook (the core's or
  another mod's) or not inside EP147's code, the mod is not allowed hooks
  (`"firmware": ["any"]` or `"hooks": false`), the installer declined the function, or the
  call came from outside `init`.

The hook runs on whatever thread EP147 calls that function from, calls `*orig` exactly as
EP147 would have, and returns fast. For refusal reasons and the rules for hooks, see
[Hooks and firmware tags](05-hooks-and-firmware.md#claim_hook).

### Settings

**`int cfg_get(const char *id, const char *key, int def)`**

Returns your stored value for `key`, or `def` if none.

**`void cfg_set(const char *id, const char *key, int val)`**

Stores a value. Cheap: the write to disk happens later, off your thread.

Values are integers (booleans are `0`/`1`). Keys are up to 31 characters, without `=`, `$`
or control characters such as a newline (`a-z`, `0-9` and `_` are safe), and stay the same
across versions. A mod can store up to 32 keys; `cfg_set` ignores a key it cannot store.
Values live in the settings file on the USB, `MODS/azmods.cfg`, so they go with the USB from
deck to deck: `<id>:cfg.<key> = <number>` and `<id>:str.<key> = "<text>"`, beside the line
`<id>:owner.repo` that names the repository they belong to. A key character outside `a-z`,
`0-9`, `.`, `_` and `-` is written as `:` and two hex digits. Another author's mod installed under
the same id starts with no values.

Changes are written about 250 ms after the last change, at most once every
5 seconds while a mod keeps changing values, and at once when the user leaves the mod's page
or reloads from the Mod Menu.

Read settings once in `init` and keep them in memory. Call `cfg_set` when a value changes,
not every tick.

#### Text settings

Text settings arrived in core 1.2.3; check with `AZ_HAS(core, cfg_set_str)`.

**`int cfg_get_str(const char *id, const char *key, char *buf, int n, const char *def)`**

Copies your stored text for `key` (or `def`) into `buf` and returns its length.

- `n`: the most bytes copied, including the terminating zero. Text is cut at a character
  boundary.
- Returns the length it copied, after any cut to fit `buf`.

**`void cfg_set_str(const char *id, const char *key, const char *val)`**

Stores one line of text: line breaks and control characters are dropped, and text past 127
bytes is cut at a character boundary. Same file and timing as `cfg_set`; the value is written in double quotes.

Text keys follow the same rules as number keys; a text key and a number key with the same name
are two different values. A mod can store 16 of them on top of the 32 number keys.

Text settings suit a chosen name, a color or a URL. Anything bigger, or data rather than a
setting, belongs in the mod's folder on the USB (below).

### Files: your folders on the USB stick

The deck's settings partition is small (about 61 MB, shared with the deck and AZ-MODS), so a
mod's files live on the USB it was installed from. The mod itself runs from the deck's
copy and keeps running after an eject; only its files are gone until the USB is back.

Both services arrived in core 1.2.3 (`AZ_HAS(core, data_dir)`).

**`const char *data_dir(const char *id)`**

Returns `<stick>/MODS/community-mods/<id>/data` on the USB your mod was installed from. The
core creates it for you; it is yours to read and write. It is kept when your mod is updated or
reinstalled, and deleted with the mod when the mod is removed. It is never copied to the deck,
and a package must not contain a `data/` folder. A user clears it by deleting the folder on a
computer.

The folder belongs to the repository your mod came from: a mod with the same id from another
repository gets an empty folder, and the same applies to settings.

**`const char *assets_dir(const char *id)`**

Returns `<stick>/MODS/community-mods/<id>/assets`, the `assets/` you shipped. It is read-only: an
update replaces the whole folder. `NULL` if you ship no `assets/`.

Both return an absolute path with no trailing slash (`"%s/save.txt"`), or `NULL` when that
USB is not in the deck right now (ejected, or not yet mounted when `init` runs at power-on).
The answer is rechecked at most every 2 seconds, and immediately when a cached folder has
gone.

- Call them each time you need a file rather than keeping the pointer.
- `data_dir` creates the folder; don't create either folder from the mod
  ([Guidelines](GUIDELINES.md#files-on-the-usb-not-the-deck)).
- They touch the disk: call them from `init` or one of the mod's own threads, not from
  `on_frame`, `on_draw` or a hook.

A package is at most 16 MB unpacked. A mod can fetch larger files into `data_dir` from a
thread of its own.

### Log

**`void log(const char *id, const char *fmt, ...)`**

`printf`-style, one line per call (a trailing newline is optional).

| | |
|---|---|
| File | `/tmp/azmod-community/<id>.log` on the deck (RAM, cleared at power-off), written as each line arrives |
| Written | only while `/home/root/settings/azmod-logs.on` exists, except the core's own lines about the mod (its load result, refused hooks and refused service calls), which are always written |
| Line length | cut at about 400 characters (about 160 for lines queued from `on_frame` and filters) |
| Size | at most 256 KB per file; at the first line of each start, a file over 128 KB moves to `<id>.log.1` and a new one begins; lines past the cap are dropped until the next start |

Each line is a file write: log on a change, not every tick or frame.

### Firmware

**`const char *fw_tag(void)`**

Returns the running firmware's tag, `ep147-<16 hex>`. The string is static and valid for the
life of the process.

When one build supports several firmware tags, call it in `init` to pick the matching set of
addresses. See [Hooks and firmware tags](05-hooks-and-firmware.md).

### Your page

**`int register_screen(const char *id, const az_screen_v1 *scr)`**

Gives your mod a page under Mod Menu > Community Mods > Installed > your mod. Pass a pointer
to a `static const` struct: the core keeps the pointer.

- Returns `0` on success, or non-zero if called outside `init` or if `scr` has no `on_draw`
  or too small a `size`.

### Drawing

Call these only from `on_draw` (or a surface's `on_paint`), with the `az_draw *` that call
was given. The pointer is valid only during the call: a pointer kept and used later draws
nothing, and its first such use is logged. Coordinates are device pixels (the panel is
1280x800). Everything is clipped to the area; text is cut off at the edge, not shortened.

**`void draw_header(az_draw *d, const char *title)`**

Draws the standard Mod Menu page header (title and divider line) in the active theme. It
takes the first `row_height` of your area; start your own rows one `row_height` below.

**`void draw_text(az_draw *d, int x, int y, const char *s, int px, uint32_t rgb)`**

Draws text in the menu font at size `px`, 8 to 96 (other sizes are clamped). (`x`, `y`) is
the top left of a line box `px * 3 / 2` pixels tall, and the text is centered vertically in
that box.

**`void fill_rect(az_draw *d, int x, int y, int w, int h, uint32_t rgb)`**

Draws a filled rectangle.

**`void draw_rect(az_draw *d, int x, int y, int w, int h, int width, uint32_t rgb)`**

Draws a rectangle outline, `width` pixels thick.

**`void draw_checkbox(az_draw *d, int x, int y, int size, int on, int highlighted)`**

Draws the standard Mod Menu checkbox (`size` 1 to 512).

**`uint32_t color(int role)`**

Returns the active theme's color for a role (below), as `0xRRGGBB`.

**`int row_height(az_draw *d)`**

Returns the height of a standard Mod Menu row: the screen height x 50 / 800, so 50 px on the
deck's panel. Lay out pages in these units.

**`int text_width(const char *s, int px)`**

Returns the width `draw_text` would use for `s` at size `px`. `px` is clamped to 8..96, as in
`draw_text`.

**`void request_redraw(void)`**

Asks for the page to be repainted soon. Cheap. Call it when something shown has changed
(compared at the precision it is shown), not every tick. A clock with seconds needs one a
second; a BPM that moves with the pitch fader stays readable at a few a second.

Color roles let a page match the Day theme and the dark themes:

| Role | Use |
|---|---|
| `AZ_COL_TEXT` | normal text |
| `AZ_COL_TEXT_DIM` | secondary text, hints, dividers |
| `AZ_COL_HI` | the highlight (selected row) background |
| `AZ_COL_HI_TEXT` | text on the highlight |
| `AZ_COL_BG` | the page background |

A hard-coded white is invisible in the Day theme, so take colors from `color(...)`.

### Read-only deck state

All of these may answer "unknown" ([Guidelines](GUIDELINES.md#1-unknown-means-do-nothing)).
Call them from `tick` and page callbacks, not from `on_frame` or hooks.

**`int deck_count(void)`**

Returns the number of decks in the current view: `2` or `4`.

**`int deck_loaded(int deck)`**

Returns `1` if a track is loaded on `deck`, else `0`.

**`double deck_bpm(int deck)`**

Returns the current BPM (with pitch), or `0` if unknown, no track, or outside 20 to 400.
Refreshed every 250 ms.

**`int deck_title(int deck, char *buf, int n)`**

Copies the loaded file's name without its extension when the core knows it, otherwise the
track's title from the library, into `buf`, and returns its length.

- `n`: the most bytes copied, including the terminating zero. `buf` is always terminated.
- A name too long for `buf` is cut: a file name by bytes, a library title at a character
  boundary.
- Returns the length; `0` means unknown.

**`int ui_theme(void)`**

Returns the active theme. Later versions may add more: treat any value other than `1` as
dark. Prefer `color()` for drawing; use this only to choose between two looks.

| Value | Theme |
|---|---|
| `0` | themes off (the deck's stock look, dark) |
| `1` | Day (light) |
| `2` to `5` | the AZ-MODS, Midnight, Ember and Forest dark themes |

### Added in core 1.2.3

The services below come after `ui_theme` in the table. 1.2.3 is the first release that
loads mods built with the SDK, so every deck that runs such a mod has them. A mod that should
also run on a core built without them checks for them with `AZ_HAS`.

The examples assume `static const az_core_v1 *C;` set in `init`, and `MOD_ID` defined as
the mod's id, as in the template.

#### Rows in the Mod Menu

A mod can add rows to the Mod Menu's main list, beside the official ones: a checkbox, a
heading, a button, a row that opens a page, or a choice among a few options. The core draws
them, saves their values and handles the knob, taps and BACK.

**`int register_row(const char *id, const az_row_v1 *row)`**

Adds a row. Call it from `init` only. Every callback, and a `SCREEN` row's screen, must be in
the mod's code.

- Returns a handle (`0` or more), or `-1` with the reason in the log.

**`int row_value(int handle)`**

Returns the row's value. It is one atomic load, safe from any thread, including `on_frame`
and filters.

- A toggle reads `1` only while it and every toggle above it are on.
- A choice reads its index.
- `-1` means a bad handle.

**`int row_set(int handle, int value)`**

Changes one of your own rows' values as if the user had. UI thread only. Refused for
official rows, another mod's rows, and the rows of a mod that is not running.

**`int row_lookup(const char *key)`**

Returns a read-only handle to an official row, by its key (see
[Mod Menu keys](MOD-MENU-KEYS.md)), for `row_value`. `-1` if there is no such row.

`az_row_v1` fields:

- `size`: `sizeof(az_row_v1)`.
- `key`: `a-z`, `0-9` and `-`, 1 to 24 characters, unique within your mod. The value is
  saved in `MODS/azmods.cfg` as `<your id>:<key>`, so it follows the USB, and is kept across
  updates, reinstalls and removal.
- `label`: up to 31 bytes, drawn in the menu's row font.
- `kind`: `AZ_ROW_TOGGLE`, `AZ_ROW_GROUP`, `AZ_ROW_ACTION`, `AZ_ROW_SCREEN` or
  `AZ_ROW_CHOICE`.
- `flags`: `AZ_ROW_DEFAULT_ON`: a toggle starts on.
- `parent`: `NULL` for the main list, or the key of one of the mod's own earlier `GROUP` or
  `TOGGLE` rows: the row is indented under it and grayed while the parent is off.
- `help`: the description shown on the right, or `NULL`.
- `icon`: `NULL`, or a 96 by 96 coverage mask (`AZ_ROW_ICON_N`; one byte a pixel, row by
  row, `0` clear, `255` ink), drawn in the row's text color like the official icons. The core
  copies it, so it may live on the stack.
- `choices`, `n_choices`, `default_choice`: `CHOICE` rows: 2 to 8 labels, and the index used
  until the user picks one.
- `screen`: `SCREEN` rows: the page the row opens, with every callback in the mod's code.
  `NULL` opens the mod's `register_screen` page; with neither, the row is grayed and does
  nothing. BACK returns to the row.
- `available(user)`: `0` grays the row out (say why in `help`). UI thread. May be `NULL`.
- `on_change(value, user)`: runs after a new value is saved. UI thread. May be `NULL`.
- `on_activate(user)`: `ACTION` rows: runs when the row is pressed. UI thread.
- `user`: handed back to every callback.

Limits: 16 rows per mod, 128 for all mods together. Mod rows appear after the official ones,
grouped by mod, in load order and then registration order; the list scrolls. The rows of a
mod that is not running are removed from the menu.

```c
static const az_core_v1 *C;
static int h_flash = -1;

static void flash_changed(int on, void *user)
{
    (void) user;
    C->log(MOD_ID, "flash %s", on ? "on" : "off");
}

static int init(const az_core_v1 *core)
{
    C = core;
    if (!AZ_HAS(C, register_row)) return 1;     /* needs AZ-MODS 1.2.3 */
    const az_row_v1 row = {
        .size = sizeof row, .key = "flash", .label = "Flash on beat",
        .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
        .help = "Blinks the pad LEDs on every beat.", .on_change = flash_changed,
    };
    h_flash = C->register_row(MOD_ID, &row);
    return h_flash < 0;
}
```

`C->row_value(h_flash)` is one atomic load, cheap enough for `on_frame`.

#### Control frames: filters

`frame_filter` shows a mod the frames on the deck's control buses and lets it change or
swallow input before EP147 sees it.

**`int frame_filter(const char *id, unsigned bus_mask, az_frame_fn fn, void *user)`**

Calls `fn(bus, frame, len, user)` for every frame on the buses in `bus_mask`. Call it from
`init` only.

- `bus_mask`: build it with `1u << AZ_BUS_...`.
- Returns `0` when registered.

| Bus | Frame | Your filter may |
|---|---|---|
| `AZ_BUS_ERP_LED` | the pad and mode-button LED frame, as the deck sends it | edit, eat |
| `AZ_BUS_ERP_IN` | buttons, pads and jogs of decks 1 and 2, as the deck reads them | edit, eat |
| `AZ_BUS_MIXER_IN` | the mixer and panel: knobs, faders, the browse knob, the screen buttons | edit, eat |
| `AZ_BUS_MIXER_LED` | the mixer's LED frame | read a copy |
| `AZ_BUS_DECK_IN` | the two deck MCU buses (jog platters) | read a copy |

Rules:

- `fn` runs on the bus thread, after the core's own handling and any filters registered
  before it. The `on_frame` rules apply: microseconds, no blocking, no files, `log` is
  queued, and settings and file services are refused.
- Edit the frame in place and return `AZ_FRAME_OK`, or return `AZ_FRAME_EATEN` to stop the
  chain (an input frame you eat doesn't reach the deck's program). The core recomputes the
  checksum once, after the last filter, so don't compute one yourself.
- Input frames reach you only if they arrived with a valid checksum.
- Add `AZ_FILTER_RAW` to `bus_mask` to see input frames, read-only, before the core
  changes them.
- Each call is timed. A filter that takes longer than 200 µs on 8 calls (in total, not in a
  row) is switched off until EP147 restarts; its frames then pass through untouched, and
  the log says so. A fault in a filter stops EP147 like any crash in the mod's code.
- A filter registered on two buses can be called from two threads at once: keep its state
  per bus, or use atomics.
- Up to 32 filters for all mods together.

```c
static int h_lock = -1;

/* Swallow mixer frames while our "lock" row is on. */
static int mixer_filter(int bus, unsigned char *frame, int len, void *user)
{
    (void) bus; (void) frame; (void) len; (void) user;
    return C->row_value(h_lock) == 1 ? AZ_FRAME_EATEN : AZ_FRAME_OK;
}

/* in init(): */
static int add_filter(void)
{
    return C->frame_filter(MOD_ID, 1u << AZ_BUS_MIXER_IN, mixer_filter, NULL);
}
```

`on_frame` (below) still works but sees only `AZ_BUS_ERP_LED`; new mods should use
`frame_filter`.

#### Touch and pointer

**`int touch_filter(const char *id, az_touch_fn fn, void *user)`**

Offers each touch on the screen to the mod before EP147 sees it. Call it from `init` only. Up
to 16 for all mods together.

- Returns `0` when registered.

`fn(t, user)` receives one contact (`az_touch`: `slot`, `x`, `y` in screen pixels, `down`)
and returns `AZ_TOUCH_TAKE` or `AZ_TOUCH_PASS`.

- The answer given when a finger comes down holds until it lifts: once a filter takes a
  touch, every report until the lift goes to it, and EP147 doesn't see half a touch.
- Only the first contact (slot 0) goes to touch filters, offered to each in the order the
  mods loaded until one takes it.
- A taken touch stays yours, blanked for the deck, until it lifts, even if your filter is
  switched off.
- `fn` runs on the bus thread and is timed like a frame filter.

```c
/* Take touches in the top-left corner, leave the rest to the deck. */
static int corner(const az_touch *t, void *user)
{
    (void) user;
    return (t->x < 100 && t->y < 100) ? AZ_TOUCH_TAKE : AZ_TOUCH_PASS;
}
```

**`int pointer_inject(int x, int y, int button, int down)`**

Sends real pointer input to the X server, for another program's window you show with
`xwin_show`.

- `button`: `0` move only, `1` left, `2` middle, `3` right.
- `down`: `1` press, `0` release.
- Returns `0` when queued, `-1` when refused (no X test extension, or the queue is full).

**`void pointer_cursor(int visible)`**

Shows or hides the X pointer.

#### What is on screen, and surfaces

A surface is an overlay: an opaque rectangle a mod paints over EP147's screen, shown only on
the views it names and only while the mod is loaded. The core owns the display connection,
the stacking and the repaint timing, and fills the surface with `AZ_COL_BG` before each
`on_paint`.

All of these except `view` are called on the UI thread (`surface_create` from `init`), and
`on_paint` and `on_input` also run there.

**`int view(void)`**

Returns the deck program's current screen: `AZ_VIEW_SOURCE`, `AZ_VIEW_BROWSE` (including
Tag List and playlists), `AZ_VIEW_SEARCH`, `AZ_VIEW_DECK`, `AZ_VIEW_MENU` or
`AZ_VIEW_OTHER`. One atomic load, any thread.

**`az_surface *surface_create(const char *id, const az_surface_v1 *s)`**

Creates a surface. Call it from `init` only; the descriptor needs `size`, a layer, `on_paint`
and at least one view.

- Returns `NULL` when refused (4 per mod, 16 in all).

**`void surface_place(az_surface *s, az_rect r)`**

Sets its position and size, in screen pixels, clamped onto the screen.

**`void surface_show(az_surface *s, int on)`**

Shows or hides it (it still appears only on the views in its mask).

**`void surface_redraw(az_surface *s)`**

Asks for a repaint.

**`int surface_focus(az_surface *s, int on)`**

Gives your surface the browse knob, its click and BACK; the deck's own list holds still
meanwhile.

- Returns `1` when granted, `0` when refused (the Mod Menu is open, or another surface has
  focus).

`az_surface_v1` fields:

- `size`.
- `layer`: `AZ_LAYER_OVER_EP147`, above the deck's screen and below the Mod Menu, or
  `AZ_LAYER_TOP`, above the Mod Menu.
- `views`: a mask of `AZ_VIEW_BIT(...)`.
- `on_paint(d, area, user)`: draws with the drawing services only, inside `area`.
- `on_input(ev, user)`: gets taps inside the surface and, while it has focus, the knob, its
  click and BACK. Return `1` if handled; returning `0` for `AZ_EV_BACK` gives the knob back to
  the deck.
- `user`.

A press on a surface delivers `AZ_EV_DOWN` then `AZ_EV_TAP`, its release delivers
`AZ_EV_UP`, and a press held for 600 ms delivers `AZ_EV_LONG`, each with `x`, `y` in screen
pixels.

```c
static az_surface *badge;

static void badge_paint(az_draw *d, az_rect area, void *user)
{
    (void) user;
    C->fill_rect(d, area.x, area.y, area.w, area.h, C->color(AZ_COL_HI));
    C->draw_text(d, area.x + 8, area.y + 4, "LIVE", 24, C->color(AZ_COL_HI_TEXT));
}

static const az_surface_v1 BADGE = {
    .size = sizeof(az_surface_v1), .layer = AZ_LAYER_OVER_EP147,
    .views = AZ_VIEW_BIT(AZ_VIEW_DECK), .on_paint = badge_paint,
};

/* in init(): */
static int add_badge(void)
{
    badge = C->surface_create(MOD_ID, &BADGE);
    if (!badge) return 1;
    C->surface_place(badge, (az_rect) { 1180, 8, 90, 44 });
    C->surface_show(badge, 1);
    return 0;
}
```

#### Other programs' windows

These services are for a program the mod started with `launch` that opens its own X window.
Up to 8 windows can be shown at once. `touch_filter` and `pointer_inject` pass the user's
touches to them.

**`int xwin_find(const char *id, int pid, const char *title_prefix, int min_w, int min_h, unsigned long *xid)`**

Looks for a top-level window of a program the mod launched (`pid`) whose title starts with
`title_prefix` and that is at least `min_w` by `min_h`.

- Returns `0` when found, with `*xid` set.

**`int xwin_show(const char *id, unsigned long xid, az_rect r, int on)`**

Shows a window of a program the mod launched (refused for any other window) at `r`, above
EP147's screen and below `AZ_LAYER_TOP` surfaces, or hides it. A width or height of `0` keeps
the window's own size and only moves it. The core absorbs the window's X errors.

- Returns `0` when done.

**`int xwin_geometry(const char *id, unsigned long xid, az_rect *out)`**

Gets the window's current position and size.

- Returns `0` when done.

#### Images and lines

Like the other drawing services, call these only from `on_draw` or `on_paint`.

**`int draw_image(az_draw *d, int x, int y, const unsigned char *rgba, int w, int h)`**

Blends an RGBA image (premultiplied alpha, `w * h * 4` bytes) at `x`, `y`, clipped to your
area.

- Returns `0` when drawn, `-1` when refused (`NULL`, or not called from `on_draw` or
  `on_paint`).

**`void draw_line(az_draw *d, int x0, int y0, int x1, int y1, int width, uint32_t rgb)`**

Draws a line `width` pixels thick, 1 to 32 (other values are clamped), clipped to your area.

#### Deck state and events

**`int deck_info(int deck, az_deck_info *out)`**

Fills a snapshot of one deck, as far as `out->size` (set it first) reaches. Any thread,
doesn't block.

- Returns `0` when filled (fields the core cannot read are left unknown), `-1` for no such
  deck or a `size` too small.

| `az_deck_info` field | |
|---|---|
| `loaded`, `has_stems`, `stems_on` | `1` / `0`, `-1` unknown |
| `playing` | `1` if the playhead moved forward in the last 250 ms, `-1` until known |
| `pad_mode` | `0` HOT CUE, `1` BEAT LOOP, `2` SLIP LOOP, `3` BEAT JUMP, `-1` unknown; this core reports only BEAT LOOP and SLIP LOOP |
| `content_id` | the rekordbox track id, `0` unknown |
| `bpm` | as `deck_bpm` |
| `pos_s` | the playhead in seconds, `-1` unknown |
| `len_s` | the track length, `-1` unknown; this core always reports `-1` |
| `title`, `artist` | empty when unknown |
| `key` | empty when unknown; this core always leaves it empty |

Each field is read atomically, but the snapshot is not taken at one instant: during a track
load, one call can show the new title beside the old BPM.

**`int deck_grid(int deck, double *beat_phase, double *bar_phase)`**

Gets the playhead's position within the current beat, `0` to `1`. Any thread.

- `*bar_phase` is `-1` today: the deck's beat grid has no bar position.
- Returns `0` when done, `-1` for no grid.

**`int on_deck_event(const char *id, void (*fn)(int deck, int what, void *user), void *user)`**

Calls `fn` on the UI thread when a deck loads (`AZ_DECK_LOADED`), ejects (`AZ_DECK_EJECTED`),
plays (`AZ_DECK_PLAY`) or stops (`AZ_DECK_STOP`). Call it from `init`. Up to 4 per mod.

```c
static void on_deck(int deck, int what, void *user)
{
    (void) user;
    if (what != AZ_DECK_LOADED) return;
    az_deck_info di = { .size = sizeof di };
    if (C->deck_info(deck, &di) == 0 && di.title[0])
        C->log(MOD_ID, "deck %d: %s (%.1f BPM)", deck + 1, di.title, di.bpm);
}
```

#### Feedback and threads

"The UI thread" is the core's thread that runs `tick`, your pages, surfaces and row
callbacks.

**`void banner(const char *id, const char *text, int ms)`**

Shows `text` (up to 111 bytes) in EP147's own caution box for `ms` milliseconds (`0` or
less: the stock time). Any thread; queued (at most 64 waiting; a full queue drops it and
logs).

**`int confirm(const char *id, const char *title, const char *text, const char *yes, void (*fn)(int yes, void *user), void *user)`**

Asks a yes or no question in a dialog over whatever is on screen. Cancel is focused first and
BACK means no. `fn` (in the mod's code) runs once, on the UI thread, with the answer.

- `title`: default "Are you sure?".
- `text`: up to four wrapped lines.
- `yes`: the label of the other button besides Cancel (default "OK").
- Returns `0` when taken (shown at the next tick), `-1` when refused (another question is
  open; `fn` is not called).

**`void post_ui(const char *id, void (*fn)(void *user), void *user)`**

Runs `fn` on the UI thread soon. `fn` must be in the mod's library. Queued (at most 64
waiting; a full queue drops it and logs); up to 16 run per tick.

It is meant for the mod's own threads. From `on_frame` or a filter, set an atomic flag and act
on it in `tick`.

**`int thread_start(const char *id, const char *name, void *(*fn)(void *), void *arg)`**

Starts a detached, normal-priority (`SCHED_OTHER`) thread with an alternate signal stack,
named `name` (15 characters shown) in the crash log. Up to 4 running per mod. Refused from
`on_frame` and filters.

- Returns `0` when started.

The core cannot stop a thread: it ends when `fn` returns, and its state reaches `on_frame` and
filters only through atomics.

```c
static int result;          /* written by the worker, read on the UI thread */

static void show_result(void *user)
{
    (void) user;
    C->banner(MOD_ID, result ? "Scan finished" : "Scan failed", 2000);
}

static void *worker(void *arg)
{
    (void) arg;
    result = 1;             /* slow work here: files, the network, the library */
    C->post_ui(MOD_ID, show_result, NULL);
    return NULL;
}

/* in init(): */
static int start_worker(void)
{
    return C->thread_start(MOD_ID, "scan", worker, NULL);
}
```

#### Other programs

A whole program (an emulator, a game, a helper with its own window) runs beside EP147, not
inside it. Call these from `init`, the UI thread or one of the mod's own threads, not from
`on_frame` or a filter.

**`int launch(const char *id, const char *path, const char *const argv[], const az_launch_opts *opts)`**

Starts the executable at `path`, which must resolve to a file inside the mod's `assets_dir`
or `data_dir`. Up to 32 programs run at once across all mods.

- `argv`: `NULL`-terminated, up to 32 entries, not including the program's own name.
- `opts`: see the table below, or `NULL` for defaults.
- Returns the program's pid, or `-1` (refused, with the reason in the log).

The program starts through `systemd-run --scope` without the mods' preload, in its own
cgroup outside EP147's, with normal (not real-time) scheduling.

| `az_launch_opts` (`NULL` for defaults) | |
|---|---|
| `size` | `sizeof(az_launch_opts)` |
| `nice` | `0` to `19` (clamped) |
| `cpu_mask` | bit `n` allows core `n`; `0` = any |
| `mem_mb` | memory limit; `0` = none |
| `log_path` | a plain file inside the mod's `data_dir` (a link, `.` or `..` is refused): the program's stdout and stderr, capped at 1 MB and rotated once to `<log>.1`; `NULL` discards them |

**`int launch_alive(const char *id, int pid)`**

Returns `1` while a program the mod launched is still running.

**`int launch_stop(const char *id, int pid)`**

Stops a program the mod launched: `SIGTERM`, then `SIGKILL` after 2 seconds.

- Returns `0` when signaled.

**`int handoff(const char *id, const char *path, const char *const argv[])`**

Hands the screen and controls to a program in EP147's place and starts EP147 again when it
exits. `argv` follows the same rules as for `launch`.

- Returns `0` once the core's confirm dialog is up (the user chooses **Start** or Cancel),
  `-1` if refused (another handoff, or another question, is open). After a yes it is still
  refused if a deck is playing.
- Holding BACK for 3 seconds ends the program (`SIGTERM`, then `SIGKILL` 2 seconds later) and
  returns to the deck, whatever the program does; the dialog tells the user.
- If EP147 does not come back up, the core tries once more, then restarts the deck.

**`void reload(const char *id)`**

The Mod Menu's Reload. Refused while any track is loaded.

```c
static int start_helper(void)
{
    const char *dir = C->assets_dir(MOD_ID);
    if (!dir) return -1;
    char path[512];
    snprintf(path, sizeof path, "%s/helper", dir);
    const char *const argv[] = { "--quiet", NULL };
    const az_launch_opts opts = { .size = sizeof opts, .nice = 10 };
    return C->launch(MOD_ID, path, argv, &opts);
}
```

#### The library on a USB

These services read the rekordbox library (`PIONEER/rekordbox/exportLibrary.db`) on the
USBs, read-only.

They read the USB, so call them from one of the mod's own threads (`thread_start`); on the UI
thread they are refused.

**`int lib_playlists(const char *id, int stick, int (*fn)(int pl_id, int parent, const char *name, int folder, void *user), void *user)`**

Calls `fn` for each playlist and folder, grouped by parent folder and in the DJ's order
within each.

- `parent`: the `pl_id` of the folder holding it; an entry whose `parent` is no `pl_id`
  reported in the same call is at the top level.
- `folder`: `1` for a folder.

**`int lib_tracks(const char *id, int stick, int playlist_id, int (*fn)(const az_track *t, void *user), void *user)`**

Calls `fn` for each track of a playlist, in the DJ's order.

- `playlist_id`: `0` lists every track.
- The strings in `az_track` are valid only during the callback, and each holds at most 191
  bytes.

| `az_track` field | |
|---|---|
| `size` | `sizeof(az_track)` in the core |
| `id` | the rekordbox content id |
| `title`, `artist`, `album`, `key` | from the library; empty when it has none |
| `path` | the audio file's absolute path on the deck (the USB's mount point and the library's path) |
| `bpm` | the library's tempo (`bpmx100 / 100`) |
| `len_s` | the track length in seconds, from the library |

**`int lib_spool_kick(const char *id, int stick)`**

Asks the core to apply the library spool now (below).

- Returns `0` when asked.

For all three, `stick` is `0` for the deck's USB 1 port and `1` for USB 2; a USB whose port
cannot be identified is not read. Each call opens its own read-only connection, copies what
it needs and closes it. Only then does it run the callbacks, on the caller's thread and
inside the call; return non-zero to stop early. If the library file changes during the read,
the call reads once more and then gives up (`-1`).

| Returns | |
|---|---|
| `0` | done |
| `1` | done, but the list was cut at 20,000 entries (one call doesn't copy more) |
| `-1` | no library, the library kept changing, or refused (the reason is in the log) |

```c
static int count_one(const az_track *t, void *user)
{
    (void) t;
    ++*(int *) user;
    return 0;
}

static int count_tracks(int stick, int playlist_id)   /* from your own thread */
{
    int n = 0;
    int r = C->lib_tracks(MOD_ID, stick, playlist_id, count_one, &n);
    return r < 0 ? -1 : n;        /* 1 = cut at 20,000 tracks: n holds the first 20,000 */
}
```

**The library spool** lets a program of the mod's add to a USB's library without an eject or
a restart.

- Write `<stick>/PIONEER/azspool/<name>.sql.tmp`, then rename it to `<name>.sql`.
- Each line is one `INSERT [OR IGNORE] INTO` or `UPDATE` statement on the library tables
  (`content`, `artist`, `album`, `genre`, `key`, `image`, `playlist`, `playlist_content`,
  `label`); blank lines and `--` lines are skipped.
- The core applies a file as one transaction on the deck program's own connection, and the
  lists on screen re-read it.
- A file with any other statement is renamed `.rejected`; one that fails is rolled back and
  renamed `.failed`; an applied one is deleted.

By itself, the core applies spools only for its own copies. `lib_spool_kick` asks it to
apply what is on the USBs now. Because the core then applies every USB's spool, the kick is
refused (with the reason in your log) while a deck plays a track from that USB, from any USB
with a spool waiting, or from a USB it cannot place. Call it from your own thread: it watches
the decks for 250 ms.

#### Audio taps

**`int audio_tap(const char *id, int source, int frames, void (*fn)(const float *lr, int frames, void *user), void *user)`**

Calls `fn` (in the mod's code) with one deck's audio, in interleaved stereo float blocks of
`frames` frames (64 to 8192). Refused from inside a tap callback.

- `source`: `AZ_TAP_DECK(n)`, `n` `0` to `3`.
- Returns `0` when tapping.

Each tap carries one deck's output as the deck's program renders it, before the mixer.
The XDJ-AZ mixes in hardware (channel faders, EQ, the crossfader and the master level),
so no software on the deck sees the master mix, and there is no master tap.

`fn` runs on the core's tap thread (normal priority), not on the audio thread; the tap
thread collects blocks every 10 ms. A slow `fn` loses blocks (counted in the log, at most one
line every 10 seconds) but doesn't delay the audio. Up to 4 taps per mod, 16 in all.

Filters, touch filters, deck-event listeners and taps stay registered until the deck's
program restarts, and a thread runs until it returns. None of them can be removed; instead,
each has a way to go idle (a flag that `tick` or a setting clears).

```c
static _Atomic float level[4];

static void meter(const float *lr, int frames, void *user)
{
    int deck = (int) (intptr_t) user;
    float peak = 0;
    for (int i = 0; i < frames * 2; i++) {
        float v = lr[i] < 0 ? -lr[i] : lr[i];
        if (v > peak) peak = v;
    }
    level[deck] = peak;
}

static int tap_all(void)
{
    for (int d = 0; d < 4; d++)
        if (C->audio_tap(MOD_ID, AZ_TAP_DECK(d), 1024, meter, (void *) (intptr_t) d) != 0)
            return 1;
    return 0;
}
```

#### Virtual method hooks

**`int claim_vslot(const char *id, uintptr_t slot, void *fn, void **orig)`**

Like `claim_hook`, for one virtual method. `fn` must be in the mod.

- `slot`: the address of the vtable entry, which must be aligned, inside EP147's data and
  still hold a function in EP147's code.
- Returns `0` when claimed.

Only for mods whose `azmod.json` has `"hooks": true` and exact firmware tags. The `claim_hook`
rules apply: one owner per slot, call from `init`, claim last
([Hooks and firmware tags](05-hooks-and-firmware.md#claim_vslot-one-virtual-method)).

## Your page: `az_screen_v1`

```c
static const az_screen_v1 SCREEN = {
    .size = sizeof(az_screen_v1),
    .on_draw = on_draw, .on_input = on_input, .on_enter = on_enter, .user = NULL,
};
```

- `size`: `sizeof(az_screen_v1)`.
- `on_draw(d, area, user)`: paints the page inside `area`, with the drawing services only.
  Required.
- `on_input(ev, user)`: an input event while the page is active. Return `1` if handled, `0` to
  let the core decide. Returning `0` for `AZ_EV_BACK` lets the core go back.
- `on_enter(user)`: the page has become active (reset its highlight, for example). May be
  `NULL`.
- `user`: handed back to every callback.

Events (`az_event`):

| `kind` | Meaning | Fields |
|---|---|---|
| `AZ_EV_KNOB` | the browse knob turned | `dir` = `-1` or `+1` |
| `AZ_EV_CLICK` | the browse knob pressed | |
| `AZ_EV_TAP` | a tap inside the page's `area` (or on a surface) | `x`, `y` in device pixels |
| `AZ_EV_BACK` | the BACK button | |
| `AZ_EV_DOWN`, `AZ_EV_UP`, `AZ_EV_LONG` | a press, its release, a long press (surfaces only) | `x`, `y` in screen pixels |

## Control frames: `on_frame`

```c
int on_frame(int bus, unsigned char *frame, int len);
```

The core calls every loaded mod's `on_frame`, in load order, for each `AZ_BUS_ERP_LED`
frame (the frame that sets the pad and mode-button LEDs), right before the deck sends it and
after the core's own changes. [`frame_filter`](#control-frames-filters) sees every bus, including
this one. `len` is the number of bytes that may be read and changed (the checksummed part of
the frame, 120 bytes in ABI 1); nothing past it is touched.

- To only read a frame, return `AZ_FRAME_OK`.
- To change it, edit it in place and return `AZ_FRAME_OK`. The core recomputes the
  frame's checksum once, after the last mod. Don't compute a checksum yourself.
- To stop later mods from seeing it, return `AZ_FRAME_EATEN`; the core stops passing it to
  the next mods. For `AZ_BUS_ERP_LED` the frame is still sent to the LEDs (with any changes
  made so far): an LED frame isn't dropped.

`on_frame` runs on the deck's I/O path, at real-time priority, many times a second.
Keep it to microseconds, with no services except a rare `log` (queued) and no locks.

## Versioning and `AZ_HAS`

`az_core_v1` only grows at the end. A mod built against a newer header can run on an older
core that lacks the newest services, and checks before calling one:

```c
char buf[96];
int n = 0;
if (AZ_HAS(C, deck_title))
    n = C->deck_title(0, buf, sizeof buf);    /* deck 1 */
```

`AZ_HAS(core, field)` is true when the running core's table is large enough to
contain `field` and the entry is not `NULL`. A mod that cannot work without a service checks
it in `init` and returns non-zero with a log line naming it.

A change that is not append-only gets a new ABI number (`AZ_MOD_ABI`), and the core
refuses mods built for a different one, showing the reason in the Mod Menu.
