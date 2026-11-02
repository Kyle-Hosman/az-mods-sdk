# Local ANLZ fixture evidence

This report validates the portable parser against the user-supplied local
`anlz-samples/ANLZ0000.DAT`, `.EXT` and `.2EX` files and an additional analysis-only pair
read from the authorized deck's USB. It describes static file observations and standalone
parser execution, not native EP147 cue/loop behavior. These observations come from ordinary bounded file inspection and the
local probe, not REA Evidence records. No track-derived fixture bytes are included
in the source contribution.

## Input identity

| Input | Bytes | SHA-256 |
|---|---:|---|
| ANLZ0000.DAT | 9142 | `2c61ca0e2898cc530ffc40f5003b0fa2309c5df0332d7952376d5697247bd9c1` |
| ANLZ0000.EXT | 176785 | `3612ee1a62b3df1adabde01d6a9f5f7dc6938e16f376d774f1a3156851a2ca7e` |
| ANLZ0000.2EX | 169039 | `f14c7a727698dcc43fc167ed273b822de737647c1b0dd44fffb4d8f1b15fb013` |

All three files have a PMAI header length of 28 and an advertised total length
matching their actual size. Every section was walked with bounds checked against
that container length.

The parser source used for the latest recorded host probe has SHA-256
`a6b93e22504c44f6743339058bf7038e746865ae67de203fa07ce59d70e87431`.
The probe source has SHA-256
`5716071b85dc634b85a42d375dd14b8d4553776e2fabc833f90033f1e1500d54`.
These identify the recorded check; later source changes require another run.

## Observed sections and field comparisons

| File / tag | Section file offset | Header / total bytes | Observed result |
|---|---:|---:|---|
| DAT PQTZ | `0x762` | 24 / 6048 | 753 eight-byte beat entries |
| DAT PWAV | `0x1f02` | 20 / 420 | 400 overview bytes |
| DAT PCOB, hot | `0x211e` | 24 / 192 | Three legacy hot slots: 1 through 3 |
| DAT PCOB, memory | `0x21de` | 24 / 472 | Eight memory cue entries |
| EXT PCOB, hot | `0xd821` | 24 / 304 | Five legacy hot slots: 4 through 8 |
| EXT PCOB, memory | `0xd951` | 24 / 24 | Explicit empty legacy memory list |
| EXT PCO2, hot | `0xd969` | 20 / 868 | Eight extended hot entries |
| EXT PCO2, memory | `0xdccd` | 20 / 868 | Eight extended memory entries |
| EXT PSSI | `0x2b091` | 32 / 512 | 20 masked, 24-byte phrase entries |

The independent probe compares every PQTZ entry's original time, stored tempo and
beat-in-bar value against the returned beat record. It compares every selected
extended cue's classification, slot, type, position, decoded comment and applicable color
fields: hot RGB or raw memory palette ID. All 16 selected cue entries in this sample are points, with the loop-end
field holding the point sentinel. This fixture therefore does not empirically
validate saved loops or automatic activation.

The PCO2 entry header length is 16 and each supplied entry spans 106 bytes. The
comment byte length at entry offset `0x28` is 18. The probe decodes the parser's
UTF-8 output back to code points and compares those against the source UTF-16BE,
rather than calling the production comment decoder. Hot RGB bytes following the comment
match the returned colors under `AZ_CUE_COLOR`. Memory entries expose their raw one-byte
ID at offset `0x1c` under `AZ_CUE_COLOR_ID`; all eight IDs match independently. The
`color_id`/`reserved` union preserves entry stride. Zero is a valid raw ID. Memory RGB
trailer bytes have no authoritative color meaning and no longer set `AZ_CUE_COLOR`.
This fixes the earlier comparison that incorrectly treated their zero trailers as black.
No EP147 palette mapping is inferred from raw memory IDs.

The PSSI payload is masked from section offset `0x12`. A separate full-buffer XOR
pass recovers mood 1, 20 phrases and an exclusive terminal beat boundary. Every
start/end index and raw kind/mood matches the production output. This establishes
the raw fields on this version-0 fixture, not normalized phrase semantics. Version and
stride are separate 16-bit fields. Version 1 has primary-format and synthetic coverage,
including an in-memory version-header change that preserves all 20 masked phrases;
this is not a natural version-1 export. Unknown versions mark phrases unsupported while
other valid components remain usable; malformed container lengths still fail. The 400 PWAV
payload bytes also match exactly. Time mapping from library duration is a separate
provider calculation and is not established by this file-only probe.

