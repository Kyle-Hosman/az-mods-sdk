# Objects

Field offsets of the EP147 objects that answer "which deck" and "what is loaded". Unless
a row says otherwise, released AZ-MODS code reads the offset on both builds, so it is the
same on 1.30 and 2.00. Addresses are 1.30 values; the 2.00 value
of every one is in [`addresses.csv`](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/reference/data/addresses.csv).

Offsets are in bytes from the start of the object. `u32`, `u64`, `f64` are little-endian.

## Which deck an object belongs to

Take the deck from the constructor that receives it as an argument, then confirm it
against the object's own field. Arrival order, slot position and "the first one found"
are not identities: they are right on deck 1 and wrong on deck 2.

| Object | Deck comes from | Confirm with |
|---|---|---|
| `dj_player::DjPlayerUnit` (the audio side of a deck; vtable `0x289f878`) | ctor `0x14a2560`, whose second argument is the player id | `u32` at `[unit + 0x1a0] + 0x1f18` = deck 0 to 3. Recorder units of the same base have `+0x1a0` = 0. |
| `gui::LoadState` (vtable `0x2ddb878`) | ctor `0x1f9c498`, deck in `w4` (0 to 3) | `u32` at `+0x47c` = deck, `u32` at `+0x4a8` = `0x5e715e28` |
| `gui::BpmState` (one per deck) | ctor `0x1f5da20(self, int deck)` | low byte of the `u64` at `+0x08` = deck |
| waveform widget (`WaveformWidgetBase`) | ctor `0x1b8f950`, deck in its fourth argument | `u32` at `+0x197f0` = deck |
| per-deck preview tile | owner `0x1c1fad0`, deck in its third argument, **1-based** | |

## `gui::LoadState`: what a deck has loaded

| Offset | Type | Meaning |
|---|---|---|
| `+0x000` | vtable | `0x2ddb878` |
| `+0x150` | pointer | its `gui::LoadedTrackInfo` (below) |
| `+0x47c` | `u32` | deck, 0 to 3 |
| `+0x4a8` | `u32` | `0x5e715e28` on a constructed LoadState |

## `gui::LoadedTrackInfo`

| Offset | Type | Meaning |
|---|---|---|
| `+0x00` | vtable | `0x2dd9288`: check it every time you read through the pointer |
| `+0x08` | 16 bytes | the loaded track's `trackid::TrackID` (all zero on an empty deck) |
| `+0x38` | `juce::String` | title (a `juce::String` is one pointer to its UTF-8 text) |
| `+0x40` | `juce::String` | artist |

The fields change while a track loads; a reader on another thread can see one load's
TrackID beside the next load's title. Read all three, then read the TrackID again and
keep the result only if it did not change.

## `trackid::TrackID` (16 bytes)

| Offset | Type | Meaning |
|---|---|---|
| `+0x00` | 8 bytes | which library: `01 00 00 00 02 01 01 00` for a USB library; bytes 4 and 5 hold the media id |
| `+0x08` | `u32` | the track's `content_id` in that library's database |
| `+0x0c` | `u32` | zero |

Two decks holding the same song hold equal TrackIDs. Nothing in the 16 bytes names the
USB stick, so a TrackID kept across a USB change can name a different song: keep
them in memory, for the life of the process.

## `gui::BpmState`

| Offset | Type | Meaning |
|---|---|---|
| `+0x08` | `u8` (low byte of a `u64`) | deck, 0 to 3 |
| `+0xa0` | `f64` | live, tempo-adjusted BPM (measured on 1.30) |
| `+0xa8` | `f64` | the same, x100 (measured on 1.30) |
| `+0xb0` | `f64` | the track's analyzed BPM |

"No track" is `DBL_MAX` in the BPM fields, not zero. If you only need the BPM, use the
core's `deck_bpm(deck)` instead of a hook.

## From a deck to its key/tempo engine

`KeyControlFGPR` (vtable `0x27ba108`) does the deck's key shift and time-stretch. It is
built inside the `DjPlayerUnit` constructor, so it is reached from the unit by fixed
pointers, each one vtable-checked:

| Step | Read | Expect vtable |
|---|---|---|
| unit to `PlayerBasisUnit` | `[unit + 0x180]` | `0x289f798` |
| to `CascadedTimeStretchManager` | `[pbu + 0x10]` | `0x27b8a50` |
| to the `KeyControlFGPR` | `[mgr + 0x48] - 0xe0` (the pointer is to an interface inside it) | `0x27ba108` |

If any step fails its check, the answer is "unknown", not another deck's engine.

## When an offset differs between builds

2.00 is a recompile, and a few objects changed size. Example: `MusicLibraryServer`
grew by 8 bytes, so its dispatcher pointer moved from `+448` to `+456`. The address map
cannot show that: it maps addresses, not layouts. To carry a field offset to another
build, find that build's own instruction that reads the field (the `ldr xN, [xM, #off]`
in the matching function), and prove the new offset with behavior before trusting it.
