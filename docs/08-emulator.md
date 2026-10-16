# XDJ-AZ emulator

The [XDJ-AZ emulator](https://github.com/Kyle-Hosman/xdj-az-emu) runs EP147 on a computer with
the same AZ-MODS core a deck runs, for building and testing mods before deploying them to the
AZ. It ships no firmware: `tools/az-emu-pull.py` copies the system from your own deck once,
over SSH, and the copy stays on your computer. The pull never reads the deck's update
partition, which holds the firmware encryption key, and refuses to write an image in which
its scan finds key material.

The emulator's own guides have the full steps and requirements:

| Guide | Covers |
|---|---|
| [Requirements](https://github.com/Kyle-Hosman/xdj-az-emu/blob/main/docs/requirements.md) | each platform's tools, with the install commands |
| [Guide](https://github.com/Kyle-Hosman/xdj-az-emu/blob/main/docs/guide.md) | install, copy the system, build, run, load a mod, the test suite |
| [Windows](https://github.com/Kyle-Hosman/xdj-az-emu/blob/main/docs/windows.md) | the same on a Windows PC |

## Platforms

| Platform | Support |
|---|---|
| Mac (Apple Silicon) | full speed |
| Windows PC | software emulation (audio does not play at full speed) |
| Intel Mac | software emulation (audio does not play at full speed); not tested |
| Linux | not supported yet |

The one-time pull needs a deck on firmware 2.00+ with [SSH access](04-testing.md#ssh-access).
AZ-MODS 1.2.3 asks to allow the new key with a **Remote access** prompt once no deck has a
track loaded. The build needs about 15 GB free (10 GB of it in Docker for the kernel), and the
emulated deck storage grows to about 10 GB in use.

## Set up the emulator and load a mod

Setup, once (the [Guide](https://github.com/Kyle-Hosman/xdj-az-emu/blob/main/docs/guide.md)
has each step in full):

1. Copy the system from your deck, with the SSH key the desktop app wrote:
   `tools/az-emu-pull.py --host <deck IP> --key ~/.azmods/ssh/xdjaz_ecdsa`
2. Build the kernel, guest tools and boot image with `./build.sh`, and QEMU and the app with
   `qemu/build.sh` and `tools/build-app.sh`.
3. Start the emulator (`AZ-EMU.app` on a Mac; on a PC, see
   [Windows](https://github.com/Kyle-Hosman/xdj-az-emu/blob/main/docs/windows.md)), then put
   the published AZ-MODS set on the emulated USB with `tools/az-mods-fetch.py` (it checks the
   set's signature).

Loading your mod, each time you build it:

```
tools/az-push-mod.py path/to/my-mod/
```

This copies the mod to the emulated USB, turns it on and approves it, so there is no
**New mod found: allow?** step. Installing from a repository (**Browse** and **Repositories**)
is not yet verified on the emulator; check it on a deck.

## Test suite

`tools/az-emu-test.py` boots the emulator headless and checks AZ-MODS itself: EP147 stays up
and maps the expected `azmod.so` and `azstem.so`, then the controls, stems, drum roll, the
4-deck view, the Mod Menu and Themes, each stage paired with a control that must fail. It runs
on Windows too. It tests an AZ-MODS build, not a mod; for a mod's own checks, use the
[test harness](04-testing.md#the-test-harness).
