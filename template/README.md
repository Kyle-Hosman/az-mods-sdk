# Hello AZ

The template mod of the [AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk):
`azmod-pack new` copies this folder under a new id.

A page under **Mod Menu > Community Mods > Installed > Hello AZ** with one setting (show track
titles) and a live list of each loaded deck's BPM and track title. It reads deck state through
the core's services only: no hooks, no controls, audio or network.

| Shows | Where in `hello-az.c` |
|---|---|
| the descriptor and the one exported entry point | `MOD`, `az_mod_v1_entry` |
| setup in `init()` | `init` |
| a page in the Mod Menu | `SCREEN`, `on_draw`, `on_input`, `on_enter` |
| a checkbox setting with `cfg_get` / `cfg_set` | `init`, `toggle_titles` |
| theme colors from `color(AZ_COL_*)` | `on_draw` |
| a `tick()` that repaints on a change, paced by the monotonic clock | `tick`, `now_ms` |

## Build

From the SDK:

```sh
tools/azmod-pack build template
tools/azmod-pack package template --github your-name/your-repo
make -C template test AZ_SDK="$PWD"      # the host test harness (needs clang)
```

## Files

| File | |
|---|---|
| `hello-az.c` | the mod |
| `azmod.json` | the manifest: id, version, firmware, hooks, source |
| `icon.png` | 256x256, shown in the Mod Menu's Browse grid |
| `Makefile` | the build flags, each explained in its header; `make test` runs the harness |
| `.github/workflows/release.yml` | build, package and release on a pushed tag |

## License

GPL-3.0-only. See [LICENSE](LICENSE). A mod built from this template is covered by the
GPL-3.0 too and publishes its source (the SDK's guide, Getting started, "License").