The combined DAT/EXT result is:

```text
full pair: 753 beats, 16 cues, 20 phrases, 400 overview bytes; every selected raw field matched
```

Memory ordering in this sample is the saved list's descending-position order.
The parser preserves file order. This observation does not establish native UI
ordering or a permanent identifier suitable for native recall commands.

## Split legacy hot-cue regression and control

The real export splits legacy hot slots between DAT and EXT. The original parser
preferred DAT for an entire legacy classification, so it silently dropped the
five hot slots held only in EXT whenever extended lists were absent.

The probe strips only PCO2 sections into private in-memory buffers and updates the
PMAI container length. Original files remain untouched. DAT is unchanged; the
derived EXT is 175049 bytes with SHA-256
`b82858daa61e9b8d8e04f6b4475d35b74664df434e39a36782a9ddf5ffd4f287`.
Before the fix the parser returned success with only 11 cues instead of the
independently expected 16, and the probe failed its count assertion. After merging
legacy hot lists by exact slot, the same probe returns:

```text
legacy fallback: result 0, expected 16 cues, actual 16 cues
```

No nonempty duplicated legacy memory lists across both files were supplied. The
verified policy is to merge hot lists by exact hot slot, preserve DAT precedence
for legacy memory when DAT contains that classification, and use EXT legacy
memory only when DAT lacks it. Extended PCO2 precedence remains independent per
classification. Do not introduce time-tolerance matching or coalesce identical
memory positions: that could erase distinct saved entries without evidence.

## Additional real saved hot-cue loops

A bounded inspection of twelve analysis-only DAT/EXT pairs from the operator's USB found a
pair with twelve points and two saved hot-cue loops. No audio, library file or native state
was modified. Its file identities are:

| Input | Bytes | SHA-256 |
|---|---:|---|
| DAT | 11368 | `2dd17f47278ddf87991b549a4d3f08d86f8ee5bdac35e3dbeea2a5eecbe0540a` |
| EXT | 235184 | `ed6c20e54ac5b7e7707f67d14b78a867a134fb83ccc690c54a502b52f61ca8be` |

The extended records encode a loop in hot slot 4 from 468590 to 480686 ms and a loop in hot
slot 1 from 203 to 46654 ms. The independent probe checks both returned boundaries, loop
type, classification, slot, comment and color against the raw entries. It also compares all
1025 beats, 25 phrases and 400 overview bytes. With extended lists removed in memory, the
legacy comparison checks every selected point/loop type and boundary, including exact hot
slot merging and DAT memory precedence.

```text
full pair: 1025 beats, 14 cues, 25 phrases, 400 overview bytes; every selected raw field matched
saved cue types: 12 points, 2 loops
legacy fallback: result 0, expected 14 cues, actual 14 cues
```

Both real pairs pass the final probe in host plain, ASan/UBSan and TSan builds. The saved-loop
pair also passes the standalone ARM ASan/UBSan probe on the 2.00 deck. Two private mutants
demonstrate the opposite outcome: returning loop-in as loop-out fails the extended boundary
comparison, and making that mistake only for legacy entries fails the stripped legacy
comparison. Original files remain untouched. This verifies stored hot-loop boundaries; it
does not establish automatic activation, saved memory loops, native engagement or persistence
after a native edit.

## Stored waveform windows and remaining calibration

The section bodies have these internally consistent stored entry sizes/counts:

| File / tag | Header bytes | Stored entry bytes | Stored entry count |
|---|---:|---:|---:|
| EXT PWV3 | 24 | 1 | 55035 |
| EXT PWV5 | 24 | 2 | 55035 |
| EXT PWV4 | 24 | 6 | 1200 |
| 2EX PWV7 | 24 | 3 | 55035 |
| 2EX PWV6 | 20 | 3 | 1200 |

The bounded-window service exposes PWV3/PWV5/PWV7 detail with encoding IDs 1/2/3 and a
150 Hz source-analysis axis. It also exposes raw PWV4/PWV6 previews under
`AZ_WAVE_PWV4_PREVIEW = 4` and `AZ_WAVE_PWV6_PREVIEW = 5`. PWV4 has a 24-byte header
and six-byte columns; PWV6 has a 20-byte header and three-byte columns. Preview requests
use valid library duration divided by the stored column count, explicitly approximate
because database duration has integer-second precision. Missing/invalid duration is
unsupported; the preview never inherits the detail clock. PWV4 selects EXT and PWV6
selects 2EX. These services preserve raw bytes without substituting them for PWAV.

