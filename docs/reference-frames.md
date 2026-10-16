# Control frames

The deck's panel talks to EP147 over four serial buses, in fixed-size frames:

| Bus | Carries | Frame |
|---|---|---|
| ERP | performance pads, pad-mode buttons and deck buttons, both sides; pad and button LEDs | 30 bytes in, 128 bytes out (LEDs) |
| DECK 1, DECK 2 | each side's jog wheel and its deck controls | |
| mixer | mixer and browse controls | 128 bytes in |

A mod sees the outgoing LED frame through `on_frame` (`AZ_BUS_ERP_LED`,
[API](API.md#control-frames-on_frame)), and the frames of every bus through
`frame_filter` ([API](API.md#control-frames-filters)). The input layouts below are for
recognizing the controls in a filter, in the program and in your hooks.

## The LED frame (`AZ_BUS_ERP_LED`)

128 bytes. Both physical sides share it; side 2's cells sit `0x38` after side 1's.
Each cell is 4 bytes, `{R, G, B, unused}`.

| Cells | Offset (side 1) | Index |
|---|---|---|
| pad-mode buttons | `0x0e + 4 * j` | `j` = 0 HOT CUE, 1 BEAT LOOP, 2 SLIP LOOP, 3 BEAT JUMP |
| performance pads | `0x1e + 4 * k` | `k` = 0 to 7, pads 1 to 8 |

Bytes `0x78` and `0x79` hold the frame's CRC; the core recomputes it after the last mod,
so don't write them. Bytes `0x09` to `0x0c` hold per-LED luminance codes that stay
constant while the colors change; they do not gate your colors.

**The color bytes are linear light, not sRGB.** Writing an sRGB color straight in lifts
the weaker channels and the LED looks washed out (`#FF2020` shows as about `#FF6363`).
Convert each channel with the sRGB transfer function first:

```c
#include <math.h>

/* convert once, outside on_frame: a 256-entry table is the usual way */
static unsigned char led_byte(unsigned char srgb)
{
    float c = srgb / 255.0f;
    float lin = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    return (unsigned char)(lin * 255.0f + 0.5f);
}
```

What EP147 itself writes, so a mod can keep the deck's own conventions:

- Each pad-mode button has one hue: HOT CUE `98ff9a`, BEAT LOOP `d16300`, SLIP LOOP
  `0000ff`, BEAT JUMP `ff0000` (frame bytes). The selected mode's button is at full
  brightness, the other three show the same hue at 12/255 of it.
- On a secondary pad page all four buttons turn lime: selected `9bff00`, the others
  `070c00`. That is the only way the frame shows which page is active.
- To recolor a button and keep the deck's selected/dimmed state, read EP147's cell
  first: a channel at or above `0x40` means selected (write your color at full), all
  zero means off (leave it), anything else means dimmed (write your color x 12/255).
- Each physical side shows the deck currently selected on it: decks 1 and 3 on the
  left, 2 and 4 on the right.

## Input controls

[`bus-fields.csv`](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/reference/data/bus-fields.csv) lists the button bits and control
fields (116 rows, 43 named controls). Columns: `bus`, `instance` (`A` = side 1, `B` =
side 2, blank = one per frame), `frame_offset`, `bit`, `width`, `name`, `kind`
(`button-bit` = one bit per button; `frame-read` and `section` = a multi-byte field).

Measured controls that are not in that file, or need a word of explanation:

| Control | Where | Notes |
|---|---|---|
| pad-mode buttons | ERP byte `0x0d`: HOT CUE `0x01`, BEAT LOOP `0x02`, SLIP LOOP `0x04`, BEAT JUMP `0x08` | momentary: it reports the button, not the mode, and a secondary page sends the same bit |
| jog wheel | deck bus `0x09` touch, `0x0a`-`0x0b` position, `0x0c`-`0x0d` velocity | |
| browse knob | mixer byte `34`: absolute position counter, +1 per detent clockwise | press: byte `31` bit 0 |
| Beat FX select | mixer byte `0x16`, high nibble, 4-bit Gray code | consecutive positions differ by one bit |
| Beat FX on/off | mixer byte `0x16` bit 0 | momentary |
| Beat FX active | mixer byte `0x54` bit `0x80` | a report; the panel owns the latch |
| MENU | mixer byte `32` mask `0x04` | hold-sensitive: a short press opens the menu, a longer one UTILITY |
| TAG TRACK | mixer byte `33` mask `0x40` | |
| MIXER SETTINGS | mixer byte `0x18` mask `0x10` | |
| X-PAD | mixer `0x1d` position (0 to 124), `0x1e` zone bitmap | |

Some deck-bus bytes (`0x12`, `0x14`, `0x26`, `0x27`) change on every frame at rest;
skip them when you look for what a control changes.
