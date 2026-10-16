# Set up your environment

## Requirements

| | macOS | Windows | Linux |
|---|---|---|---|
| Container engine | Docker Desktop | Docker Desktop with the WSL 2 engine | Docker Engine |
| arm64 containers | native on Apple Silicon; emulated on Intel | emulated | native on arm64; on x86_64, install the arm64 emulator (below) |
| Python 3.8+ | Command Line Tools (`python3`) | [python.org](https://www.python.org/downloads/windows/) | the distribution's package |
| clang, for `make test` | Command Line Tools | a Linux container ([Testing](04-testing.md#running-it)) | the distribution's `clang` (and `clang++` for C++) |
| git | Command Line Tools | [Git for Windows](https://git-scm.com/download/win) | the distribution's package |
| `azmod-pack` | `tools/azmod-pack` | `tools\azmod-pack.cmd` (PowerShell or Command Prompt) | `tools/azmod-pack` |

`azmod-pack` and the SDK's other tools use only the Python standard library (3.8 or newer).
Building these docs as a website requires Python 3.11 or newer ([CONTRIBUTING](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/CONTRIBUTING.md)).

Emulated arm64 is slower but produces the same bytes.

The first build creates the `az-mods-sdk:1` image (Ubuntu 18.04 arm64, GCC 7.5 for C and C++,
glibc 2.27). Later builds reuse it.

## Install and build the template

### macOS

1. Install the command line tools (git, Python 3, clang):

   ```sh
   xcode-select --install
   ```

2. Install [Docker Desktop for Mac](https://www.docker.com/products/docker-desktop/), choosing
   the Apple silicon or Intel chip build to match the Mac. Start it and wait until it reports
   that the engine is running.
3. In Terminal, run:

   ```sh
   git clone https://github.com/Kyle-Hosman/az-mods-sdk
   cd az-mods-sdk
   tools/azmod-pack build template
   ```

### Windows (PowerShell)

1. Install [Docker Desktop for Windows](https://www.docker.com/products/docker-desktop/) with
   the WSL 2 engine it offers by default. The installer may ask to restart. Start Docker
   Desktop and wait until it reports that the engine is running.
2. Install [Python 3](https://www.python.org/downloads/windows/).
3. Install [Git for Windows](https://git-scm.com/download/win).
4. Open a new PowerShell window (a window opened before the installs does not see them) and
   run:

   ```powershell
   git clone https://github.com/Kyle-Hosman/az-mods-sdk
   cd az-mods-sdk
   tools\azmod-pack.cmd build template
   ```

### Linux

1. Install [Docker Engine](https://docs.docker.com/engine/install/) and allow Docker without
   `sudo`, then log in again:

   ```sh
   sudo usermod -aG docker $USER
   ```

2. On x86_64 only, install the arm64 emulator (once per machine):

   ```sh
   docker run --privileged --rm tonistiigi/binfmt --install arm64
   ```

   Some distributions package this as `qemu-user-static` instead.
3. Install git, Python 3 and clang from the distribution's packages, then run:

   ```sh
   git clone https://github.com/Kyle-Hosman/az-mods-sdk
   cd az-mods-sdk
   tools/azmod-pack build template
   ```

### Windows: working inside WSL 2

Turn on Docker Desktop's WSL integration for the distribution (Settings > Resources > WSL
integration) and follow the Linux steps inside it. Keep the mod's folder in the Linux file
system (for example `~/mods`) rather than under `/mnt/c`, where builds are much slower.

## Result

A successful build ends with the validator's verdict:

```
validate /.../az-mods-sdk/template: OK
```

and leaves the library at `template/out/hello-az.so`.

`azmod-pack` checks Docker before it builds. When Docker is missing, not running, or cannot
run arm64 containers, it names the fix.

A folder that Docker Desktop does not share mounts empty, and `azmod-pack` reports it as
`Docker cannot see ...`. Keep the mod under your home folder, or add its location under
Settings > Resources > File sharing.

Other errors are covered in the [FAQ](07-faq.md#build-problems).

## The SDK image

The image carries a label with a hash of the SDK's `docker/` folder. `azmod-pack build`
compares that label with the SDK it runs from. It refuses an image built from another
`docker/` (an older SDK, or a `docker build` by hand) and prints the command that rebuilds it.

| | |
|---|---|
| After updating the SDK (`git pull`) | `azmod-pack image` rebuilds `az-mods-sdk:1` when `build` asks for it |
| Another tag | `azmod-pack build MODDIR --image TAG`; `azmod-pack image --image TAG` builds it |
| An image of your own | `--image TAG --any-image` skips the label check; the bytes may then differ from an official build |

## Line endings

The SDK's `.gitattributes` keeps LF line endings on every system, so the Makefile works in
the Linux container. Files you add to a mod by hand need LF endings too. A CRLF file shows up
as `/bin/sh^M: bad interpreter` or as `missing separator` from make.

## Check the build is byte-identical

The same source builds to the same bytes on every machine, because the container pins its
base image and every toolchain package. The SDK records the template's hash, so you can
compare your build with it:

| System | From the SDK folder |
|---|---|
| Linux, WSL | `cd template/out && sha256sum -c ../../tests/expected/hello-az.so.sha256` |
| macOS | `cd template/out && shasum -a 256 -c ../../tests/expected/hello-az.so.sha256` |
| PowerShell | `(Get-FileHash template\out\hello-az.so).Hash.ToLower() -eq (Get-Content tests\expected\hello-az.so.sha256).Split(' ')[0]` (prints `True` on a match) |

## `azmod-pack` on your PATH

The rest of the guide writes `azmod-pack` without its folder. To run it that way, add
`tools` to your PATH:

| Shell | From the SDK folder |
|---|---|
| macOS, Linux, WSL | `export PATH="$PWD/tools:$PATH"` (or in the shell profile) |
| PowerShell | `$env:PATH = "$PWD\tools;" + $env:PATH` (`azmod-pack` then runs `azmod-pack.cmd`) |
| Git Bash | `azmod-pack` itself fails there (`/usr/bin/env: 'python3'`): call `tools/azmod-pack.cmd`, or work in PowerShell |

On Windows, `azmod-pack.cmd` runs the `python` of the active virtual environment when one is
activated. Otherwise it uses the `py` launcher, and failing that, `python`.

**Next:** [Your first mod](03-first-mod.md).
