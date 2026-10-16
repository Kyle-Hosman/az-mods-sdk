# Contributing to the AZ-MODS SDK

This repository is the SDK: the header mods include, the build container, `azmod-pack`, the
test harness and the developer guide. Writing a mod requires no changes here; for that, see
the [developer guide](docs/README.md).

## Before a pull request

For anything larger than a fix, open an issue first so the approach is agreed before the
work starts.

The header is a binary interface that every published mod depends on, so it only grows: new
fields go at the end of a struct and are checked with `AZ_HAS`. Changes land in the AZ-MODS
core first and are then copied here unchanged (`tools/check-header.py` compares the two).

## Checks

| Command | Checks |
|---|---|
| `python3 -m unittest discover -s tests -v` | `azmod-pack` |
| `tools/azmod-pack image` | the build container, labeled with a hash of `docker/` |
| `tools/check-doc-snippets.py` (and `--control`) | every C block in the guide compiles against the header |
| `tools/azmod-pack build template` | the template builds and validates |
| `cd template/out && sha256sum -c ../../tests/expected/hello-az.so.sha256` | the build is byte-identical to the recorded one |
| `make -C template test SAN=plain` (`asan`, `tsan`) | the template under the test harness |
| `tools/azmod-pack build tests/cxx/hello-cxx`, `make -C tests/cxx/hello-cxx test SAN=plain` (`asan`, `tsan`) | the C++ test mod builds, validates and passes the harness |
| `sh tools/harness/sanitize.sh --controls --passes plain` (`asan`, `tsan`) | every planted bug is caught (9, 3 and 1 on Linux) |
| `pip install -r docs/requirements.txt && mkdocs build --strict` | the website builds with no broken link or anchor |
| `python3 tools/public-hygiene .` | the hygiene rules below |
| `git ls-files '*.c' '*.cpp' ':!include/azmod-mod.h' \| xargs clang-format --dry-run --Werror` | the C and C++ sources are formatted (clang-format 21.1.8) |

CI runs all of them.

## Style

- C: C11. C++ in `tests/cxx/`: C++14. Both formatted with clang-format 21.1.8 and the
  repository's `.clang-format` (`pip install clang-format==21.1.8`, then
  `git ls-files '*.c' '*.cpp' ':!include/azmod-mod.h' | xargs clang-format -i`); CI checks it.
  `include/azmod-mod.h` is the core's file, copied unchanged.
- Comments explain why the code is written the way it is, not what each line does.
- Python in `tools/`: standard library only, Python 3.8 or newer. The docs website needs
  Python 3.11 or newer (`docs/requirements.txt` pins Markdown 3.11).
- Docs: plain US English, short sentences and no em dashes. Each page has to read well both
  on GitHub and on the website.

## Hygiene

Every push and pull request runs `tools/public-hygiene`. It keeps personal names, dates,
private paths and em dashes out of code, docs and commit messages, using the rules in
`.public-hygiene.json`. `tools/install-hooks.sh` installs the same check as a pre-push hook.

By contributing you agree that your contribution is licensed under the repository's
license (GPL-3.0-only).
