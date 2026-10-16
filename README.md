# AZ-MODS SDK

This SDK builds mods for the XDJ-AZ running AZ-MODS. It contains the header,
the build container, a template mod and the packaging tool.

A mod is a shared library (`<id>.so`), a manifest and an icon, packed into one `.azmod`
file. Users browse and install mods on the deck (Mod Menu > Community Mods) or with the
AZ-MODS desktop app. The AZ-MODS core loads each installed mod into EP147 and gives it these
services: settings and a log, rows and a page in the Mod Menu, deck state and events, the
control buses and the touch screen, surfaces (overlays on EP147's screen), other programs,
the rekordbox library, and each deck's audio.

The [developer guide](docs/README.md) covers setup on macOS, Windows and Linux, the first
mod, testing, hooks, publishing and the FAQ. The [Guidelines](docs/GUIDELINES.md) cover
what keeps a mod stable.
The same guide is on the web at
[kyle-hosman.github.io/az-mods-sdk](https://kyle-hosman.github.io/az-mods-sdk/).

## Contents

| Path | |
|---|---|
| `include/azmod-mod.h` | the one header a mod includes (ABI version 1) |
| `docker/Dockerfile` | the build container: the C and C++ compilers and C library official AZ-MODS releases use |
| `template/` | `hello-az`, a complete mod: a settings page with a checkbox, and a live BPM and title readout for each deck |
| `template/.github/workflows/release.yml` | push a tag, get a release with the `.azmod` attached |
| `tools/azmod-pack` | `new`, `build`, `image`, `validate`, `package`, `repo add`, `repo check`, `list check` |
| `tools/harness/` | the host test harness: plain, AddressSanitizer + UBSan, ThreadSanitizer |
| `tests/cxx/hello-cxx/` | a C++ mod: proof that C++ builds, validates and passes the harness |
| `docs/` | the developer guide, the Guidelines and the API reference; also the source of the AZ-MODS Docs website (`mkdocs.yml`) |

## Requirements

| | |
|---|---|
| Docker with `linux/arm64` | native on Mac (Apple Silicon) and arm64 Linux; emulated by Docker Desktop on Intel Macs and Windows; on x86_64 Linux, install the arm64 emulator once ([setup](docs/02-setup.md)) |
| Python 3.8+ | `azmod-pack` and the SDK's tools, standard library only (3.11+ only to build the docs website) |
| git | |
| clang (clang++ for C++) | the test harness (`make test`); on Windows in a container ([Running it](docs/04-testing.md#running-it)) |

## Quickstart

Install Docker, Python 3 and git as described in [Set up your environment](docs/02-setup.md),
then run:

```sh
git clone https://github.com/Kyle-Hosman/az-mods-sdk
cd az-mods-sdk

# Windows (PowerShell): tools\azmod-pack.cmd in place of tools/azmod-pack

# 1. a new mod from the template, under your own id and GitHub user
tools/azmod-pack new ../beat-counter --id beat-counter --author your-name

# 2. build it in the container (the first run also builds the container), then validate
tools/azmod-pack build ../beat-counter

# 3. package it for testing: beat-counter-0.1.0.azmod and its icon, in ../beat-counter/dist/
tools/azmod-pack package ../beat-counter
```

To install `dist/beat-counter-0.1.0.azmod` on a deck, see [Testing on your deck](docs/04-testing.md).

A published mod is a GitHub release that users add by repository, and it can also go in the
Mod Index: see [Packaging and publishing](docs/06-publishing.md). `azmod-pack` refuses to
build a package for publishing (`--github OWNER/REPO`) until `summary`, `description` and
`icon.png` are the mod's own, because users read them before installing. `source` points at the
mod's public source.

## The tool

```
azmod-pack new DIR --id ID [--name NAME] [--author USER]   start from the template
azmod-pack build [MODDIR] [--image IMAGE] [--any-image] [--no-validate]   build, then validate
azmod-pack image [--image IMAGE]                            build or rebuild the container
azmod-pack validate MODDIR | FILE.azmod                     check manifest, library and icon
azmod-pack package [MODDIR] [--github OWNER/REPO] [--tag TAG] [--out DIR]
                   [--package-url URL] [--icon-url URL]      (hosting outside GitHub releases)
azmod-pack repo add REPO.json ENTRY.json                   add or update a mod in a repository file
azmod-pack repo check REPO.json | OWNER/REPO [--download] [--cafile PEM]   check a repository file
azmod-pack list check azmods-repos.json [--download] [--cafile PEM]   check the Mod Index
```

`validate` runs every check a deck runs before it loads a mod: the manifest, an aarch64
shared library, the entry point as the only export, none of the names the core interposes,
only the allowed libraries, and at most 16 MB. A mod that passes is not refused on the deck
for any of these reasons.

It also checks what a deck discovers only when loading fails, or never: no symbol version
newer than the deck's C library, no RPATH or text relocations, and the icon. The full list is
in the [`azmod-pack` reference](docs/06-publishing.md#azmod-pack-reference).

## Reproducible builds

The same source builds to the same bytes on any machine. The container pins its base image
and every toolchain package, and every build mounts the mod at the same path. Anyone can
check that a published package was built from the source it links to:

```sh
tools/azmod-pack build path/to/mod
sha256sum path/to/mod/out/<id>.so     # macOS: shasum -a 256; compare with the .so inside the .azmod
```

## The SDK's own tests

```sh
python3 -m unittest discover -s tests -v      # Windows: python
```

The libraries in `tests/fixtures/bin/` are real builds from the container
(`tests/fixtures/build.sh`): one good mod, and one for each refusal reason.
[CONTRIBUTING.md](CONTRIBUTING.md) lists every check CI runs.

## Related repositories

| Repository | |
|---|---|
| [xdj-az-mods](https://github.com/Kyle-Hosman/xdj-az-mods) | AZ-MODS itself: install, features, releases |
| [xdj-az-emu](https://github.com/Kyle-Hosman/xdj-az-emu) | the XDJ-AZ emulator: EP147 on a computer |
| [az-mods-examples](https://github.com/Kyle-Hosman/az-mods-examples) | example mods built with the SDK |
| [az-mods-index](https://github.com/Kyle-Hosman/az-mods-index) | the Mod Index of repositories decks show by default |

## License

GPL-3.0-only. See [LICENSE](LICENSE). Copyright (C) 2026 Kyle Hosman.

Mods are GPL-3.0 as well. A mod includes the SDK's header and runs inside the AZ-MODS core,
which is GPL-3.0, so a published mod ships its source under the GPL-3.0. For a summary, see
[Getting started](docs/01-getting-started.md#license).

## The docs website

The guide is also published as a website,
[AZ-MODS Docs](https://kyle-hosman.github.io/az-mods-sdk/), which
[MkDocs Material](https://squidfunk.github.io/mkdocs-material/) builds from `docs/`:

```sh
pip install -r docs/requirements.txt   # Python 3.11 or newer
mkdocs serve            # http://127.0.0.1:8000, rebuilds on every edit
mkdocs build --strict   # what CI runs: a broken link or page fails the build
```

Every page also reads correctly on GitHub. On the website, links to files outside `docs/`
(such as `../include/azmod-mod.h`) point at GitHub instead (`docs/_hooks.py`).

<p align="center"><sub>Not affiliated with AlphaTheta, Pioneer DJ or rekordbox; trademarks belong to their owners.</sub></p>
