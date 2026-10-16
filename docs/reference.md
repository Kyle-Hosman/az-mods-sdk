# Reference: the deck program

What is known about EP147, so a hook mod can start from a map instead of an empty
disassembler. Mods that use only the core's services do not need it
([Hooks and firmware tags](05-hooks-and-firmware.md)).

| Page | What it covers |
|---|---|
| [Addresses](reference-addresses.md) | 354 functions, vtables and data the AZ-MODS core uses, on each firmware |
| [Objects](reference-objects.md) | Field offsets of the objects behind decks, loaded tracks and BPM |
| [Control frames](reference-frames.md) | The LED frame `on_frame` hands you, and the button and control bytes of the panel |
| [Reading the program](reference-static-re.md) | Load address, RTTI, checking a hook target, testing on the emulator |

## Firmware

Every address on these pages belongs to one exact build of EP147, named by its
firmware tag (`core->fw_tag()`). Two builds are covered:

| Tag | EP147 md5 | Firmware |
|---|---|---|
| `ep147-79d43e5de779fc1b` | `3518275e8b4d5d63636d8d7b4468d7ba` | 1.30, and 1.32 (the AZ-MODS loader firmware, which carries the 1.30 program unchanged) |
| `ep147-9e9454e357389ece` | `8daa1de55d7328f458f6d855cdc3cdde` | 2.00 and 2.01 (2.01 adds the AZ-MODS loader; EP147 itself is the same file) |

In the data files the two columns are `v130` and `v200`. A firmware that is not in
this table has no addresses here: read your own deck's tag, and treat everything below
as a lead to re-find, not a value to use.

## The data files

Machine-readable copies live in [`reference/data/`](https://github.com/Kyle-Hosman/az-mods-sdk/tree/main/reference/data/):

| File | Contents |
|---|---|
| [`addresses.json`](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/reference/data/addresses.json), [`addresses.csv`](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/reference/data/addresses.csv) | the address map, with the firmware table above in `meta` |
| [`bus-fields.csv`](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/reference/data/bus-fields.csv) | button bits and control fields of the deck and mixer frames |

`addresses.*` is generated from the source of the AZ-MODS core it names in
`meta.source`, so it lists exactly the addresses the shipped core uses, with the names
and one-line comments that code gives them. It is regenerated with each core release.

## How sure these are

- Every address here is used by released AZ-MODS code on both builds, so it has run on
  real decks. That proves the core's use of it, not that it means what you need: read
  the code at the address before you rely on it.
- A **class** is listed only where the core's code names the vtable's class outright.
- **`core_hook = yes`** means the core already hooks that address, so `claim_hook`
  refuses it (one owner per address). A blank does not promise the address is free;
  `claim_hook`'s answer is the one that counts.
- **`kind = mid-function`** is an address inside a function (a call site, a branch
  target). It is never a hook target.
