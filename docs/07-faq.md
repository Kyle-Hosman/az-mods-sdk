# FAQ and troubleshooting

## Build problems

| Message | Cause and fix |
|---|---|
| `exec format error`, `This machine cannot run arm64 containers yet` | on x86_64 Linux: `docker run --privileged --rm tonistiigi/binfmt --install arm64`. Docker Desktop includes it; if the error appears there, update Docker Desktop |
| `Docker cannot see <folder>` | the folder is outside what Docker Desktop shares: move it under your home folder, or add it under Settings > Resources > File sharing |
| `/bin/sh^M: bad interpreter`, `missing separator` | a file has CRLF line endings: save it with LF. For a whole clone: `git rm --cached -r . && git reset --hard` |
| `REFUSED: exports <name>: only az_mod_v1_entry may be exported` | build with the template's Makefile (`-fvisibility=hidden`) and make helpers `static` |
| `REFUSED: needs GLIBC_2.30 from libc.so.6; the deck has up to GLIBC_2.29` | built outside the SDK container: build with `azmod-pack build` (the container's glibc 2.27 cannot produce newer symbol versions) |
| `undefined reference to pthread_create` (or `pthread_mutex_...`) | add `-lpthread` to `LDLIBS`: the deck's glibc keeps threads in a separate library. Math functions (`sinf`, `powf`) need `-lm` |

Builds are slower when the container runs under emulation (Intel Mac, x86_64 PC), and the first
build is the slowest because it also builds the container. On Windows, a mod inside the WSL
file system builds much faster than one under `/mnt/c`.

## Writing a mod

### Can I use C++?

Yes. The build container has g++ 7.5, the same GCC as the C compiler. In the template's
Makefile, list `.cpp` or `.cc` files in `SRCS` (C and C++ can be mixed); they compile with
`g++` and the library links with it. [`tests/cxx/hello-cxx`](https://github.com/Kyle-Hosman/az-mods-sdk/tree/main/tests/cxx/hello-cxx/) is a
complete C++ mod, built, validated and run under the harness by the SDK's CI.

| How a C++ mod links | Why |
|---|---|
| against the deck's own `libstdc++.so.6` and `libgcc_s.so.1` | EP147 is C++ and already has them loaded: one C++ runtime and one unwinder in the process. The deck's is 6.0.25 (up to `GLIBCXX_3.4.25`); GCC 7 needs at most `GLIBCXX_3.4.24` |
| not with `-static-libstdc++` | it puts a second copy of the runtime inside EP147 and exports its symbols from the mod |
| with a version script exporting only `az_mod_v1_entry` | the standard headers give their templates default visibility, so `-fvisibility=hidden` alone exports instances such as `std::vector<int>::~vector`, and the deck refuses the library (`Exports _ZNSt...; a mod exports only az_mod_v1_entry`) |

The Makefile does all three. In the source:

- The descriptor is aggregate-initialized in field order (C++14 has no designated
  initializers); `az_mod_v1_entry` is `extern "C"` through `azmod-mod.h`.
- Avoid global objects whose constructors do work; do setup in `init()`
  ([Guidelines](GUIDELINES.md#2-set-up-in-init-not-in-a-constructor)).
- Keep exceptions inside the mod: every callback catches what it can throw
  ([Guidelines](GUIDELINES.md#3-keep-the-busy-paths-fast)).
- `make test` builds C++ sources with `clang++` (`TEST_CXX` overrides it).

An `az-mods-sdk:1` image built before the SDK added g++ has no C++ compiler. `azmod-pack build`
refuses any image that was not built from the SDK's current `docker/`, and prints the command
that rebuilds it ([The SDK image](02-setup.md#the-sdk-image)).

### Can I use another library?

| Allowed to link | Anything else |
|---|---|
| `libc`, `libm`, `libdl`, `libpthread`, `librt`, `libgcc_s`, `libstdc++` | compile it into the mod from source, under a license compatible with the GPL-3.0 |

X11, FreeType and ALSA are refused: the core owns the display, the font and the audio device.
Drawing goes through the drawing services, audio through `audio_tap`.

### How do I show the time of day?

The deck keeps its clock in UTC and has no time zone setting, so `localtime` gives UTC. The
mod can add its own setting for the offset from UTC, as Set Clock in
[az-mods-examples](https://github.com/Kyle-Hosman/az-mods-examples) does. A mod that calls
`localtime_r` or `strftime` anyway should call `tzset()` in `init`: glibc reads the zone file
the first time it needs it, and that read must not land in `tick` or `on_draw`. Measure
durations with `clock_gettime(CLOCK_MONOTONIC, ...)`, which does not jump when the deck sets its clock from
the network.

### How often does `tick()` run?

Periodically, up to about 30 times a second. The rate varies, so time anything with a clock
rather than by counting ticks. A mod that only reacts to events can set `.tick = NULL`
([How a mod runs](03-first-mod.md#5-how-a-mod-runs)).

### Can my mod use the network or write files?

Yes, from `init` or from a thread of its own, but not from `tick`, `on_frame`, a filter, a
page or a hook. The mod's description says that it does. Files it writes go in `data_dir()`,
on the USB; files the mod ships go in `assets/` ([API reference](API.md#files-your-folders-on-the-usb-stick)).

### Can my mod draw over EP147's screens, outside the Mod Menu?

Yes, with an overlay (`surface_create`): an opaque rectangle the mod paints over EP147's
screen, on the views it names, below or above the Mod Menu
([API reference](API.md#what-is-on-screen-and-surfaces)). A program the mod launched can also
show its own window with `xwin_show`.

### How many mods can be enabled?

Up to 16. The core loads the first 16 `community-mods.<id> = on` lines in `MODS/azmods.cfg`.

## On the deck

### My mod is not in the Installed list

Check that `MODS/azmods.cfg` has the line `community-mods.<id> = on`, that the folder name, the
`id` in `azmod.json` and `MOD_ID` in the code are identical, and that the deck was powered on
with the USB inserted. The USB is copied to the deck at power-on and by **Reload now**, not by
the Mod Menu's Reload row.

### Every new build shows `New mod found: allow?`

A deck runs only the exact `.so` files it has approved, so each new build asks once. Over SSH, a build
can be approved as it is copied ([Over SSH](04-testing.md#over-ssh)); on the emulator,
`az-push-mod.py` approves it.

### My log is empty

The mod's own `log()` lines are written only while `/home/root/settings/azmod-logs.on`
exists. The load result is always written.

### My settings are not saved

A key is at most 31 characters, and a mod has up to 32 keys (16 more for text); `cfg_set` ignores a
key it cannot store. Values are kept as `<id>:<key>` lines in `MODS/azmods.cfg` on the USB. A
change is written there within about 250 ms after values stop changing, at most every 5 seconds while
they keep changing, and at once when the user leaves the mod's page or reloads.

### The deck restarted and now all mods are off

This is the quarantine: see [Crashes, quarantine and recovery](04-testing.md#crashes-quarantine-and-recovery).

## Publishing

### My mod does not show up in Browse

Run `azmod-pack repo check OWNER/REPO --download`. Then check that `version` is
`MAJOR.MINOR.PATCH` (an entry with any other version is skipped) and that no other repository
the deck reads offers the same id (a deck shows each id once). Browse lists at most 128 mods. A deck reads the Mod Index and the repository files again
when Community Mods opens (at most every 10 minutes, at once after a failed read).

### Do users have to restart after installing?

Yes. Mods load when EP147 starts, so an install, update, Allow or Turn off takes effect at the
next **Reload now**. The deck asks first, because a reload stops the music and unloads the
tracks.

### The deck says the package does not match

A release asset was replaced after it was published, so its `sha256` or `size` no longer
matches the repository file. Release a new version instead of replacing a package.

### Do I have to be in the Mod Index?

No. Any user can add a repository on the deck by its name. The Mod Index decides which
repositories every deck shows by default.

### Where do I ask for help?

Ask questions on the [AZ-MODS Discord](https://discord.gg/SGBxQPK24J). For a bug or a
concrete request, open an
[issue on the SDK repository](https://github.com/Kyle-Hosman/az-mods-sdk/issues).
