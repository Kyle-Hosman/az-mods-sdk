# Addresses

[`addresses.csv`](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/reference/data/addresses.csv) (and the same rows in
[`addresses.json`](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/reference/data/addresses.json)) lists 354 of the EP147 addresses
the AZ-MODS core uses, one row each:

| Column | Meaning |
|---|---|
| `name` | the name the core's source gives the address, if any |
| `v130`, `v200` | the address on each build (tags in [Reference](reference.md#firmware)) |
| `kind` | `function`, `vtable`, `data` (writable), `rodata` (read-only tables, images, strings), `mid-function` |
| `class` | the C++ class, for a vtable whose class the core names |
| `meaning` | the core's one-line comment, when it has one that stands on its own |
| `core_hook` | `yes` if the core hooks it, so `claim_hook` refuses it |

Totals: 213 functions, 102 vtables, 26 read-only and 4 writable data addresses, 9
mid-function addresses; 97 are hooked by the core.

## Using a row

Put both builds' values in your per-firmware table, the shape
[Hooks and firmware tags](05-hooks-and-firmware.md#several-firmware-builds-in-one-release)
describes:

```c
struct fw_addrs {
    const char *tag;
    uintptr_t   some_fn;        /* what you hook on this firmware */
};

static const struct fw_addrs FW[] = {
    { "ep147-79d43e5de779fc1b", 0x1f5da20 },   /* gui::BpmState::BpmState(self, int deck), 1.30 */
    { "ep147-9e9454e357389ece", 0x20c9d90 },   /* the same function, 2.00 */
};
```

That example is hooked by the core (`core_hook = yes`), so a claim on it is refused;
it shows the shape, not a target. Before claiming any address, check its first
instructions as [Reading the program](reference-static-re.md#checking-a-hook-target)
describes.

## Vtables

A `vtable` row is the address a constructed object's first word holds (the "address
point", past the offset-to-top and type_info words). Checking an object's first word
against it is how the core confirms an object is what it expects before it reads a
field. To call or hook a virtual function, read the slot at `vtable + 8 * index`.

## Finding a function that is not here

The map only covers what the core needed. For anything else, see
[Reading the program](reference-static-re.md): start from a vtable or a string here and
walk outward.
