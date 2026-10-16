# Your first mod

The template, `hello-az`, is a complete mod: a page under **Mod Menu > Community Mods >
Installed > Hello AZ** with one setting (show track titles) and a live list of each deck's
BPM and track title. The sections below walk through its source, `template/hello-az.c`.

## 1. Start from the template

```sh
azmod-pack new ../beat-counter --id beat-counter --author your-name
azmod-pack build ../beat-counter
```

`new` copies the template and renames `hello-az` to the id in the source, the `Makefile`,
`azmod.json` and the release workflow. It points the Makefile's `AZ_SDK` and `AZ_SDK_INCLUDE`
at this SDK, relative to the new folder so that `make test` finds the harness, and fills
`azmod.json` with placeholders:

```
beat-counter/
    beat-counter.c                  the mod
    azmod.json                      the manifest
    icon.png                        256x256, shown in the Mod Menu's Browse grid
    Makefile                        build flags (see the Makefile's header for each one)
    LICENSE                         GPL-3.0
    README.md
    .github/workflows/release.yml   release on a pushed tag
```

The mod's sources can be C, C++ or both; see [Can I use C++?](07-faq.md#can-i-use-c).

The id is 3 to 32 characters drawn from `a-z`, `0-9` and `-`, and cannot be any of `enabled`,
`azmod`, `azstem`, `az-mods` or `azmods`. It names the mod's folder on the USB, the prefix of
its settings in `MODS/azmods.cfg`, its log and its package, and it never changes once the mod
is published. It must also be unique across every repository a deck reads; see [Pick an ID that stays yours](06-publishing.md#pick-an-id-that-stays-yours).

## 2. The manifest: `azmod.json`

```json
{
  "azmod": 1,
  "id": "beat-counter",
  "name": "Beat Counter",
  "version": "0.1.0",
  "author": "your-name",
  "summary": "Describe in one line what your mod does.",
  "description": "Describe what your mod changes on the deck ...",
  "license": "GPL-3.0-only",
  "source": "https://github.com/your-name/beat-counter",
  "api": 1,
  "firmware": ["any"],
  "hooks": false,
  "min_core": "1.2.3"
}
```

The core reads the manifest before any of the mod's code runs.

| Field | For this mod |
|---|---|
| `firmware`, `hooks` | `["any"]` and `false`: the mod uses services only, so it loads on every firmware AZ-MODS supports. For hook mods, see [Hooks and firmware tags](05-hooks-and-firmware.md) |
| `source` | A public `https://` link to the source. A deck refuses a mod without one (`No source link in azmod.json`) |
| `summary`, `description` | What users read before installing: up to 119 characters, in plain text |
| `min_core` | The oldest AZ-MODS release the mod runs on. An older core shows `Needs AZ-MODS <min_core> or newer` and does not load the mod |

For every field, see [Packaging and publishing](06-publishing.md#azmodjson).

## 3. The descriptor and entry point

Every mod ends with its descriptor and entry point:

```c
static const char *const FW_TAGS[] = { "any", NULL };

static const az_mod_v1 MOD = {
    .abi      = AZ_MOD_ABI,
    .size     = sizeof(az_mod_v1),
    .id       = MOD_ID,              /* "beat-counter", same as azmod.json */
    .fw_tags  = FW_TAGS,             /* same as "firmware" in azmod.json */
    .init     = init,
    .tick     = tick,
    .on_frame = NULL,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
```

`az_mod_v1_entry` is the only exported symbol. The core calls it, checks the descriptor's ABI,
id and firmware tags against `azmod.json`, and then calls `init`.

## 4. `init`

```c
static const az_core_v1 *C;          /* the core's services, kept for later */
static int show_titles = 1;

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;

    show_titles = C->cfg_get(MOD_ID, "show_titles", 1) ? 1 : 0;

    if (C->register_screen(MOD_ID, &SCREEN) != 0)
        C->log(MOD_ID, "register_screen refused; staying loaded without a page");

    C->log(MOD_ID, "loaded on %s, show_titles=%d", C->fw_tag ? C->fw_tag() : "?", show_titles);
    return 0;
}
```

`init` runs once, on the core's UI thread, before any other callback.

- `core` stays valid for the life of the process: keep the pointer.
- Returning `0` keeps the mod loaded. Returning non-zero means the core never calls the mod
  again and shows `Its init() returned <n>` in the Mod Menu; the library stays mapped.
- The core's services do not exist yet in a C constructor, so do all setup here
  ([Guidelines](GUIDELINES.md#2-set-up-in-init-not-in-a-constructor)).

## 5. How a mod runs

After `init`, a mod runs only from callbacks. It can follow either of two models, or both:

| | Polling | Event-driven |
|---|---|---|
| Entry | `tick()`, called periodically, up to about 30 times a second at a varying rate | Callbacks registered in `init`: deck events, frame and touch filters, surface paints, `post_ui`, and threads of its own |
| Suits | Reading state and repainting on a change | Reacting to something as it happens, or work that blocks |
| Examples | this template, Set Clock, Deck Overview | Deck Events (`.tick = NULL`) |

The [API reference](API.md#threads-and-timing) lists which thread runs each callback and its time budget.

## 6. Settings: `cfg_get` and `cfg_set`

Settings are whole numbers, stored as `<id>:<key>` lines in `MODS/azmods.cfg` on the USB, so
they follow the USB from deck to deck. Read each one once in `init`, keep it in a variable, and
write it when it changes:

```c
static void toggle_titles(void)
{
    show_titles = !show_titles;
    C->cfg_set(MOD_ID, "show_titles", show_titles);
    if (AZ_HAS(C, request_redraw)) C->request_redraw();
}
```

`cfg_set` returns at once; the core writes the file later, off the caller's thread. For limits,
text settings and timing, see the [API reference](API.md#settings).

## 7. A page in the Mod Menu

A page is three callbacks in a `static const` struct, registered from `init`. The core keeps
the pointer:

```c
static const az_screen_v1 SCREEN = {
    .size     = sizeof(az_screen_v1),
    .on_draw  = on_draw,     /* paint the page */
    .on_input = on_input,    /* knob, click, tap, BACK */
    .on_enter = on_enter,    /* the page just opened (may be NULL) */
};
```

### Drawing

`on_draw` receives a paint context and the rectangle the page owns. Draw only with the core's
helpers, size everything in units of `row_height`, and take colors from `color(AZ_COL_...)` so
the page works in the light Day theme and the dark ones.

Keep everything inside `area`, bottom edge included. On the deck, drawing past the edge is
clipped; in the [test harness](04-testing.md#the-test-harness) it fails the `draw-area` check.
Pages range from the full screen down to the harness's smallest, 120x60 with 24-px rows,
which fits the header and one row. The template draws each checkbox row with
`draw_setting_row()`, which stops when the next row would not fit:

```c
static int draw_setting_row(az_draw *d, az_rect area, int *y, const char *label, int on,
                            int highlight, az_rect *hit)
{
    const int row = C->row_height(d);
    const int pad = row / 4, box = row - 2 * pad;
    int px = row / 2;
    if (px < 8) px = 8;
    if (px > 96) px = 96;
    *hit = (az_rect){ 0, 0, 0, 0 };
    if (*y + row > area.y + area.h) return 0;       /* does not fit: draw nothing */
    *hit = (az_rect){ area.x, *y, area.w, row };    /* remembered for taps */
    C->draw_checkbox(d, area.x + pad, *y + pad, box, on, highlight);
    draw_fitted(d, area.x + 2 * pad + box, *y + pad, label, px, C->color(AZ_COL_TEXT),
                area.w - 3 * pad - box);
    *y += row;
    return 1;
}
```

A simplified `on_draw` follows. The template's version also draws a divider and a row per
deck, each with the same bottom check:

```c
static void on_draw(az_draw *d, az_rect area, void *user)
{
    int y = area.y;
    C->draw_header(d, "Beat Counter");
    y += C->row_height(d);
    if (!draw_setting_row(d, area, &y, "Show track titles", show_titles, highlighted == 0,
                          &titles_row))
        return;                                      /* the page is full */
}
```

Text is cut off at the edge, not shortened. The template's `draw_fitted()` shortens a string by
whole UTF-8 characters until its `text_width` fits.

### Input

`on_input` returns `1` for an event it handled and `0` otherwise. Returning `0` for
`AZ_EV_BACK` lets the core leave the page. Taps arrive only from inside `area`:

```c
static int on_input(const az_event *ev, void *user)
{
    switch (ev->kind) {
    case AZ_EV_CLICK:            /* browse knob pressed */
        toggle_titles();
        return 1;
    case AZ_EV_TAP:              /* a tap on the page (ev->x, ev->y) */
        if (ev->x < titles_row.x || ev->x >= titles_row.x + titles_row.w ||
            ev->y < titles_row.y || ev->y >= titles_row.y + titles_row.h)
            return 0;            /* not on the checkbox row */
        toggle_titles();
        return 1;
    case AZ_EV_KNOB:             /* browse knob turned, ev->dir = -1 or +1 */
        return 1;
    default:
        return 0;                /* BACK and anything else: the core decides */
    }
}
```

## 8. Reading the decks

```c
int n = C->deck_count();                   /* 2 or 4 decks in the current view */
for (int deck = 0; deck < n; deck++) {     /* deck 1 is 0 */
    if (!C->deck_loaded(deck)) continue;
    double bpm = C->deck_bpm(deck);        /* 0 = unknown */
    char title[96];
    if (C->deck_title(deck, title, sizeof title) <= 0)
        title[0] = '\0';                   /* unknown: show nothing */
}
```

Each of these calls can report "unknown" (`-1`, `0` or an empty string). When one does, the mod
does nothing for that deck this time instead of substituting deck 1 or a guess
([Guidelines](GUIDELINES.md#1-unknown-means-do-nothing)).

## 9. `tick`

`tick` runs on the same thread as the page callbacks, so they share variables without locks.
Its rate varies, so anything timed uses a clock. The template reads the decks, compares them
with what it last showed, and requests at most one repaint every 250 ms of `CLOCK_MONOTONIC`
time. The example below is simplified to deck 1; the template's `tick()` compares every deck in
the view:

```c
#include <time.h>

#define MIN_REDRAW_MS 250
static long last_redraw_ms = -MIN_REDRAW_MS;
static long shown_bpm = -1;            /* deck 1's BPM as last drawn, x100 */
static int  dirty;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void tick(void)
{
    double b = C->deck_bpm(0);
    if (!(b > 0.0 && b < 1000.0)) b = 0.0;  /* unknown or not a number: casting it is UB */
    long bpm = (long)(b * 100.0 + 0.5);
    if (bpm != shown_bpm) {
        shown_bpm = bpm;
        dirty = 1;
    }
    long now = now_ms();
    if (dirty && now - last_redraw_ms >= MIN_REDRAW_MS) {
        dirty = 0;
        last_redraw_ms = now;
        C->request_redraw();
    }
}
```

`tick` is one of the busy paths: no file or network access, no sleeping, no large allocations,
and a log line only on a change ([Guidelines](GUIDELINES.md#3-keep-the-busy-paths-fast)).
The test harness holds `tick` to the repaint rate set by `MAX_REDRAWS` in the Makefile (4 for
the template).

## 10. Change it, build, test

To add a second setting, edit `beat-counter.c`:

1. Beside `static int show_titles = 1;` add `static int show_bpm = 1;`, and beside
   `static az_rect titles_row;` add `static az_rect bpm_row;`.
2. In `init`, after `show_titles` is read:
   `show_bpm = C->cfg_get(MOD_ID, "show_bpm", 1) ? 1 : 0;`
3. In `on_draw`, right after the `draw_setting_row(...)` call for the titles, add a second call:
   `if (!draw_setting_row(d, area, &y, "Show BPM", show_bpm, highlighted == 1, &bpm_row)) return;`
   On a page too short for it, the row is not drawn and `bpm_row` stays empty.
4. Replace `on_input` with:

```c
static void toggle_bpm(void)
{
    show_bpm = !show_bpm;
    C->cfg_set(MOD_ID, "show_bpm", show_bpm);
    C->request_redraw();
}

static int in_rect(az_rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int on_input(const az_event *ev, void *user)
{
    (void)user;
    switch (ev->kind) {
    case AZ_EV_KNOB:                         /* two rows: 0 = titles, 1 = BPM */
        highlighted = ev->dir < 0 ? 0 : 1;
        C->request_redraw();
        return 1;
    case AZ_EV_CLICK:
        if (highlighted == 0) toggle_titles();
        else toggle_bpm();
        return 1;
    case AZ_EV_TAP:
        if (in_rect(titles_row, ev->x, ev->y)) { toggle_titles(); return 1; }
        if (in_rect(bpm_row, ev->x, ev->y))    { toggle_bpm(); return 1; }
        return 0;
    default:
        return 0;                            /* BACK and anything else: the core decides */
    }
}
```

```sh
azmod-pack build ../beat-counter                 # compile in the container, then validate
make -C ../beat-counter test                     # the host test harness (needs clang)
```

On Windows, `make test` runs in a Linux container; see [Running it](04-testing.md#running-it).

For what `validate` checks, see the [`azmod-pack` reference](06-publishing.md#what-validate-checks).
For the harness, see [Testing on your deck](04-testing.md#the-test-harness).

**Next:** [Testing on your deck](04-testing.md).
