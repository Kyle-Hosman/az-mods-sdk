# Packaging and publishing

A deck finds mods through repositories: public GitHub repositories (or any `https://`
host) with a repository file, `azmods-repo.json`. Publishing a version takes two things: a `.azmod`
package attached to a release, and an entry for it in the repository file on the default branch.

| Route | Users get the mod by | Needs |
|---|---|---|
| a repository | adding it once, then **Browse** | the release and the repository file, nothing else |
| the [Mod Index](#the-mod-index) (optional) | **Browse**, with nothing to add: every deck and the desktop app read the list by default | the same repository, plus a pull request to [az-mods-index](https://github.com/Kyle-Hosman/az-mods-index) |
| a package file | copying it to the USB by hand ([Testing on your deck](04-testing.md#by-copying-it-to-the-usb)) | the `.azmod`; the deck asks **New mod found: allow?** |

## How a deck reads a repository

1. A user adds it on the deck under **Mod Menu > Community Mods > Repositories > Add**, or in
   the AZ-MODS desktop app (**Add repository**), as `owner/repo`, a github.com URL of it, or
   any `https://` address ending in `azmods-repo.json`. Both write the USB's
   `MODS/community-mods/repos.txt`, so a repository added in one shows in the other. Repositories
   in the Mod Index appear without being added.
2. The deck downloads `https://raw.githubusercontent.com/owner/repo/HEAD/azmods-repo.json`,
   the repository file on the default branch.
3. **Browse** shows each mod with the icon, name and author from its entry in the repository file.
4. On install, the deck downloads the entry's `package`, checks its `size` and `sha256`
   against the entry, unpacks it into `MODS/community-mods/<id>/` on the USB, checks its
   `azmod.json`, approves that build on this deck, and offers **Reload now**.

A deck runs only builds it has approved. Installing or updating from the deck's own Mod Menu
approves the build. A mod put on the USB another way (by the desktop app or by hand) shows
**New mod found: allow?** under **Community Mods > Installed**, once per build. Turning a mod
off and on again does not ask.

| Limit on a deck | Value | Beyond it |
|---|---|---|
| Repositories from the Mod Index | the first 64 | ignored |
| Repositories a user adds | 32 | refused |
| Repository file size | 256 KB | the repository shows an error |
| Mods in one repository file | 256 | further entries skipped |
| Entries whose `version` or `min_core` is not `MAJOR.MINOR.PATCH` | | skipped |
| Mods in Browse | 128, installed ones first | "Showing the first N of M mods." |
| Package download | 16 MB, and never more than the entry's `size` | refused |
| Icon | 256 KB | not shown |

Each id appears only once across all repositories (see [Pick an ID that stays yours](#pick-an-id-that-stays-yours)).

## 1. Set up the repository

### One mod per repository

```
your-repo/
    azmod.json
    Makefile
    beat-counter.c
    icon.png
    LICENSE
    azmods-repo.json
    .github/workflows/release.yml
```

`azmod-pack new` creates everything except `azmods-repo.json`.

### Several mods in one repository

```
your-repo/
    azmods-repo.json
    LICENSE  .gitignore  .gitattributes
    beat-counter/          azmod.json, Makefile, beat-counter.c, icon.png, LICENSE, README.md
    loop-meter/            the same for the second mod
    .github/workflows/
        release-beat-counter.yml
        release-loop-meter.yml
```

`azmod-pack new` gives each mod folder its own `.github/`, `.gitignore` and `.gitattributes`,
but these only work at the root of a repository. Make these changes for each mod:

| Change | Why |
|---|---|
| move its `release.yml` to the root's `.github/workflows/` under its own name | GitHub reads workflows only there |
| set `MOD_DIR` to the mod's folder | the workflow builds there |
| keep only `"<id>-*"` in `tags:` | a `v1.0.0` tag would start every mod's workflow |
| keep one `.gitignore` and `.gitattributes` at the root | |

Each mod keeps its own `LICENSE`, which goes into its package.
[az-mods-examples](https://github.com/Kyle-Hosman/az-mods-examples) is laid out this way.

### The repository file

```json
{
  "azmods_repo": 1,
  "name": "Beat Counter",
  "maintainer": "your-name",
  "homepage": "https://github.com/your-name/beat-counter",
  "mods": []
}
```

| Field | |
|---|---|
| `azmods_repo` | `1` |
| `name` | the repository's name as users see it, up to 64 characters |
| `maintainer` | a name or GitHub user, up to 64 characters |
| `homepage` | an `https://` page about the mods |
| `mods` | one entry per mod, written by `azmod-pack package` ([below](#the-repository-file-entry)) |

You can also create the repository file with its first entry: `azmod-pack repo add azmods-repo.json
dist/<id>-<version>.entry.json --name "Beat Counter" --maintainer your-name --homepage
https://github.com/your-name/beat-counter`.

## 2. Release with a tag

First, put the repository on GitHub. This is a one-time step: either create an empty public
repository on github.com and push to it, or create it with the GitHub CLI:

```sh
cd beat-counter
git init -b main
git add .
git commit -m "Beat Counter 0.1.0"
# on github.com: New repository "beat-counter", public, empty; then
git remote add origin https://github.com/your-name/beat-counter.git
git push -u origin main
# or, with the GitHub CLI, in place of the two lines above:
gh repo create your-name/beat-counter --public --source . --push
```

Then, for each release:

```sh
# bump "version" in azmod.json, commit, then:
git tag beat-counter-1.0.0
git push origin beat-counter-1.0.0
```

The tag is `<id>-<version>` or `v<version>`, matching `azmod.json`. The workflow
(`.github/workflows/release.yml`) then:

1. checks the tag against `azmod.json`,
2. builds the mod in the SDK container and validates it,
3. packages it (`beat-counter-1.0.0.azmod` and `beat-counter-1.0.0-icon.png`),
4. creates the GitHub release for the tag and attaches both,
5. prints the repository file entry, and
6. with `UPDATE_INDEX: "true"` and an `azmods-repo.json` at the root, adds the entry and
   commits it to the default branch.

| Setting in the workflow | |
|---|---|
| `SDK_REF` | the SDK release it builds with (`azmod-pack --version` prints the local one); keep it at the version the mod was built and tested with. If `SDK_REF` is not a tag or branch of the SDK repository, the workflow's first step fails and names it |
| `MOD_DIR` | the folder with `azmod.json` and the Makefile |
| `UPDATE_INDEX` | `"true"` to commit the entry |
| `runs-on` | `ubuntu-latest` builds arm64 through QEMU; public repositories can use `ubuntu-24.04-arm` and drop the QEMU step |

The workflow pushes a commit, so run `git pull` before your next push.

## 3. Release by hand

```sh
azmod-pack build .
azmod-pack package . --github your-name/beat-counter --tag beat-counter-1.0.0
```

Create a GitHub release with that tag, attach both files from `dist/`, then add the entry to
the repository file on the default branch. The first release creates the repository file, which needs its three
fields. Later releases add to it:

```sh
# first release: creates azmods-repo.json
azmod-pack repo add azmods-repo.json dist/beat-counter-1.0.0.entry.json \
    --name "Beat Counter" --maintainer your-name --homepage https://github.com/your-name/beat-counter
# later releases
azmod-pack repo add azmods-repo.json dist/beat-counter-1.1.0.entry.json

azmod-pack repo check azmods-repo.json
git add azmods-repo.json && git commit -m "beat-counter 1.0.0" && git push
```

`git add` matters on the first release: `git commit -a` commits only files git already tracks,
so a new `azmods-repo.json` would be left out and decks would see no mods.

With `--github` or `--package-url`, `package` refuses a mod whose summary, description or icon
is still the template's placeholder, because users see those in Browse. Without either option, it
writes a package for local testing and no repository file entry.

To host outside GitHub releases, pass the final `https://` addresses instead of `--github`:
`--package-url https://example.com/beat-counter-1.0.0.azmod` (the icon defaults to the same
folder; `--icon-url` sets it). Decks download over HTTPS only.

## 4. Check it the way a deck will

```sh
azmod-pack repo check your-name/beat-counter --download
```

This fetches `azmods-repo.json` from the same address a deck uses, downloads every package,
checks each against the entry's `sha256` and `size`, and validates it.

A package replaced after publishing (same name, new bytes) fails the deck's hash check until
the repository file changes. Release a new version instead.

For a test server with its own certificate authority (for example, the examples'
`serve-local.py`), pass `--cafile PATH/ca.pem` to add that CA to the system's.

Then add the repository on a deck and install from it ([Testing on your deck](04-testing.md)).

## The repository file entry

`azmod-pack package` writes the entry to `dist/<id>-<version>.entry.json`.

| Field | |
|---|---|
| `id`, `name`, `version`, `author`, `summary` | from `azmod.json` |
| `icon` | `https://` URL of the 256x256 icon |
| `package` | `https://` URL of the `.azmod` |
| `sha256`, `size` | of the `.azmod` file; the deck checks both before opening it |
| `firmware`, `hooks`, `source` | from `azmod.json`; shown on the mod's page |
| `updated` | the release date, `YYYY-MM-DD` |
| `category`, `license`, `min_core`, `description` | from `azmod.json` when present |
| `assets_kb` | when the mod has `assets/`: what it keeps on the USB, shown before install |

## The package

A `.azmod` is a zip archive (stored, with fixed times and modes, so the same inputs give the
same bytes) with these files at its root:

| File | |
|---|---|
| `azmod.json` | the manifest |
| `<id>.so` | the library |
| `icon.png` | 256x256 PNG, 8-bit RGBA or RGB, not interlaced; metadata chunks such as color profiles are dropped when packaging |
| `LICENSE` | optional |
| `shots/*.png` | optional screenshots for the mod's page |
| `assets/...` | optional files the mod reads through `assets_dir()` (samples, images, presets), installed to the USB |

| Package limit | |
|---|---|
| files | 256 |
| unpacked size | 16 MB |
| an asset's path | up to 128 characters; letters, digits, `.`, `_` and `-`, not starting with `.`; at most two folders inside `assets/` |
| asset names | unique ignoring case: the USB is FAT32, where `Kick.wav` and `kick.wav` are one file |

`azmod-pack` leaves out hidden files (`.DS_Store`, `desktop.ini`, `Thumbs.db` and the like) and
warns when `assets/` passes 4 MB, since users download the package over WiFi. The mod can fetch
larger files into `data_dir()` from a thread of its own.

### `azmod.json`

The manifest is at most 16383 bytes: one flat JSON object of at most 48 keys, with no nested
objects and no repeated keys.

| Field | Required | |
|---|---|---|
| `azmod` | yes | `1` |
| `id` | yes | 3 to 32 characters of `a-z`, `0-9`, `-`; not `enabled`, `azmod`, `azstem`, `az-mods`, `azmods`. Must be unique everywhere and stable forever ([below](#pick-an-id-that-stays-yours)) |
| `name` | yes | display name, up to 47 characters |
| `version` | yes | `MAJOR.MINOR.PATCH`, each part 0 to 9999 without leading zeros, no suffix: a deck offers an update only for a higher number |
| `author` | yes | up to 47 characters |
| `summary` | yes | one line, up to 119 characters, shown in the grid |
| `description` | no | plain text for the mod's page, newlines allowed. The deck shows the first 599 bytes as one paragraph; the desktop app shows all of it |
| `license` | yes | the source's license: `GPL-3.0-only` (the template's) or `GPL-3.0-or-later` ([License](01-getting-started.md#license)) |
| `source` | yes | public `https://` URL of the source |
| `api` | yes | `1`, the ABI this SDK builds for |
| `firmware` | yes | `["any"]` alone, or `ep147-` tags ([Hooks and firmware tags](05-hooks-and-firmware.md)) |
| `hooks` | yes | `true` only with tags; `false` for `["any"]` |
| `min_core` | no | the lowest AZ-MODS version the mod needs (`AZ_CORE_VERSION` in `azmod-mod.h`) |
| `category` | no | `visual`, `controls`, `audio` or `tools` (the Browse filter) |

A deck also refuses to install a package whose `azmod.json` `version` differs from the entry's.

## Pick an ID that stays yours

The id names the mod's folder on the USB (`MODS/community-mods/<id>/`), its switch
(`community-mods.<id>`), its values in `MODS/azmods.cfg` and its data folder
(`MODS/community-mods/<id>/data/`). Two mods with the same id
cannot both be installed.

| | |
|---|---|
| Make it yours | start it with your GitHub name or a word only you would use: `djnova-beat-counter`, not `beat-counter` |
| Look first | search the [Mod Index](https://github.com/Kyle-Hosman/az-mods-index) and the repositories it names; its check refuses a pull request that adds an id a listed repository already uses (compared ignoring case) |
| Don't change it | a changed id is a different mod to every deck: the old one stays installed beside it, and the new one starts without its settings. The `name` is only a label and can change |

When two repositories offer the same id:

- **Browse** shows one of them, and its page names the other. Browse prefers the one installed
  from, if either is installed; otherwise a repository in the Mod Index over one the user
  added; otherwise the one added first.
- An installed mod belongs to the repository it came from. Updates come only from there, and
  the same id from another repository is refused until the installed one is removed
  ("*Name* by *author* already uses this ID. Remove it first.").
- Settings and data belong to the repository too. A mod that replaces another author's mod
  under the same id starts with fresh settings and an empty data folder; the other mod's are
  kept aside and return if it is installed again.

Two mods may share a display `name`; the deck and the app show each with its author.

## The Mod Index

Listing is optional. The [Mod Index](https://github.com/Kyle-Hosman/az-mods-index) is
the set of repositories every deck and the desktop app show by default, without anyone adding
them. To list a repository, open a pull request there that adds its URL to
`azmods-repos.json`. The list's README describes the review: public source, a passing
validator, the [Guidelines](GUIDELINES.md) and an accurate description. `azmod-pack list check
azmods-repos.json --download` runs the same checks as its CI.

A repository off the list works the same once a user adds it, but its mods' pages say "Not in
the Mod Index. Install only what you trust."

## `azmod-pack` reference

The tool is `tools/azmod-pack` (`tools\azmod-pack.cmd` on Windows), and `azmod-pack --version`
prints the SDK version.

| Command | Does |
|---|---|
| `new DIR --id ID [--name NAME] [--author USER]` | copy the template into a new folder under that id |
| `build [MODDIR] [--image IMAGE] [--any-image] [--include DIR] [--no-validate] [-- MAKE_ARGS]` | build in the container (`az-mods-sdk:1`, built on first use), then validate. Refuses an image not built from this SDK's `docker/` unless `--any-image` ([setup](02-setup.md#the-sdk-image)). `MAKE_ARGS` go to make, for example `DEBUG=1 OUT=out-debug` |
| `image [--image IMAGE]` | build or rebuild the container from this SDK's `docker/`, labeled with its hash |
| `validate MODDIR \| FILE.azmod ...` | the checks below |
| `package [MODDIR] [--github OWNER/REPO] [--tag TAG] [--out DIR] [--package-url URL] [--icon-url URL]` | validate, then write `<id>-<version>.azmod`, its icon and (with `--github` or `--package-url`, which refuse placeholders) the repository file entry (default `MODDIR/dist`) |
| `repo add REPO.json ENTRY.json [--name N --maintainer M --homepage URL]` | add or replace an entry; the three options create a new repository file |
| `repo check REPO.json \| OWNER/REPO \| URL [--download] [--cafile PEM]` | validate a repository file, and with `--download` every package; `--cafile` trusts one more CA |
| `list check azmods-repos.json [--download] [--cafile PEM]` | validate the Mod Index and every repository on it |

### What `validate` checks

| Check | Also refused by a deck |
|---|---|
| `azmod.json`: fields, types, lengths and formats above | yes |
| an aarch64 ELF shared library with its section table | yes |
| exports `az_mod_v1_entry`, as a function | yes |
| exports nothing else (besides the linker's own) | yes |
| exports none of the names the core interposes (`read`, `write`, `open`, `ioctl`, `mmap`, `dlopen`, `sqlite3_*` and the like) | yes |
| depends only on `libc`, `libm`, `libdl`, `libpthread`, `librt`, `libgcc_s`, `libstdc++`, `ld-linux-aarch64` | yes |
| needs no symbol version newer than the deck has (`GLIBC_2.29`, `GLIBCXX_3.4.25`, `CXXABI_1.3.11`, `GCC_7.0.0`) | as `Could not load` |
| the library is at most 16 MB | yes |
| no RPATH or RUNPATH, no text relocations | |
| the icon: a 256x256 PNG the deck can show | |
| `assets/`: the package limits above | at install |

Warnings are allowed but worth fixing: constructors beyond the compiler's own, a library that
names `/home/root/settings` (the deck's small settings partition), the template's placeholder
summary, description or icon, a description longer than the deck shows, no `LICENSE`, unknown
keys in `azmod.json`.
