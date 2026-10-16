# Testing on your deck

Testing on a deck needs an XDJ-AZ running AZ-MODS 1.2.3 or newer and a USB stick set up by
the AZ-MODS desktop app. To work without a deck, use the [XDJ-AZ emulator](08-emulator.md),
which runs the same core.

## The test harness

`tools/harness/` loads a mod on your computer the same way the core does (`dlopen`,
`az_mod_v1_entry`, `init`), in a child process against a stand-in core. Each run soaks the
mod for thousands of ticks, covering decks loading, ejecting and changing tempo every tick; unknown,
negative, NaN, very long, Unicode and invalid UTF-8 values; page areas from full screen down
to 120x60; theme switches; clock steps; random knob, click, tap and BACK input; deck events;
screen changes; and a reload in the middle. The clock is simulated (the harness defines
`clock_gettime`, `time` and `gettimeofday`), 33.3 ms per tick.

| Check | Fails when |
|---|---|
| `init` | `init()` returns non-zero, or registers no page, surface or deck-event listener |
| `no-crash` | the child dies from a signal or a sanitizer report |
| `draw-area` | anything is drawn outside the page or surface area |
| `draw-context` | a drawing service is called outside `on_draw` / `on_paint`, or with another call's context |
| `text-size` | a size outside 8 to 96 reaches `draw_text` or `text_width` |
| `no-io` | `tick`, a page, surface, deck-event, confirm or `post_ui` callback makes a file, process, sleep or network call (Linux only) |
| `no-alloc` | those callbacks allocate on the heap (Linux only) |
| `redraw-rate` | `tick` asks for more repaints a second than `MAX_REDRAWS` in the Makefile allows |
| `log-quiet` | more than 30 log lines in a 1000-tick soak |
| `api` | a deck number outside 0 to 3, a bad buffer size or settings key, `claim_hook`, `data_dir` / `assets_dir` outside `init`, a registration outside `init` |

A mod should pass all three runs:

| Run | Built with | Also catches |
|---|---|---|
| `plain` | no instrumentation | |
| `asan` | AddressSanitizer + UndefinedBehaviorSanitizer | overflows, use after free, signed overflow, out-of-range float-to-int casts |
| `tsan` | ThreadSanitizer | data races between a mod's threads and its UI-thread callbacks |

A sanitizer report is a bug in the mod even if the deck has not crashed on it: on the deck,
the memory it corrupts belongs to EP147.

### Running it

From the mod's folder:

- macOS or Linux (with clang):
  ```
  make test AZ_SDK=path/to/az-mods-sdk
  ```
