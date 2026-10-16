# Getting started

Mods load into EP147, the deck's main program. Users install them and switch them on from the
deck (Mod Menu > Community Mods). A mod is packed as one `.azmod` file and published as a
GitHub release. The package holds three files:

| File | Contents |
|---|---|
| `<id>.so` | the mod, in C or C++ |
| `azmod.json` | manifest: id, name, version, author, source URL, firmware tags |
| `icon.png` | 256x256, shown in the Mod Menu |

## How it runs

When EP147 starts, the core loads each enabled mod and calls its `init()` once with the
service table. From then on the mod runs from callbacks. An optional `tick()` handles
periodic work and runs up to about 30 times a second, so time things with a clock rather than
by counting ticks. Everything else runs from what the mod registered in `init()`: deck
events, frame and touch filters, surface paints and its own threads.

The service table reaches these parts of the deck:

| Service | Reaches |
|---|---|
| Mod Menu | rows in the main list, and a page of the mod's own |
| Surfaces | overlays: opaque rectangles the mod paints over EP147's screen, on the views it names |
| Deck state and events | what is loaded and playing, the beat grid, load / eject / play / stop |
| Library | the rekordbox library on each USB, read-only |
| Audio | each deck's output, before the hardware mixer |
| Control buses and touch | deck and mixer frames, the 1280x800 touch screen |
| Settings | per-mod values kept across updates |
| Logs | a capped log file per mod |
| Other programs | launch a program, or hand it the screen and controls |

The [API reference](API.md) documents every function.

## Services or hooks

A mod builds on the services above. It can also hook EP147's own functions, to change how
the deck program itself behaves.

| | What it does | Runs on | After a new official firmware |
|---|---|---|---|
| Services | everything in the table above | every firmware AZ-MODS supports (`"firmware": ["any"]`) | keeps working once AZ-MODS supports that firmware |
| Hooks | changes EP147's own behavior | the exact firmware build it was made for | needs its addresses found again, and a new release |

Hooks are covered in [Hooks and firmware tags](05-hooks-and-firmware.md). To help keep a mod
stable, see the [Guidelines](GUIDELINES.md).

## What you need

An XDJ-AZ with AZ-MODS 1.2.3 or newer, and Docker. The build runs in a container, so there is
no cross-compiler to install and every machine produces the same bytes.
[Set up your environment](02-setup.md) has the rest for each OS.

The [XDJ-AZ emulator](https://github.com/Kyle-Hosman/xdj-az-emu) runs EP147 on a computer, for
building and testing mods before deploying them to the AZ. Setting it up requires SSH access to
the deck, provided by [AZ-MODS](https://github.com/Kyle-Hosman/xdj-az-mods), to copy the deck's
system once ([details](08-emulator.md)).

## Repositories

| Repository | Contents |
|---|---|
| [xdj-az-mods](https://github.com/Kyle-Hosman/xdj-az-mods) | AZ-MODS releases, issues and source, including the mod loader |
| [xdj-az-emu](https://github.com/Kyle-Hosman/xdj-az-emu) | the XDJ-AZ emulator |
| [az-mods-sdk](https://github.com/Kyle-Hosman/az-mods-sdk) | `azmod-mod.h`, the build container, the template mod, `azmod-pack`, these guides |
| [az-mods-examples](https://github.com/Kyle-Hosman/az-mods-examples) | example mods covering the services, with a host test harness |
| [az-mods-index](https://github.com/Kyle-Hosman/az-mods-index) | the Mod Index: repositories every deck shows by default (optional for publishing) |

## License

The SDK, including `azmod-mod.h`, is GPL-3.0-only, and so is the AZ-MODS core a mod runs
inside. A mod includes the header and is loaded into the core, so a mod is GPL-3.0 too.

A published mod ships its source under the GPL-3.0, and its `azmod.json` says so:
`"license": "GPL-3.0-only"` (the template's default) or `"GPL-3.0-or-later"`. Code under a
GPL-compatible license (MIT, BSD, Apache-2.0) can be combined into a mod, and the mod as a
whole is GPL-3.0. Authors keep the copyright to their own code. Every mod in the Mod Index
has public source. The full text is in [LICENSE](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/LICENSE).

**Next:** [Set up your environment](02-setup.md).