An independent bounded tag walker compares three streamed preview windows (start,
midpoint and clipped tail) against raw bytes in each original file. Its deliberately
supplied 400-second reference duration checks the calculation, not measured track duration.
The stored suite also covers canonical short PCP2 entries, raw memory IDs, status values
0/1/4 and exact unique legacy-to-extended saved-loop matching. Mismatches, duplicates and
unknown statuses leave the saved property unavailable. These are format/synthetic Active
Loop checks; this pair contains no saved memory loops and establishes no native behavior.

The 2EX file also contains PWVC at offset 169019, header/total bytes 14/20, with payload
values 80, 100, 152. Its meaning remains unresolved: primary
[Dysentery research](https://github.com/Deep-Symmetry/dysentery/blob/main/doc/modules/ROOT/pages/track_metadata.adoc#requesting-vocal-detection-configuration)
describes vocal thresholds, while [Baken research](https://github.com/M-Igashi/baken/issues/147)
describes per-track gain. No native color/gamma/amplitude calibration, frequency cutoff,
precise playback alignment or faithful player rendering is inferred from these fields.

## Reproducing the local check

From the core checkout, with `ANLZ_DIR` pointing at the user's local sample folder:

```sh
cc -std=c11 -O1 -Wall -Wextra -Werror -Isrc/azmod \
    tests/unit/sdkrealanlz_probe.c -lm -o /tmp/sdkrealanlz-probe
/tmp/sdkrealanlz-probe "$ANLZ_DIR/ANLZ0000.DAT" \
    "$ANLZ_DIR/ANLZ0000.EXT" "$ANLZ_DIR/ANLZ0000.2EX"
```

The latest probe passes in plain, host ASan/UBSan and host TSan builds. An earlier provider
baseline also passed the original pair in all three modes as standalone processes on the
2.00 deck; those runs do not establish the later memory-color, phrase-version or preview
changes on hardware. This is distinct from in-process SDK acceptance. `tests/sdk-data-deck.sh` automates these checks and sanitizer
controls with an optional `ANLZ_SAMPLE_DIR`. Additional exports, real saved memory loops,
time-zero saved cues, automatic activation, native loaded-data precedence, native edits and
audible behavior require their own evidence. Synthetic and fuzz tests cover some parser
boundary cases separately.

The separate `tests/sdk-stored.sh` suite passes 356 asset-independent checks, or 557 with
the original DAT/EXT/2EX files, in host plain, ASan/UBSan and TSan modes. Twelve deliberate
stored-format/provider mutations must fail specific assertions: short PCP2 rejection,
unchecked entry headers, active-status loss, ambiguous counterpart acceptance, memory RGB,
preview 150 Hz clock, wrong PWV4 stride, wrong PWV6 header size, ignored duration validity,
undersized packed buffer, combined PSSI version/stride and unknown PSSI version parsing.
A thirteenth control restores the old request encoding upper bound and must reject the
regression through the real core engine. All thirteen controls are caught in each host mode.
The disk-provider fixture also tests real request/path selection for previews; its separate
`preview-extension` mutation removes PWV6's 2EX selection. These controls do not establish
native player behavior. Run the additional suite from the core checkout with:

```sh
ANLZ_SAMPLE_DIR="$ANLZ_DIR" SAN=asan tests/sdk-stored.sh
```

## PWV2 and PWAV bounded raw previews

The range service also accepts append-only encoding IDs6
(`AZ_WAVE_PWV2_TINY_PREVIEW`) and7 (`AZ_WAVE_PWAV_PREVIEW`). Both select DAT,
require the canonical20-byte header/count plus opaque `0x00010000` word, and
preserve one raw byte per column. They use the approximate valid library-duration
/count axis; they never inherit the150Hz detail clock. The opaque word has no
inferred gain or sample-rate meaning.

The immutable genuine-export snapshot contains91 DAT files with both formats. An
independent raw container walker compares start/middle/tail windows against every
file. No export is modified or embedded in contribution files. Synthetic checks
include empty and70,000-column sources, clipped windows, untouched tails, bad
count/header/word, duplicate tags and malformed later envelopes. The real disk
provider includes a conflicting EXT decoy to prove DAT and its freshness slot are
selected. Raw previews remain uncalibrated; PWVC gain/gamma/frequency/pixel
interpretation remains unresolved. Evidence is under
`anchor/output/sdk-feature-goal-20261028/mono-preview/`.