- Windows (Docker Desktop). The SDK's build container has no clang, so this installs it in a
  stock Ubuntu container on each run:
  ```powershell
  docker run --rm -v "${PWD}:/mod" -v "C:\path\to\az-mods-sdk:/sdk:ro" -w /mod ubuntu:24.04 `
    sh -c "apt-get update -qq && apt-get install -y -qq clang make >/dev/null && make test AZ_SDK=/sdk"
  ```

`make test` runs all three passes: plain, asan and tsan. To run only one, add `SAN=plain`,
`SAN=asan` or `SAN=tsan`.

The output, per pass:

```
== plain
hello-az (plain): 10 checks passed, not measured: functional
ALL SANITIZER GATES PASS
```

That is the plain pass on Linux, and therefore in the Windows container. On macOS, and in the
`asan` and `tsan` passes on every host, it reads `8 checks passed, not measured: no-io no-alloc
functional`.

| Part | Meaning |
|---|---|
| `10 checks passed` | the checks in the table above that ran and passed |
| `not measured: ...` | checks that did not run here: `no-io` and `no-alloc` on macOS (the counter, `iocount.c`, interposes glibc) and under the sanitizers (they bring their own allocator); `functional`, the mod's own scenarios, when no scenarios file was given (`--scenarios`) |
| `MISMATCH: ...` | a check that failed, with its detail and, for a sanitizer, the report and a stack trace into the source |

Each `sanitize.sh` invocation (one `make test`, however many passes) ends with a single
verdict, `ALL SANITIZER GATES PASS` or `SOME SANITIZER GATES FAILED`, and exits non-zero on
the second.

If the tsan pass stops with `FATAL: ThreadSanitizer: unexpected memory mapping`, the host's
`vm.mmap_rnd_bits` is above 28: set it to 28 (as the SDK's CI does), or run the container
with `--privileged` and `setarch -R`.

To compile scenarios for a specific mod (open a page, press a button, check what was drawn)
into the harness, use `sanitize.sh --scenarios FILE`; see `harness_scenarios` in
`tools/harness/harness.c` and `tools/harness/sanitize.sh --help`. The SDK's CI runs the
template and a set of planted bugs (`tools/harness/planted.c`) under all three runs and fails
if any planted bug goes uncaught.

## SSH access

1. In the AZ-MODS desktop app, with the USB in the computer, open the USB's **Optional**
   settings > **SSH access** (*Developer*). The first time, the app creates a key pair, writes
   the public key to the USB and shows where the private key is (by default
   `~/.azmods/ssh/xdjaz_ecdsa`). That key is the only way in.
2. Power the deck on with the USB inserted.
3. Once no deck has a track loaded, the deck asks **Remote access** and shows the key's
   fingerprint. **Allow** records the key on the deck. Cancel leaves it waiting, and the deck
   asks again at the next start.
4. Connect with `ssh -i ~/.azmods/ssh/xdjaz_ecdsa root@DECK-IP`. The address is in the
   router's device list or the deck's network settings.

Access is key-only. Unticking **SSH access** turns SSH off at the next power-on with that
USB.

## Installing a build

### By copying it to the USB

```
<stick>/MODS/
    azmods.cfg               the settings file: community-mods.beat-counter = on
    community-mods/
        beat-counter/
            azmod.json
            beat-counter.so  out/<id>.so from a build
            icon.png
            assets/          if the mod ships any
            data/            made by the deck for the mod's own files (data_dir); never copied
```

1. Copy the files above into `MODS/community-mods/<id>/`, or unzip `dist/<id>-<version>.azmod`
   there (it holds exactly these files).
2. Add the line `community-mods.<id> = on` to `MODS/azmods.cfg`.
3. Power the deck on with the USB inserted.
4. In **Mod Menu** (the MOD MENU row on the SOURCE screen) > **Community Mods > Installed**,
   the mod shows **New mod found: allow?**. Open it, press **Allow**, then **Reload now**.

At power-on, the AZ-MODS loader copies `MODS/community-mods/` (without each mod's `assets/` and `data/`) to the
deck's own storage, and the core loads enabled mods from that copy. It loads only mods the
deck has approved. The deck keeps its own list of approved mods, each with the SHA-256 of its
`.so`, so a USB alone cannot make a deck run code. A new build has a new hash, so the deck
asks once.

| Action | Copies `MODS/community-mods/` from the USB | Restarts EP147 |
|---|---|---|
| Power-on | yes | |
| **Reload now** on the Community Mods page (after Allow, an install or an update) | yes | yes |
| **Reload** in the Mod Menu, or a mod's `reload()` | no | yes |

### Over SSH

Each time EP147 starts, the core loads mods from `/home/root/settings/az-usbload/community/`,
so you can swap a build in there and restart EP147:

```sh
# on your computer: upload next to the loaded library
scp -i path/to/private-key out/beat-counter.so \
    root@DECK-IP:/home/root/settings/az-usbload/community/beat-counter/beat-counter.so.new

