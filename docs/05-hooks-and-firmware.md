# Hooks and firmware tags

A **hook** redirects one of EP147's own functions to the mod's code, which then calls the
original. Mods use hooks to change behavior EP147 already has (what a button does, what a
screen shows, how a value is computed) or to read something the core's services do not expose.

The cost is a tie to the firmware. A function's address moves from one EP147 build to the
next, so a hook mod works only on the exact builds it was made for:

| | Loads on | After a new official firmware |
|---|---|---|
| Services | every firmware AZ-MODS supports | keeps working once AZ-MODS supports that firmware |
| Hooks | the listed builds only; any other shows `Not made for this firmware` before any of its code runs | needs its addresses found again on the new build, and a new release |

In `azmod.json`, a services mod declares:

```json
"firmware": ["any"], "hooks": false
```

and a hook mod lists its builds:

```json
"firmware": ["ep147-..."], "hooks": true
```

A wrong address runs the hook with the wrong arguments inside EP147's code: a crash, or
silent corruption.

Finding a function is reverse engineering on your own deck. Start from EP147 itself, at
`/home/root/pdj/EP147` on the deck. It is an aarch64 ELF executable, readable over SSH
([SSH access](04-testing.md#ssh-access)) and loadable in any disassembler or decompiler. Its
firmware tag (below) names the build the addresses belong to.

The [reference pages](reference.md) map what is already known: 354 named functions, vtables
and data addresses for each supported build, the objects behind decks and tracks, and the
control frames. `reference/ghidra/ImportAzModsLabels.java` applies those names to your own
copy of EP147 in Ghidra.

A mod publishes only its own code and the addresses it hooks, not firmware, files extracted
from it, or decompiled code ([Guidelines](GUIDELINES.md#6-share-your-source-never-the-decks-firmware)).

## Firmware tags

A tag names one exact build of EP147:

    ep147-0123456789abcdef

This example is not a real tag. A tag is `ep147-` followed by 16 lowercase hex digits: a
64-bit FNV-1a hash of EP147's executable segments, read from the program file.

| Changes when | Does not change for |
|---|---|
| any firmware update that changes EP147's code, even if the version users see stays the same | settings, tracks, the AZ-MODS version, installed mods, hooks already in place |

Every deck running that build reports the same tag, and so does the emulator.

| Where | How |
|---|---|
| On the deck | **Mod Menu > Community Mods > Repositories**, bottom line: `For mod makers: this deck's firmware tag is ep147-...` |
| Over SSH | the first line of `/tmp/azmod-community/loaded` (`fw=ep147-...`) |
| From a mod | `core->fw_tag()` |

## Declaring a hook mod

`azmod.json` and the descriptor must agree:

```json
"firmware": ["ep147-0123456789abcdef"],
"hooks": true
```

```c
static const char *const FW_TAGS[] = { "ep147-0123456789abcdef", NULL };
```

| Declared | Result |
|---|---|
| a tag list without this deck's tag | `Not made for this firmware`; nothing of the mod runs |
| `"hooks": true` | the mod's page tells users **Changes the deck software itself (uses hooks)** before they install |
| `"any"` and `"hooks": true` | refused: `Says "any" firmware but also patches code` |
| `"any"`, or `"hooks": false` | every `claim_hook` and `claim_vslot` is refused |

## `claim_hook`

```c
int claim_hook(const char *id, uintptr_t addr, void *hook, void **orig);
```

| Argument | |
|---|---|
| `addr` | the function's address in the running EP147; it must lie inside EP147's code |
| `hook` | the replacement, with exactly the same signature |
| `orig` | receives a pointer that runs the original function |
| returns | `0` on success, non-zero if refused; the reason is always in the mod's log |

The core replaces the function's first instruction with a branch to a jump island near it, and
runs the displaced instruction, relocated, from a trampoline before jumping back. The swap is
one aligned 4-byte store, so a thread already in the function sees either the old code or the
new.

| Refusal in the log | Cause |
|---|---|
| `not inside EP147's code` | `addr` is outside EP147's executable segments |
| `overlaps the hook at 0x... patched by <owner>` | `addr` is within 16 bytes of an existing hook (the core's or another mod's) |
| `the hook installer declined it` | a null or misaligned `addr`, or one of the function's first four instructions depends on where it sits: `b`, `bl`, `b.cond`, `cbz`, `cbnz`, `tbz`, `tbnz`, `adr` or a literal `ldr` (`adrp` is re-encoded and accepted) |
| `no jump island within reach of it, ...` | no island in branch range of `addr` |
| `a mod for "any" firmware may not patch code`, `azmod.json does not declare "hooks": true` | see [Declaring](#declaring-a-hook-mod) |
| `call it from init()` | the claim came after `init` returned, or from another mod's `init` |
| `called with this mod's id from outside its code` | the call did not come from the mod named by `id` |

If a function is declined, hook its caller or a function it calls instead. The core accepts a
wrong address whenever its first instructions happen to be movable, so read the first
instructions at each address in your own copy of EP147 and confirm they start the intended
function.

### Claim from `init`, last

```c
typedef int (*some_fn_t)(void *self, int value);
static some_fn_t orig_some_fn;

static int my_some_fn(void *self, int value)
{
    /* be quick: this runs on the deck's own thread, as often as the deck calls it */
    return orig_some_fn(self, value);       /* run the original, same arguments */
}

static int init(const az_core_v1 *core)
{
    C = core;
    if (!AZ_HAS(C, claim_hook) || !AZ_HAS(C, fw_tag)) return 1;
    /* ... settings, register_screen, everything that can fail ... */

    const struct fw_addrs *a = pick(core);  /* see "Several firmware builds" below */
    if (!a) return 2;
    if (C->claim_hook(MOD_ID, a->some_fn, (void *)my_some_fn, (void **)&orig_some_fn) != 0)
        return 3;                           /* refused: run without it, or not at all */
    return 0;
}
```

A hook cannot be removed while EP147 runs. If `init` fails after a claim, the library stays in
memory with the hook live until the next start, and the Mod Menu shows
`Its init() returned <n> (after patching code; reload to clear)`.

### One address, one owner

An address belongs to the AZ-MODS core or to exactly one mod. A claim on an owned address is
refused, and the mod then either runs without that feature or returns non-zero from `init`. Code is never
patched outside `claim_hook` and `claim_vslot`.

### Inside a hook

- Call the original as EP147 would have (same arguments), and return its result unless
  changing it is the point.
- It runs on whatever thread EP147 calls the function from, as often as EP147 calls it: no
  file or network access, no long-held locks, no allocations, no logging on every call.
- When the deck an object belongs to is not certain, pass the call through untouched.
  `deck_for_ds()` and `deck_for_modeobj()` answer "which deck is this?" for EP147's objects
  and return `-1` for unknown ([API reference](API.md#deck-identity)).
- Keep state shared with `tick` or a page in plain words, read and written with atomics
  (`__atomic_load_n`, `__atomic_store_n`).

## `claim_vslot`: one virtual method

```c
int claim_vslot(const char *id, uintptr_t slot, void *fn, void **orig);
```

This swaps one vtable entry instead of patching code, so it intercepts only calls made
through that vtable. `slot` is the address of the entry. The same declaration rules apply,
and a claim is also refused in these cases:

| Refused when | Log |
|---|---|
| `slot` is not 8-byte aligned | `not an aligned pointer` |
| `slot` is outside EP147's non-executable segments | `not inside EP147's data` |
| `fn` is not inside the mod | `the new function is not inside this mod` |
| the slot no longer holds a function in EP147's code | `the slot does not hold a function in EP147's code (already swapped?)` |
| another mod claimed it | `already claimed by <id>` |

The page's protection is restored after the swap, and `*orig` is published before the slot.

## Several firmware builds in one release

List every tag in `azmod.json` and `FW_TAGS`, and keep one row of addresses per tag:

```c
#include <string.h>

struct fw_addrs {
    const char *tag;
    uintptr_t   some_fn;        /* what you hook on this firmware */
};

static const struct fw_addrs FW[] = {
    { "ep147-0123456789abcdef", 0x0 /* your address for this firmware */ },
    { "ep147-fedcba9876543210", 0x0 /* your address for this firmware */ },
};

static const struct fw_addrs *pick(const az_core_v1 *core)
{
    const char *tag = core->fw_tag();
    for (size_t i = 0; i < sizeof FW / sizeof FW[0]; i++)
        if (tag && strcmp(tag, FW[i].tag) == 0)
            return &FW[i];
    return NULL;                /* never "use the first row" */
}
```

The core already refuses the mod on an unlisted build, so if `pick()` returns `NULL`, the
table and `FW_TAGS` disagree. Falling back to the first row would hook a wrong address.

## A new firmware

1. Read the new tag on a deck that has updated.
2. Find the addresses again on that build, and test on it.
3. Add the tag to `"firmware"`, `FW_TAGS` and the address table, bump `version`, and release.

Until that release, decks on the new firmware show **Not made for this firmware** and the mod
stays off.

## No hook needed

| To | Use |
|---|---|
| read or change pad and mode-button LEDs | `frame_filter` on `AZ_BUS_ERP_LED` (or `on_frame`) |
| read, change or swallow buttons, pads, jogs, knobs and faders | `frame_filter` on the input buses |
| take touches before EP147 sees them | `touch_filter` |
| draw over EP147's screens | surfaces (overlays) |
| know which deck an EP147 object belongs to | `deck_for_ds`, `deck_for_modeobj` |

All of these work with `"firmware": ["any"]`. The core fixes frame checksums after the last
filter. See the [API reference](API.md#control-frames-filters).

**Next:** [Packaging and publishing](06-publishing.md).
