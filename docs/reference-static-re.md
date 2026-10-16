# Reading the program

How the addresses on these pages were found, so you can find the ones you need. Any
disassembler that reads AArch64 ELF works (Ghidra, IDA, Binary Ninja, radare2). The SDK's
build container has `objdump`, which is enough for the checks below.

## Get the program

EP147 is `/home/root/pdj/EP147` on the deck. Copy it from your own deck over SSH
(see [Testing on your deck](04-testing.md)), or take it from the system the
[emulator](08-emulator.md) pulled from your deck. Check its md5 against the
[firmware table](reference.md#firmware): the addresses are only valid for that exact
file. Don't publish the file or anything extracted from it
([Guidelines](GUIDELINES.md#6-share-your-source-never-the-decks-firmware)).

## Addresses

EP147 is not position-independent: it always loads at `0x400000`, with one code
segment mapped from file offset 0. So:

- **address = file offset + `0x400000`.** Tools that print file offsets
  (`strings -t x`, `perf`, a hex editor) need the `0x400000` added.
- Section starts differ per build; `objdump -h EP147` lists them.

Check the base once at the start, against an address you can confirm (any `function`
row of the [address map](reference-addresses.md) should start with a plausible
instruction). With a wrong base every cross-reference search comes back empty, and that
looks exactly like "not referenced".

## From a class name to its vtable (RTTI)

EP147 keeps C++ run-time type information, so classes can be found by name:

1. Find the mangled name string, for example `N3gui9LoadStateE` for `gui::LoadState`.
2. Find the `type_info` whose name pointer (its second word) points at that string.
3. Find the vtable whose word at `+0x08` points at that `type_info`. The vtable's
   address point, the value a constructed object's first word holds, is `+0x10` from
   there; the virtual functions follow it, one 8-byte slot each.
4. The constructor is the function that stores that address point into a new object:
   search for code that loads the address and stores it at `[x0]`.

Constructors are worth finding: the one that receives a deck number or an id as an
argument is the reliable way to tell four decks' objects apart (see
[Objects](reference-objects.md#which-deck-an-object-belongs-to)).

## Strings

A string in the file does not prove the code using it is still there: many assert and
`__PRETTY_FUNCTION__` texts survive while the code that referenced them was optimized
out. Before trusting one, find its reference (an `adrp` + `add` pair, or an 8-byte
literal pool entry holding its address).

## Checking a hook target

`claim_hook` relocates the first four instructions of the function, so it refuses one
whose first four include a branch (`b`, `bl`, `b.cond`, `cbz`, `cbnz`, `tbz`, `tbnz`),
an `adr` or a literal `ldr` (`adrp` is fine). Look before you claim:

```sh
docker run --rm -v "$PWD:/w" az-mods-sdk:1 \
    objdump -d --start-address=0x1f5da20 --stop-address=0x1f5da30 /w/EP147
```

Then confirm it is the start of the function you mean, not just something hookable:

- Its callers `bl` to exactly this address.
- It begins like a function (a stack frame being set up, typically `stp x29, x30, ...`).
- Don't walk backward from an interesting instruction to "the nearest prologue" over
  more than a few instructions: across a long distance that finds another function's
  start, and a hook there runs with the wrong arguments.

Declare the hook with every argument the function takes. A replacement that declares
fewer forwards whatever happens to be in the remaining registers. The `gui::LoadState`
constructor, for example, takes eight (the deck is the fifth).

## Testing

The [emulator](08-emulator.md) runs the same EP147 as a deck with that firmware, so an
address, a field offset or a hook can be proven there first. Put the new behavior behind
a setting that is off by default until it has worked on both, so a wrong guess costs a
toggle, not a restart loop.

## Name everything in Ghidra in one step

`reference/ghidra/ImportAzModsLabels.java` applies this reference to your own copy of
EP147. Import EP147 into Ghidra, add `reference/ghidra` to the Script Manager's script
directories, and run **ImportAzModsLabels** (category *AZ-MODS*). It picks the firmware
by the program's MD5, then names every documented function (creating it if the analysis
has not), labels the vtables with their class and the data addresses, and comments the
mid-function addresses instead of labeling them. Running it again changes nothing.

Headless, for example on a fresh import without auto-analysis:

```sh
analyzeHeadless /tmp/ghidra-proj ep147 -import EP147 -noanalysis \
    -scriptPath reference/ghidra -postScript ImportAzModsLabels.java
```

On the 1.30 build it creates and names 213 functions and labels 132 vtables and data
addresses. It is a plain Java GhidraScript, so it needs no Python support in Ghidra.