# on the deck
cd /home/root/settings/az-usbload/community/beat-counter
mv beat-counter.so.new beat-counter.so
# approve this build (the last line for an id wins)
echo "beat-counter $(sha256sum beat-counter.so | cut -d' ' -f1)" >> /home/root/settings/azmod-community/approved
rm -f /home/root/settings/azmod-community/.starting   # this restart is intended, not a crash
systemctl restart EP147
```

Why each step is there:

- Upload as `.new`, then `mv`: copying over the mapped `.so` changes its code under the
  running EP147, which crashes it.
- The `approved` line: without it, the build shows **New mod found: allow?** after the restart.
- `rm .starting`: two starts in a row that end within 90 seconds read as a crash loop, and the
  core holds every mod back ([below](#crashes-quarantine-and-recovery)).

- The USB wins at the next power-on, so copy the final build to the USB too.
- That folder is on the deck's settings partition (about 61 MB, shared with the deck's own
  settings). Keep only the mod's files there; data belongs in `data_dir()` on the USB
  ([API reference](API.md#files-your-folders-on-the-usb-stick)).
- An SSH session may stall while EP147 restarts.

`systemctl show EP147 -p NRestarts` counts the restarts systemd made on its own (crashes) since
the last requested one, so a Mod Menu **Reload** resets it to 0. For a longer test, count crash
records instead: `grep -c '=== azmod crash' /tmp/azmod-crash.log`.

## Load results and the log

**Mod Menu > Community Mods > Installed** shows each mod's result. Over SSH, the same list is in
`/tmp/azmod-community/loaded`: `fw=<this deck's tag>`, `suspended=` and `off=` come first,
followed by one `mod=<id>|<state>|<version>|<reason>` line per mod.

