# Guidelines for mods

The goal is to never stop the music, so your mod must stay stable. A mod runs inside EP147,
and if it causes a crash, the core switches that mod off at the next start.

`azmod-pack validate` checks everything that can be checked from the file. A reviewer looks
at the rest before a repository joins the Mod Index.

## The musts

### 1. Unknown means do nothing

Deck numbers run from `0` to `3` (deck 1 is `0`), and `-1` means unknown. Services that
return a length or text return `0` or an empty string when they do not know. When a value is
unknown, the mod skips this round instead of substituting deck 1, the first match or a guess.

- A pointer kept from earlier may point at something EP147 has since rebuilt, and loading a
  track rebuilds a lot. Check the pointer when you use it, not only when you store it.
- In a hook, an object whose deck is not certain passes through untouched.

*Why:* a fallback to deck 1 works on deck 1, where it gets tested, and then changes the wrong
deck on stage.

### 2. Set up in `init()`, not in a constructor

Do all setup in `init()`. Don't use `__attribute__((constructor))` or C++ globals whose
constructors do real work.

*Why:* the core's services do not exist yet when a constructor runs, and a constructor cannot
report a failure. When `init()` returns non-zero, the Mod Menu shows that as the reason, and
the deck carries on without the mod.

### 3. Keep the busy paths fast

The callbacks below run on EP147's busy paths: the screen, the controls and, through them,
the audio.

| Busy path | Thread |
|---|---|
| `tick()`, `on_draw()`, surface `on_paint()` and `on_input()`, row, deck-event, confirm and `post_ui` callbacks | the core's UI thread |
| `on_frame()`, frame filters, touch filters | the control bus threads (real-time priority) |
| audio tap callbacks | the core's tap thread |
| hooks | whichever EP147 thread calls the function |

A busy-path callback reads a few values, updates state and returns. It does not block or
sleep, access files or the network, start processes or make large allocations, and it logs
only when something changes.

- Do file, network and library work in `init`, or on a thread started with `thread_start`.
  `post_ui` hands the result back to the UI thread.
- Share state between a busy path on another thread and the UI thread as plain words, read
  and written with atomics (`__atomic_load_n`, `__atomic_store_n`). Don't hold a lock for
  long, and don't share a structure that can be seen half-updated.
- Don't call `fork`, `system` or `popen`: they copy the whole EP147 process. Start other
  programs with `launch` or `handoff`. They run outside EP147, where a crash or a stall cannot
  reach the music.
- Don't let a C++ exception leave the mod's code. Catch exceptions in every callback.

*Why:* a single file read per call on a render path has frozen the deck. Filters are timed,
and one that takes over 200 µs on 8 calls is switched off for the session.

### 4. Export one symbol

Build with `-fvisibility=hidden`, as the template does, and export only `az_mod_v1_entry`.
Don't export a name the core interposes: `read`, `open`, `ioctl`, `sqlite3_*` and the others
that `azmod-pack validate` lists. A private, unexported helper with such a name is fine. Both
the validator and the deck refuse a library that exports one.

*Why:* every library in EP147 shares one set of names. When two libraries both export a
`scale()`, both silently call the same one. A library that defines `read()` takes over EP147's
input for the whole process.

### 5. Hooks only on the firmware you tested

A services-only mod declares `"firmware": ["any"]`. A mod that patches EP147's code declares
`"hooks": true` and the exact firmware tags it was tested on, both in `azmod.json` and in the
descriptor's `fw_tags` ([API reference](API.md#your-descriptor-az_mod_v1)). The core loads it
on no other firmware.

- Hook only through `claim_hook` and `claim_vslot`; an address has one owner. A refused claim
  means running without that feature or returning non-zero from `init()`.
- Claim hooks last in `init()`, after everything that can fail: a hook cannot be undone
  while EP147 runs.

How to declare and claim hooks safely is covered in
[Hooks and firmware tags](05-hooks-and-firmware.md).

### 6. Share your source, never the deck's firmware

Every listed mod links its public source from `"source"` in `azmod.json`, under the GPL-3.0
([License](01-getting-started.md#license)).

Don't put any of these in a repository, package or issue: firmware or update files (`.UPD`)
or anything extracted from them, encryption or signing keys, decompiled code from the deck,
or the deck's own images and fonts. Firmware tags and hooked addresses are facts about the
firmware, not copies of it, so they are fine.

*Why:* public source is what lets users and reviewers trust a library that runs with full
access to their deck. The deck's firmware files would put the repository, and the Mod Index,
at risk of being taken down.

## Good habits

### Say what the mod does

`summary` and `description` say what the mod changes. If the mod uses the network, writes
files, changes controls, LEDs or audio, or patches code, they say that too.

### Files on the USB, not the deck

Keep the mod's files on the USB stick. The deck's settings partition is about 61 MB, shared
with the deck's own settings and AZ-MODS. Filling it once cost the deck its BPM display until
space was freed. The USB has gigabytes.

| Data | Where |
|---|---|
| settings | `cfg_get` / `cfg_set` (numbers), `cfg_get_str` / `cfg_set_str` (short text) |
| files the mod writes | `data_dir()`: a folder on the USB, created by the core |
| files the mod ships (samples, images, presets) | `assets/` in the package, read through `assets_dir()` |

- `data_dir()` and `assets_dir()` return `NULL` while the USB is out. The mod keeps running
  from the deck's copy and carries on without its files.
- Don't create those folders by hand. After an eject the empty mount point stays behind, and a
  folder made there lands on the deck's own storage.
- Open a file, read or write it, and close it. A file left open on the USB makes the deck's
  eject wait.
- Open files with close-on-exec (`fopen(path, "re")`, `"we"`, `"ae"`, `open(..., O_CLOEXEC)`,
  `SOCK_CLOEXEC`). EP147 starts helper processes constantly, and they inherit every
  descriptor opened without it, which keeps the USB busy when the user ejects it.
- Keep `data_dir()` modest, because the USB also holds the user's music.

`azmod-pack validate` warns when a library names the settings partition.

### Memory and threads

A cache that grows with every track loaded becomes a crash hours into a set.

A thread the mod starts stays quiet when there is nothing to do, and doesn't make EP147's
threads wait for it. The core cannot stop threads or remove taps and filters, so each one
needs a way to go idle, such as a flag that a row or setting controls.

### Test the way a deck is used

The [test harness](04-testing.md#the-test-harness) covers these automatically. Check them on
a deck or the emulator too:

- every deck, decks 3 and 4 included, and an empty deck
- the 2-deck and 4-deck views
- loading, ejecting and swapping tracks
- the Day theme and a dark theme
- a long session

## Checklist before you publish

- [ ] `azmod-pack validate` passes, and its warnings are read.
- [ ] `make test` passes all three runs: plain, AddressSanitizer + UBSan, ThreadSanitizer.
- [ ] Setup is in `init()`, not a constructor.
- [ ] The busy paths do no file or network work and log only on a change.
- [ ] No `fork`, `system` or `popen`; other programs start with `launch` or `handoff`.
- [ ] Every deck lookup does nothing on "unknown".
- [ ] `"firmware"` in `azmod.json` and `fw_tags` agree; `"hooks": true` only with exact tags.
- [ ] `"source"` is public with a `LICENSE`; no firmware files, keys or decompiled code anywhere.
- [ ] The description says what the mod changes.
- [ ] Tested on a deck: 2-deck and 4-deck views, all decks, empty decks, Day and dark, a long session.