| Shown | Cause |
|---|---|
| `Loaded` | running |
| `New mod found: allow?` | this deck has not approved this exact `.so` (copied by hand, installed by the desktop app, or a new build) |
| `Turned off` | switched off in the Mod Menu, or `community-mods.<id> = off` in `MODS/azmods.cfg` |
| `Community mods are turned off` | the Community Mods row is off, or `community-mods = off` in `MODS/azmods.cfg` |
| `Not made for this firmware` | `firmware` does not list this deck's tag |
| `This deck's firmware could not be identified` | the core could not hash EP147's code, so only `"any"` would match and it is not listed |
| `Needs AZ-MODS <min_core> or newer` | `min_core` is newer than this core |
| `Made for mod API <n>; this AZ-MODS has 1` | `api` in `azmod.json` is not `1` |
| `No source link in azmod.json` | `source` is missing |
| `Says "any" firmware but also patches code` | `"firmware": ["any"]` with `"hooks": true` |
| `Missing azmod.json`, `azmod.json is for a different mod id`, `azmod.json ...` | the manifest is missing, names another id, is not one flat JSON object, or has more than 48 keys (`azmod.json has too many keys`) |
| `Not a 64-bit ELF library`, `Built for the wrong CPU (not aarch64)`, `Not a shared library` | not built with the SDK |
| `Exports <name>(), which only AZ-MODS may define`, `Exports <name>; a mod exports only az_mod_v1_entry` | build with `-fvisibility=hidden` ([Guidelines](GUIDELINES.md#4-export-one-symbol)) |
| `Links <lib>, which a mod may not use` | a library outside the allowed list ([FAQ](07-faq.md#can-i-use-another-library)) |
| `Could not load: <reason>` | `dlopen` failed, for example a symbol version the deck does not have |
| `Built for a different mod API`, `The library's id does not match azmod.json`, `The library is not built for this firmware`, `The library has no init()` | the descriptor disagrees with `azmod.json` |
| `Its init() returned <n>` | `init` returned non-zero (the number is the mod's); `(after patching code; reload to clear)` when it had claimed hooks |
| `Suspended: ...` | see [Crashes](#crashes-quarantine-and-recovery) |

### The log

Each mod logs to `/tmp/azmod-community/<id>.log`. The core always writes the mod's load result
and every refused hook or service call there. The mod's own `log()` lines are written only
while `/home/root/settings/azmod-logs.on` exists, and the core checks for that file once a
second:

```sh
touch /home/root/settings/azmod-logs.on
tail -f /tmp/azmod-community/beat-counter.log
rm /home/root/settings/azmod-logs.on          # when done
```

Each call writes one line, as it happens. For limits, see the [API reference](API.md#log).
`/tmp` is in RAM and is cleared at power-off. Decks in normal use run with logs off.
[Logs](09-logs.md) lists every file a mod's output lands in, on the deck and in the emulator.

## Crashes, quarantine and recovery

| What happened | At the next start | To clear it |
|---|---|---|
| EP147 crashed with a mod's code on the faulting address, the return address or one of up to 16 stack frames | that mod alone: `Suspended: it crashed the deck` | the mod's page: **Try again**, then **Reload now** |
| EP147 froze or died inside a mod's `init()` | that mod alone: `Suspended: the deck stopped while this mod was starting` | the same |
| two starts in a row that loaded mods each ended within 90 seconds | no mods load; the Community Mods page shows **Resume** / **Keep off** | **Resume** |
| three crashes within 30 minutes that no mod could be blamed for, while mods were loaded | the same | **Resume** |
| EP147 restarted 3 times within 40 seconds without reaching its UI | the loader starts the deck stock: every mod off, official ones too | change the mods on the USB, or delete `MODS/.mods-disabled-crashloop` |

Official AZ-MODS features keep working in the first four cases. A crash outside a mod's own
code (for example, in memory the mod corrupted that EP147 later trips over) cannot be blamed on
one mod. Turning mods off one at a time finds the cause.

### Finding where it crashed

Each crash appends one block to `/tmp/azmod-crash.log`. A blamed crash names the mod
(`community mod: beat-counter`), and its `maps:` list includes the mod's library. The fault's
offset in the library is the `pc` minus the start of the library's `r-xp` line:

```
community mod: beat-counter
pc 0x0000ffff7da0af8c ...
maps:
ffff7da0a000-ffff7da0c000 r-xp 00000000 ... /community/beat-counter/beat-counter.so
```

```sh
# 0xffff7da0af8c - 0xffff7da0a000 = 0xf8c, looked up in the exact build that was installed
docker run --rm --platform linux/arm64 -v "$PWD":/m az-mods-sdk:1 addr2line -f -e /m/out/beat-counter.so 0xf8c
```

Run this from the mod's folder. The `-v` mount depends on the shell:

| Shell | `-v` |
|---|---|
| macOS, Linux, WSL | `-v "$PWD":/m` |
| PowerShell | `-v "${PWD}:/m"` |
| Git Bash | `-v "$(pwd -W)":/m`, with `MSYS_NO_PATHCONV=1` before `docker` (otherwise `/m` is rewritten into a Windows path) |

This prints only the function name, with no file or line, because the release build has no
debug info. To get the file and line, build the same source with `-g` into another folder and
look the offset up there. `-g` adds debug information without changing the generated code, so
the offsets match the installed build:

```sh
azmod-pack build . --no-validate -- DEBUG=1 OUT=out-debug
docker run --rm --platform linux/arm64 -v "$PWD":/m az-mods-sdk:1 addr2line -f -i -e /m/out-debug/beat-counter.so 0xf8c
```

(Use the same `-v` form for your shell as above.)

The `frames:` lines are return addresses further up the stack. Apply the same subtraction to
each one that falls inside the library. Copy the log off the deck before powering it off.

### Turning mods off

Changes on the USB apply at the next power-on with the USB inserted. Changes in the Mod
Menu apply at the next start of EP147.

| Scope | In the Mod Menu | On the USB |
|---|---|---|
| one mod | switch it off under Community Mods > Installed | `community-mods.<id> = off` in `MODS/azmods.cfg` |
| every mod (official AZ-MODS features stay on) | untick the **Community Mods** row | `community-mods = off` in `MODS/azmods.cfg` |
| remove a mod | | delete `MODS/community-mods/<id>/` |

`MODS/azmods.cfg` holds every AZ-MODS setting and travels with the USB; Mod Menu changes
are written back to it. The boot loader reads it before EP147 starts, so a mod that makes the
deck unusable can be switched off from a computer.

## Before you publish

- [ ] All three harness runs pass.
- [ ] The 2-deck and the 4-deck view, and switching between them.
- [ ] Every deck, not only deck 1.
- [ ] Empty decks; loading, ejecting and swapping tracks while the mod runs.
- [ ] The Day theme and a dark theme (Mod Menu > Themes), switched while its page is open.
- [ ] Its page: open, use, leave with BACK, open again.
- [ ] An hour or more with tracks playing, watching for stutter in the screen or the audio.
- [ ] On a firmware the mod does not list, if one is at hand, the mod is refused cleanly.
- [ ] An install from the published repository (Mod Menu > Community Mods > Repositories),
      not only a copy by hand ([Packaging and publishing](06-publishing.md)).

**Next:** [Hooks and firmware tags](05-hooks-and-firmware.md) for a mod that changes EP147's
own code, or [Packaging and publishing](06-publishing.md).
